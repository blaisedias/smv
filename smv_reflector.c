#include <assert.h>

#include "smv.h"

static const vis_signature_t replica_sig = {.chars = {'r', 'e', 'p', 'l', 'i', 'c', 'a', '\0'}};

// single threaded !

typedef struct smv_proxy_s {
    int shm_fd;
    vis_t* vis;
    __off_t shm_size;
    uint32_t sampled_buf_index;
    volatile bool keep_running;
    smv_sender_t* sender;
    // Secondary samples buffer, holds samples which are serialised
    // using multiple UDP packets.
    int16_t secondary_buffer[VIS_BUF_SIZE];
} smv_reflector_t;

void smv_teardown_reflector(smv_reflector_t* reflector) {
    smv_destroy_sender(&reflector->sender);
    if (reflector->shm_fd > -1) {
        if (reflector->vis != MAP_FAILED) {
            munmap(reflector->vis, sizeof(*reflector->vis));
            reflector->vis = MAP_FAILED;
        }
        close(reflector->shm_fd);
        reflector->shm_fd = -1;
        reflector->shm_size = -1;
    }
}

bool smv_init_reflector(smv_reflector_t* reflector, smv_if_addr_t ifaddr) {
    (void)replica_sig;
    if (NULL == reflector) {
        return false;
    }
    reflector->shm_fd = -1;
    reflector->vis = MAP_FAILED;
    reflector->shm_size = -1;
    reflector->sender = NULL;

    char shm_path[32];
    snprintf(shm_path, sizeof(shm_path), "/squeezelite-%02x:%02x:%02x:%02x:%02x:%02x",
            ifaddr.mac_addr[0], ifaddr.mac_addr[1], ifaddr.mac_addr[2],
            ifaddr.mac_addr[3], ifaddr.mac_addr[4], ifaddr.mac_addr[5]);

    reflector->shm_fd = shm_open(shm_path, O_RDWR, 0666);
    if (reflector->shm_fd == -1) {
        fprintf(stderr, "Failed to open shared memory %s, error:%s\n", shm_path, strerror(errno));
        return false;
    }

    struct stat shm_stat;
    if (fstat(reflector->shm_fd, &shm_stat) == -1) {
        fprintf(stderr, "Failed to stat shm %s\n", strerror(errno));
        close(reflector->shm_fd);
        reflector->shm_fd = -1;
        return false;
    }
    reflector->shm_size = shm_stat.st_size;
    assert(shm_stat.st_size >= (__off_t)sizeof(*reflector->vis));
    if (shm_stat.st_size == sizeof(vis_t)) {
        fprintf(stderr, "shm size == squeezelite\n");
    } else {
        fprintf(stderr, "shm size > squeezelite\n");
    }

    reflector->vis = mmap(0, reflector->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, reflector->shm_fd, 0);
    if (reflector->vis == MAP_FAILED) {
        smv_teardown_reflector(reflector);
    } else {
        reflector->sender = smv_create_sender(ifaddr, SMV_VERSION);
        if (NULL != reflector->sender) {
            pthread_rwlock_rdlock(&reflector->vis->rwlock);
            reflector->sampled_buf_index = reflector->vis->buf_index;
            pthread_rwlock_unlock(&reflector->vis->rwlock);
        } else {
            smv_teardown_reflector(reflector);
        }
    }
    return reflector->vis != MAP_FAILED;
}

#define POLL_SLEEP 10000
// approximate to sending a packet once a second
#define MAX_SEND_PACKET_COUNT_VALUE (1000000/POLL_SLEEP)
void smv_reflect(smv_reflector_t* reflector) {
    reflector->keep_running = true;
    bool silent = false;
    unsigned send_packet_counter = MAX_SEND_PACKET_COUNT_VALUE;
    while (reflector->keep_running) {
        int16_t *audio_data = NULL;
        uint32_t audio_sample_count = 0;

        pthread_rwlock_rdlock(&reflector->vis->rwlock);
        // First send any pending data
        if (reflector->vis->buf_index < reflector->sampled_buf_index) {
            // samples buffer wrap around
            int16_t* p1 = reflector->vis->buffer + reflector->sampled_buf_index;
            uint32_t samples1 = VIS_BUF_SIZE - reflector->sampled_buf_index;
            assert(samples1%2 == 0);
            int16_t* p2 = reflector->vis->buffer;
            uint32_t samples2 = reflector->vis->buf_index;
            assert(samples2%2 == 0);
            audio_data = reflector->secondary_buffer;
            audio_sample_count = samples1+samples2;
            assert(audio_sample_count <= VIS_BUF_SIZE);
            memcpy(audio_data, p1, samples1*sizeof(*audio_data));
            memcpy(audio_data+samples1, p2, samples2*sizeof(*audio_data));
            silent = false;
            reflector->sampled_buf_index = reflector->vis->buf_index;
#ifdef DEBUG
            printf("\n#%u, ", samples1 + samples2);
#endif
        } else if (reflector->vis->buf_index > reflector->sampled_buf_index) {
            int16_t* p = reflector->vis->buffer + reflector->sampled_buf_index;
            uint32_t samples = reflector->vis->buf_index - reflector->sampled_buf_index;
            assert(samples%2 == 0);
            audio_data = reflector->secondary_buffer;
            audio_sample_count = samples;
            assert(audio_sample_count <= VIS_BUF_SIZE);
            memcpy(audio_data, p, samples*sizeof(*audio_data));
            silent = false;
            reflector->sampled_buf_index = reflector->vis->buf_index;
#ifdef DEBUG
            printf("%u, ", samples);
#endif
            send_packet_counter = 0;
        } else if (!reflector->vis->running) {
            // Then check is not running
            if (!silent) {
                // transition to silent, send silent packet immediately
                silent = true;
                send_packet_counter = MAX_SEND_PACKET_COUNT_VALUE;
            }
        } else {
            // No change in status;
        }
        pthread_rwlock_unlock(&reflector->vis->rwlock);

        // if and audio packet was created successfully, send that
//        if (audio_packet) {
        if (audio_data) {
            smv_send_data(reflector->sender, audio_data, audio_sample_count);
        } else {
            if (++send_packet_counter >= MAX_SEND_PACKET_COUNT_VALUE) {
                send_packet_counter = 0;
                if (silent) {
                    smv_send_silent(reflector->sender);
#ifdef DEBUG
                    putchar('s');
#endif
                } else {
                    smv_send_keep_alive(reflector->sender);
#ifdef DEBUG
                    putchar('k');
#endif
                }
            }
        }
#ifdef DEBUG
        fflush(stdout);
#endif
        // squeezelite sends audio data 100 times a second, attempt to match
        usleep(POLL_SLEEP);
    }
}

