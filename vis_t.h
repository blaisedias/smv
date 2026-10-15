#ifndef __vis_t_h
#define __vis_t_h

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define VIS_BUF_SIZE    16384

typedef struct {
    pthread_rwlock_t    rwlock;
    uint32_t            buf_size;
    uint32_t            buf_index;
    bool                running;
    uint32_t            rate;
    time_t              updated;
    int16_t             buffer[VIS_BUF_SIZE];
} vis_t;
#endif // __vis_t_h
