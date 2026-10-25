#include <assert.h>

#include "smv.h"

// replicator
// single threaded!
static const vis_signature_t replica_sig = {.chars = {'r', 'e', 'p', 'l', 'i', 'c', 'a', '\0'}};

struct smv_replicator_s {
    int shm_fd;
    vis_ext_t* vis_ext;
    vis_t* vis_mmap;
    mac_address_t mac_addr;
#ifdef DEBUG
    bool vbo_printed;
#endif
};

void smv_destroy_replicator(smv_replicator_t** replicator_ptr) {
    if (replicator_ptr && *replicator_ptr) {
        smv_replicator_t* replicator = *replicator_ptr;
        replicator->vis_mmap = NULL;
        if (replicator->shm_fd > -1) {
            if (replicator->vis_ext != MAP_FAILED) {
                munmap(replicator->vis_ext, sizeof(*replicator->vis_ext));
                replicator->vis_ext = MAP_FAILED;
            }
            close(replicator->shm_fd);
            replicator->shm_fd = -1;
            char shm_path[32];
            snprintf(shm_path, sizeof(shm_path), "/squeezelite-%02x:%02x:%02x:%02x:%02x:%02x", replicator->mac_addr[0],
                     replicator->mac_addr[1], replicator->mac_addr[2], replicator->mac_addr[3], replicator->mac_addr[4],
                     replicator->mac_addr[5]);
            shm_unlink(shm_path);
            memset(replicator->mac_addr, 0, sizeof(replicator->mac_addr));
        }
        free(replicator);
        *replicator_ptr = NULL;
    }
}

// derived from squeezelite output_vis.c
smv_replicator_t* smv_create_replicator(mac_address_t mac_addr) {
    if (NULL == mac_addr) {
        return NULL;
    }

    smv_replicator_t* replicator = calloc(1, sizeof(*replicator));
    if (NULL == replicator) {
        return NULL;
    }

    replicator->shm_fd = -1;
    replicator->vis_ext = MAP_FAILED;
    mac_address_cpy(replicator->mac_addr, mac_addr);
    char shm_path[32];
    snprintf(shm_path, sizeof(shm_path), "/squeezelite-%02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);

    // TODO: if the shm exists, and is of size vis_ext_t, and has replicator signature
    // and process with owner_pid no longer exists, re-use and set this process as
    // owner.

    {
        mode_t old_mask = umask(000);  // allow any user to read our shm when created
        replicator->shm_fd = shm_open(shm_path, O_CREAT | O_EXCL | O_RDWR, 0666);
        // restore mask
        umask(old_mask);
    }

    if (replicator->shm_fd == -1) {
        fprintf(stderr, "Failed to open shared memory %s, error:%s\n", shm_path, strerror(errno));
        smv_destroy_replicator(&replicator);
        return NULL;
    }

    if (ftruncate(replicator->shm_fd, sizeof(*replicator->vis_ext)) == 0) {
        replicator->vis_ext = mmap(NULL, sizeof(*replicator->vis_ext), PROT_READ | PROT_WRITE, MAP_SHARED, replicator->shm_fd, 0);
    }

    if (replicator->vis_ext != MAP_FAILED) {
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        replicator->vis_mmap = &replicator->vis_ext->vis;
        pthread_rwlock_init(&replicator->vis_mmap->rwlock, &attr);
        replicator->vis_mmap->buf_size = VIS_BUF_SIZE;
        replicator->vis_mmap->running = false;
        replicator->vis_mmap->rate = 44100;
        pthread_rwlockattr_destroy(&attr);

        // vis_ext_t has additional fields
        // - signature
        replicator->vis_ext->sig.u64 = replica_sig.u64;
        // - owner process id
        replicator->vis_ext->owner_pid = getpid();
    }

    if (replicator->vis_ext == MAP_FAILED) {
        smv_destroy_replicator(&replicator);
    }

    return replicator;
}

static void _smv_update_replicator(smv_replicator_t* replicator, int16_t* samples, uint32_t sample_count) {
    if (replicator->vis_mmap) {
        pthread_rwlock_wrlock(&replicator->vis_mmap->rwlock);
        if (sample_count) {
            uint32_t avail = VIS_BUF_SIZE - replicator->vis_mmap->buf_index;
            if (avail < sample_count) {
#ifdef DEBUG
                printf("\n#%u, ", sample_count);
#endif
                memcpy(replicator->vis_mmap->buffer + replicator->vis_mmap->buf_index, samples, avail);
                replicator->vis_mmap->buf_index = 0;
                sample_count -= avail;
            } else {
#ifdef DEBUG
                printf("%u, ", sample_count);
#endif
            }
            memcpy(replicator->vis_mmap->buffer + replicator->vis_mmap->buf_index, samples, sample_count);
            replicator->vis_mmap->buf_index = (replicator->vis_mmap->buf_index + sample_count) % VIS_BUF_SIZE;
        }
        pthread_rwlock_unlock(&replicator->vis_mmap->rwlock);
    }
}

bool smv_update_replicator(smv_replicator_t* replicator, smv_packet_t* packet) {
    if (replicator->vis_mmap && 0 == memcmp(packet->player_mac, replicator->mac_addr, sizeof(replicator->mac_addr))) {
#ifdef DEBUG
    if (!replicator->vbo_printed) {
        printf("receiver vbo %hx network-order=%s version=%hd\n",
            packet->vbo,
            (packet->vbo & BYTEORDER_MASK) ? "true" : "false",
            (packet->vbo & VERSION_MASK)
            );
        replicator->vbo_printed = true;
    }
    switch (packet->pkt_id) {
        case SMV_PKT_KEEPALIVE:
            putchar('k');
            break;
        case SMV_PKT_AUDIO_SILENT:
            putchar('s');
            break;
        case SMV_PKT_AUDIO_SAMPLES:
            putchar('a');
            break;
        default:
            putchar('X');
            break;
    }
#endif
        replicator->vis_mmap->updated = time(NULL);
        switch (packet->pkt_id) {
            case SMV_PKT_KEEPALIVE:
                break;
            case SMV_PKT_AUDIO_SILENT:
                pthread_rwlock_wrlock(&replicator->vis_mmap->rwlock);
                replicator->vis_mmap->running = false;
                pthread_rwlock_unlock(&replicator->vis_mmap->rwlock);
                break;
            case SMV_PKT_AUDIO_SAMPLES: {
                replicator->vis_mmap->running = true;
                smv_audio_packet_t* ap = (smv_audio_packet_t*)packet;
                assert((uintptr_t)ap->samples % sizeof(ap->samples[0]) == 0);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
                _smv_update_replicator(replicator, ap->samples, ap->sample_count);
#pragma GCC diagnostic pop
            } break;
        }
    }
    return false;
}
