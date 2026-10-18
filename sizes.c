#include <assert.h>

#include "smv.h"


int main() {
    printf("sizeof(vis_t) %u\n", (unsigned)sizeof(vis_t));
    printf("sizeof(vis_ext_t) %u\n", (unsigned)sizeof(vis_ext_t));
    printf("sizeof(mac_address_t) %u\n", (unsigned)sizeof(mac_address_t));


    printf("sizeof(smv_packet_t) %u\n", (unsigned)sizeof(smv_packet_t));
    printf("sizeof(smv_audio_packet_t) %u\n", (unsigned)sizeof(smv_audio_packet_t));
    printf("sizeof(smv_audio_mtu_packet_t) %u\n", (unsigned)sizeof(smv_audio_mtu_packet_t));

    return 0;
}
