#include <assert.h>

#include "smv.h"


int main() {
    printf("sizeof(vis_t) %ld\n", sizeof(vis_t));
    printf("sizeof(vis_ext_t) %ld\n", sizeof(vis_ext_t));
    printf("sizeof(mac_address_t) %ld\n", sizeof(mac_address_t));


    printf("sizeof(smv_packet_t) %ld\n", sizeof(smv_packet_t));
    printf("sizeof(smv_audio_packet_t) %ld\n", sizeof(smv_audio_packet_t));
    printf("sizeof(smv_audio_mtu_packet_t) %ld\n", sizeof(smv_audio_mtu_packet_t));

    return 0;
}
