#include <assert.h>
#include <byteswap.h>

#include "smv.h"

// sender
// single threaded!
struct smv_sender_s {
    mac_address_t player_mac;
    uint16_t vbo;
    // total count of frames sent
    uint64_t total_frames;
    // keep_alive_packet
    smv_packet_t keep_alive_packet;
    // silent audio packet
    smv_packet_t silent_packet;
    // socket descriptor
    int sock_fd;
    // multicast group
    struct sockaddr_in groupSock;
};

void smv_destroy_sender(smv_sender_t** sender) {
    if (*sender) {
        if ((*sender)->sock_fd >= 0) {
            close((*sender)->sock_fd);
            (*sender)->sock_fd = -1;
        }
        free(*sender);
        *sender = NULL;
    }
}

smv_sender_t* smv_create_sender(mac_address_t player_mac, uint16_t version) {
    const char* interface_ip = INTERFACE_IP;
    struct in_addr localInterface;

    smv_sender_t* sender = calloc(1, sizeof(*sender));
    if (NULL == sender) {
        return NULL;
    }
    memset(sender, 0, sizeof(&sender));
    sender->sock_fd = -1;
    mac_address_cpy(sender->player_mac, player_mac);
    sender->vbo = MAKE_VBO(version);
#ifdef DEBUG
    printf("sender vbo %hx network-order=%s version=%hd\n",
            sender->vbo,
            HOST_IS_NETWORK_ORDER ? "true" : "false",
            version);
#endif

    mac_address_cpy(sender->keep_alive_packet.player_mac, player_mac);
    sender->keep_alive_packet.vbo = sender->vbo;
    sender->keep_alive_packet.pkt_id = SMV_PKT_KEEPALIVE;

    mac_address_cpy(sender->silent_packet.player_mac, player_mac);
    sender->silent_packet.vbo = sender->vbo;
    sender->silent_packet.pkt_id = SMV_PKT_AUDIO_SILENT;

    sender->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender->sock_fd < 0) {
        perror("Opening datagram socket error");
        smv_destroy_sender(&sender);
        return sender;
    }

    // Initialize the group sockaddr structure
    memset((char*)&sender->groupSock, 0, sizeof(sender->groupSock));
    sender->groupSock.sin_family = AF_INET;
    sender->groupSock.sin_addr.s_addr = inet_addr(SMV_GROUP1);
    sender->groupSock.sin_port = htons(SMV_MCAST_PORT);

    {
        // turn off multicast loopback to same host
        char loopch = 0;
        if (setsockopt(sender->sock_fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&loopch, sizeof(loopch)) < 0) {
            perror("Setting IP_MULTICAST_LOOP error");
            smv_destroy_sender(&sender);
            return sender;
        }
    }

    // multicast capable interface.
    localInterface.s_addr = inet_addr(interface_ip);
    if (setsockopt(sender->sock_fd, IPPROTO_IP, IP_MULTICAST_IF, (char*)&localInterface, sizeof(localInterface)) < 0) {
        perror("Setting local interface error");
        smv_destroy_sender(&sender);
        return sender;
    }
    return sender;
}

static void __smv_send_packet(smv_sender_t* sender, smv_packet_t* pkt) {
    if (sender->sock_fd < 0) {
        fprintf(stderr, "smv_send_packet on closed socket");
        return;
    }
    size_t datalen = sizeof(*pkt) + pkt->payload_len;
    if (sendto(sender->sock_fd, pkt, datalen, 0, (struct sockaddr*)&sender->groupSock, sizeof(sender->groupSock)) < 0) {
        perror("Sending datagram message error");
    }
}

// send audio packet
void smv_send_audio_packet(smv_sender_t* sender, smv_audio_packet_t* pkt) {
    sender->total_frames += pkt->sample_count / 2;
    pkt->header.total_frames = sender->total_frames;
    __smv_send_packet(sender, (smv_packet_t*)pkt);
}

// send keep alive packet
void smv_send_keep_alive(smv_sender_t* sender) {
    sender->keep_alive_packet.total_frames = sender->total_frames;
    __smv_send_packet(sender, &sender->keep_alive_packet);
}

// send silent audio packet
void smv_send_silent(smv_sender_t* sender) {
    sender->silent_packet.total_frames = sender->total_frames;
    __smv_send_packet(sender, &sender->silent_packet);
}

// create audio packet from a contiguous sample set
// allocates memory for the audio packet
// freeing this memory is the responsibility of the caller
smv_audio_packet_t* smv_create_audio_packet(smv_sender_t* sender, int16_t* samples, uint32_t sample_count) {
    smv_audio_packet_t* pkt = NULL;
    size_t size = sizeof(smv_audio_packet_t) + (sample_count * sizeof(pkt->samples[0]));
    pkt = calloc(1, size);
    if (pkt) {
        mac_address_cpy(pkt->header.player_mac, sender->player_mac);
        pkt->header.vbo = sender->vbo;
        pkt->header.pkt_id = SMV_PKT_AUDIO_SAMPLES;
        assert(size - sizeof(smv_packet_t) < 65535);
        pkt->header.payload_len = size - sizeof(smv_packet_t);
        pkt->sample_count = sample_count;
        memcpy(pkt->samples, samples, sizeof(pkt->samples[0]) * sample_count);
    }
    return pkt;
}

// create audio packet from 2 sample sets
// allocates memory for the audio packet
// freeing this memory is the responsibility of the caller
// This function simplifes handling of sample buffer wraparound
smv_audio_packet_t* smv_create_audio_packet_2(smv_sender_t* sender, int16_t* samples_1, uint32_t sample_count_1, int16_t* samples_2,
                                              uint32_t sample_count_2) {
    smv_audio_packet_t* pkt = NULL;
    size_t size = sizeof(smv_audio_packet_t) + ((sample_count_1 + sample_count_2) * sizeof(pkt->samples[0]));
    pkt = calloc(1, size);
    if (pkt) {
        mac_address_cpy(pkt->header.player_mac, sender->player_mac);
        pkt->header.vbo = sender->vbo;
        pkt->header.pkt_id = SMV_PKT_AUDIO_SAMPLES;
        assert(size - sizeof(smv_packet_t) < 65535);
        pkt->header.payload_len = size - sizeof(smv_packet_t);
        pkt->sample_count = sample_count_1 + sample_count_2;
        memcpy(pkt->samples, samples_1, sizeof(pkt->samples[0]) * sample_count_1);
        memcpy(pkt->samples + sample_count_1, samples_2, sizeof(pkt->samples[0]) * sample_count_2);
    }
    return pkt;
}

void smv_destroy_audio_packet(smv_sender_t* sender, smv_audio_packet_t** packet) {
    (void)sender;
    if (*packet) {
        free(*packet);
        *packet = NULL;
    }
}

// convenience function, sends audio packets specified,
// but replaces keep alive and silent packets with sender
// defined packets.
void smv_send_packet(smv_sender_t* sender, smv_packet_t* pkt) {
    if (sender->sock_fd < 0) {
        fprintf(stderr, "smv_send_packet on closed socket");
        return;
    }
    switch (pkt->pkt_id) {
        case SMV_PKT_KEEPALIVE:
            smv_send_keep_alive(sender);
            break;
        case SMV_PKT_AUDIO_SILENT:
            smv_send_silent(sender);
            break;
        case SMV_PKT_AUDIO_SAMPLES:
            smv_send_audio_packet(sender, (smv_audio_packet_t*)pkt);
            break;
    }
}