// derived  from squeezelite/jivelite
int platform_get_mac_addresses(mac_address_t* mac_addrs, int size) {
    static mac_address_t zeromac = {0, 0, 0, 0, 0, 0};
    struct ifconf ifc;
    struct ifreq *ifr, *ifend;
    struct ifreq ifreq;
    struct ifreq ifs[size - 1];
    char* utmac;
    int count = 0;

    int s = socket(AF_INET, SOCK_DGRAM, 0);

    ifc.ifc_len = sizeof(ifs);
    ifc.ifc_req = ifs;

    if (ioctl(s, SIOCGIFCONF, &ifc) == 0) {
        ifend = ifs + (ifc.ifc_len / sizeof(struct ifreq));

        for (ifr = ifc.ifc_req; ifr < ifend; ifr++) {
            if (ifr->ifr_addr.sa_family == AF_INET) {
                strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));
                if (ioctl(s, SIOCGIFHWADDR, &ifreq) == 0) {
                    memcpy(mac_addrs[count], ifreq.ifr_hwaddr.sa_data, 6);
                    if (memcmp(zeromac, mac_addrs[count], sizeof(zeromac))) {
                        // skip lo interface
                        ++count;
                    }
                }
            }
        }
    }

    close(s);

    utmac = getenv("UTMAC");
    if (utmac) {
        if (strlen(utmac) == 17) {
            sscanf(utmac, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", &mac_addrs[count][0], &mac_addrs[count][1], &mac_addrs[count][2],
                   &mac_addrs[count][3], &mac_addrs[count][4], &mac_addrs[count][5]);
        }
    }

    return count;
}

volatile bool* keep_running_ptr;
void handle_signal(int sig) {
    (void)sig;
    fprintf(stderr, "\nGot signal %d. Terminating\n", sig);
    if (keep_running_ptr) {
        *keep_running_ptr = false;
    }
}

extern int platform_get_if_addresses(smv_if_addr_t* smv_ifaddrs, int size);

int main() {
    smv_reflector_t refl;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    /*
    mac_address_t macs[16];
    memset(macs, 0, sizeof(macs));
    int count = platform_get_mac_addresses(macs, 16);
    */
    smv_if_addr_t ifaddrs[16];
    int count = platform_get_if_addresses(ifaddrs, 16);
    if (count == 0) {
        fprintf(stderr, "no network interfaces found\n");
    } else if (count == 1) {
        fprintf(stderr, "%02x:%02x:%02x:%02x:%02x:%02x\n",
                ifaddrs[0].mac_addr[0],
                ifaddrs[0].mac_addr[1],
                ifaddrs[0].mac_addr[2],
                ifaddrs[0].mac_addr[3],
                ifaddrs[0].mac_addr[4],
                ifaddrs[0].mac_addr[5]
               );

        if (smv_init_reflector(&refl, ifaddrs[0])) {
            printf("Init reflector OK\n");
            fflush(stdout);
        }
        keep_running_ptr = &refl.keep_running;
        smv_reflect(&refl);
        smv_teardown_reflector(&refl);
    } else {
        printf("Multiple mac addresses\n");
        for (int i = 0; i < count; ++i) {
            printf("%d) %02x:%02x:%02x:%02x:%02x:%02x\n", i,
                ifaddrs[i].mac_addr[0],
                ifaddrs[i].mac_addr[1],
                ifaddrs[i].mac_addr[2],
                ifaddrs[i].mac_addr[3],
                ifaddrs[i].mac_addr[4],
                ifaddrs[i].mac_addr[5]
               );
        }
        for (int i = 0; i < count; ++i) {
            printf("trying %02x:%02x:%02x:%02x:%02x:%02x\n",
                ifaddrs[i].mac_addr[0],
                ifaddrs[i].mac_addr[1],
                ifaddrs[i].mac_addr[2],
                ifaddrs[i].mac_addr[3],
                ifaddrs[i].mac_addr[4],
                ifaddrs[i].mac_addr[5]
                );
            if (smv_init_reflector(&refl, ifaddrs[i])) {
                printf("Init reflector OK\n");
                fflush(stdout);
                keep_running_ptr = &refl.keep_running;
                smv_reflect(&refl);
                smv_teardown_reflector(&refl);
                break;
            }
        }
    }
    /*
        printf("sizeof(vis_t) = %ld\n", sizeof(vis_t));
        printf("sizeof(vis_ext_t) = %ld\n", sizeof(vis_ext_t));
        printf("%ld\n", strlen("/squeezelite-40:b0:76:a3:cc:24"));
        printf("%d\n", getpid());
        */
}
