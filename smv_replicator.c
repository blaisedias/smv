#include <assert.h>

#include "smv.h"

smv_replicator_t* replicators[16];

volatile bool* keep_running_ptr;
void handle_signal(int sig) {
    (void)sig;
    fprintf(stderr, "\nGot signal %d. Terminating\n", sig);
    if (keep_running_ptr) {
        *keep_running_ptr = false;
    }
}

int main(int argc, char** argv) {
    volatile bool keep_running = false;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);

    smv_receiver_t* receiver = smv_create_receiver();
    if (NULL == receiver) {
        exit(EXIT_FAILURE);
    }

    int num_replicators = 0;
    for (int ix = 1; ix < argc; ++ix) {
            char* mac = strdup(argv[ix]);
            char* ip = strchr(mac, ','); 
            if (strlen(mac) >= 17 && ip) {
                *ip = 0;
                ++ip;
                smv_if_addr_t ifaddr;
                if (6 == sscanf(argv[ix], "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
                            &ifaddr.mac_addr[0], &ifaddr.mac_addr[1], &ifaddr.mac_addr[2],
                            &ifaddr.mac_addr[3], &ifaddr.mac_addr[4], &ifaddr.mac_addr[5])
                        ) {
                    if (inet_pton(AF_INET, ip, &ifaddr.ipv4) == 1) {
                        if (smv_receiver_join(receiver, ifaddr)) {
                            replicators[num_replicators] = smv_create_replicator(ifaddr, receiver);
                            if (replicators[num_replicators]) {
                                ++num_replicators;
                                keep_running = true;
                                printf("Replicating player %s\n", argv[ix]);
                            }
                        } else {
                            fprintf(stderr, "receiver failed to join multicast group");
                        }
                    } else {
                        fprintf(stderr, "invalid IP address '%s'\n", ip);
                    }
                }
            } else {
                fprintf(stderr, "arguments must be in the format, '<mac address,IP address>' for example '01:23:45:67:98:ab,192.168.123.456'\n");
            }
    }
    keep_running_ptr = &keep_running;
    uint64_t* u64buff = calloc(16384 / sizeof(*u64buff), sizeof(*u64buff));
    uint8_t* buffer = (uint8_t*)u64buff;
    size_t buff_len = (16384 / sizeof(*u64buff)) * sizeof(*u64buff);

    while (keep_running) {
        smv_packet_t* pkt = smv_receive(receiver, buffer, buff_len);
        if (pkt) {
            for (int ix = 0; ix < num_replicators; ++ix) {
                if (smv_update_replicator(replicators[ix], pkt)) {
                    break;
                }
            }
#ifdef DEBUG
            fflush(stdout);
#endif
        } else {
            usleep(1000);
        }
    }
    for (int ix = 0; ix < num_replicators; ++ix) {
        smv_destroy_replicator(replicators + ix);
    }
}
