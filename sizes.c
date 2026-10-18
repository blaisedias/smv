#include <assert.h>

#include "smv.h"


int main() {
    vis_t vis;
    printf("sizeof(vis_t)           %u\n", (unsigned)sizeof(vis_t));
    printf("sizeof(vis_ext_t)       %u\n", (unsigned)sizeof(vis_ext_t));
    printf("sizeof(vis_t.rwlock)    %u\n", (unsigned)sizeof(vis.rwlock));
    printf("sizeof(vis_t.buf_size)  %u\n", (unsigned)sizeof(vis.buf_size));
    printf("sizeof(vis_t.buf_index) %u\n", (unsigned)sizeof(vis.buf_index));
    printf("sizeof(vis_t.running)   %u\n", (unsigned)sizeof(vis.running));
    printf("sizeof(vis_t.rate)      %u\n", (unsigned)sizeof(vis.rate));
    printf("sizeof(vis_t.updated)   %u\n", (unsigned)sizeof(vis.updated));
    printf("sizeof(vis_t.buffer)    %u\n", (unsigned)sizeof(vis.buffer));


    printf("---------------------\n");
    printf("sizeof(mac_address_t)           %u\n", (unsigned)sizeof(mac_address_t));
    printf("sizeof(smv_packet_t)            %u\n", (unsigned)sizeof(smv_packet_t));
    printf("sizeof(smv_audio_packet_t)      %u\n", (unsigned)sizeof(smv_audio_packet_t));
    printf("sizeof(smv_audio_mtu_packet_t)  %u\n", (unsigned)sizeof(smv_audio_mtu_packet_t));

    return 0;
}
