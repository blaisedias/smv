#ifndef __smv_h_
#define __smv_h_

#include <arpa/inet.h>
// #include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <linux/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/sem.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "vis_t.h"
#define VIS_BUF_FRAMES VIS_BUF_SIZE / 2
typedef union {
    uint64_t u64;
    char chars[sizeof(uint64_t)];
} vis_signature_t;

typedef struct {
    vis_t vis;
    vis_signature_t sig;
    pid_t owner_pid;
} vis_ext_t;

// version is an unsigned integer in range 0-32768
// this is considered ample for out purposes
#define SMV_VERSION 1
// 16 bit MSB
#define BYTEORDER_MASK 0x8000
// complement of 16 bit MSB
#define VERSION_MASK (~BYTEORDER_MASK)
#define HOST_IS_NETWORK_ORDER (bool)(htons(1) & 0x1)
#define MAKE_VBO(version) (htons((HOST_IS_NETWORK_ORDER << 15) + (version)))

enum {
    SMV_PKT_KEEPALIVE = 0,
    SMV_PKT_AUDIO_SILENT,
    SMV_PKT_AUDIO_SAMPLES,
};

typedef uint8_t mac_address_t[6];

typedef struct {
    uint32_t        ipv4;
    mac_address_t   mac_addr;
}smv_if_addr_t;

// Serialisation convention:
// vbo is the only member which is in network order
// all other fields are in sender byte order
// Rationale:
//  reduce endian conversion operations required to a maximum of 1 in all scenarios.
//  if sender host and receiver host have the same byte order then no conversion is required.
//  if not then destination does the conversion.
//      This lowers the demands on senders which
//          - are producing the audio
//          - may be resource constricted devices.
//      Receivers are typically more powerful and are visually rendering audio data
//      an operation which will not affect audio quality.
//      it is expected that conversion operations use bswap_16, bswap_32 and bswap_64
//      removing the need to create 64 bit ntohl functions.
typedef struct {
    // player MAC address
    mac_address_t player_mac;
    // version and byte order bit encoded in network order
    // sender use htons when writing and receivers use ntohs when reading
    uint16_t vbo;
    // type of packet
    uint16_t pkt_id;
    // size packet excluding this header
    uint16_t payload_len;
    // CRC for data excluding this header
    uint32_t payload_crc;
    // For consistency with squeezelite, use the term frames
    // to denote 2 16 bit samples.
    // The total number of frames sent on this stream
    // including the frames in the current packet
    // This is a 64 bit value, so size of preceding fields
    // should be a multiple of 8
    uint64_t total_frames;
} __attribute__((packed)) smv_packet_t;

typedef struct {
    smv_packet_t header;
    // number of samples
    uint16_t sample_count;
    // 16 bit samples
    int16_t samples[0];
} __attribute__((packed)) smv_audio_packet_t;

#define MTU_STEREO_SAMPLE_COUNT     360
#define MTU_SAMPLE_COUNT            MTU_STEREO_SAMPLE_COUNT*2
typedef struct {
    smv_packet_t header;
    // number of samples
    uint16_t sample_count;
    // 16 bit samples
    int16_t samples[MTU_SAMPLE_COUNT];
} __attribute__((packed)) smv_audio_mtu_packet_t;

// 239.0.0.0-239.255.255.255 	Organization-Local Scope 	[David_Meyer][RFC 2365]
// The IPv4 Local Scope -- 239.255.0.0/16
// The IPv4 Organization Local Scope -- 239.192.0.0/14
#define SMV_GROUP1 "239.255.83.67"  // First multicast group
#define SMV_GROUP2 "239.255.83.69"  // Second multicast group
#define INTERFACE_IP "0.0.0.0"      // Join on all interfaces (INADDR_ANY)
#define SMV_MCAST_PORT 30305

typedef struct smv_sender_s smv_sender_t;
typedef struct smv_receiver_s smv_receiver_t;
typedef struct smv_replicator_s smv_replicator_t;

//----------------------------------------------------------------------------
// sender interface
void smv_destroy_sender(smv_sender_t** sender);
smv_sender_t* smv_create_sender(smv_if_addr_t ifaddr, uint16_t version);

void smv_send_audio_packet(smv_sender_t* sender, smv_audio_packet_t* pkt);
void smv_send_keep_alive(smv_sender_t* sender);
void smv_send_silent(smv_sender_t* sender);
void smv_send_packet(smv_sender_t* sender, smv_packet_t* pkt);

// audio packets are dynamically allocated
smv_audio_packet_t* smv_create_audio_packet(smv_sender_t* sender, int16_t* samples, uint32_t sample_count);
smv_audio_packet_t* smv_create_audio_packet_2(smv_sender_t* sender, int16_t* samples_1, uint32_t sample_count_1, int16_t* samples_2,
                                              uint32_t sample_count_2);
void smv_destroy_audio_packet(smv_sender_t* sender, smv_audio_packet_t** packet);
void smv_send_data(smv_sender_t*, int16_t* data, uint32_t sample_count);
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// receiver interface
void smv_destroy_receiver(smv_receiver_t** receiver);
smv_receiver_t* smv_create_receiver();
bool smv_receiver_join(smv_receiver_t* receiver, smv_if_addr_t ifaddr);
// receiver sockets are set non blocking, so this call returns immediately
// if no data is available. Callers should sleep between calls when looping on
// calls to smv_receive
smv_packet_t* smv_receive(smv_receiver_t* receiver, uint8_t* buffer, size_t buffer_size);
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// replicator interface
void smv_destroy_replicator(smv_replicator_t** replicator_ptr);
smv_replicator_t* smv_create_replicator(smv_if_addr_t ifaddr);
bool smv_update_replicator(smv_replicator_t* replicator, smv_packet_t* packet);

static inline void mac_address_cpy(mac_address_t dest, mac_address_t src) {
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
    dest[3] = src[3];
    dest[4] = src[4];
    dest[5] = src[5];
}
#endif  // __smv_h_
