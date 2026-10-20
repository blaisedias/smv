#include <byteswap.h>

#include "smv.h"

struct smv_receiver_s {
    // socket descriptor
    int sock_fd;
};

// receiver
// single threaded!

static uint16_t smv_deserialise_packet(smv_packet_t* packet) {
    packet->vbo = ntohs(packet->vbo);
    bool pkt_bo = (packet->vbo & BYTEORDER_MASK) ? true : false;
    bool host_bo = HOST_IS_NETWORK_ORDER;

    if (pkt_bo != host_bo) {
#ifdef DEBUG
        putchar('~');
#endif
        packet->pkt_id = bswap_16(packet->pkt_id);
        packet->payload_len = bswap_16(packet->payload_len);
        packet->payload_crc = bswap_32(packet->payload_crc);
        packet->total_frames = bswap_64(packet->total_frames);
        switch (packet->pkt_id) {
            case SMV_PKT_KEEPALIVE:
                // nothing to do
                break;
            case SMV_PKT_AUDIO_SILENT:
                // nothing to do
                break;
            case SMV_PKT_AUDIO_SAMPLES: {
                smv_audio_packet_t* ap = (smv_audio_packet_t*)packet;
                ap->sample_count = bswap_16(ap->sample_count);
                for (uint32_t ix = 0; ix < (uint32_t)ap->sample_count * 2; ++ix) {
                    ap->samples[ix] = bswap_16(ap->samples[ix]);
                }
            } break;
        }
    }
    return packet->vbo & VERSION_MASK;
}

void smv_destroy_receiver(smv_receiver_t** receiver) {
    if (*receiver) {
        if ((*receiver)->sock_fd >= 0) {
            close((*receiver)->sock_fd);
            (*receiver)->sock_fd = -1;
        }
        free(*receiver);
        *receiver = NULL;
    }
}

smv_receiver_t* smv_create_receiver() {
//    const char* interface_ip = INTERFACE_IP;
    struct sockaddr_in localSock;
//    struct ip_mreq group;

    smv_receiver_t* receiver = calloc(1, sizeof(*receiver));
    if (NULL == receiver) {
        return NULL;
    }

    receiver->sock_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (receiver->sock_fd < 0) {
        perror("Failed to opend datagram socket");
        smv_destroy_receiver(&receiver);
        return receiver;
    }

    // Enable SO_REUSEADDR to allow multiple recipients  of the multicast datagrams.
    int reuse = 1;
    if (setsockopt(receiver->sock_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse)) < 0) {
        perror("Failed to set SO_REUSEADDR");
        smv_destroy_receiver(&receiver);
        return receiver;
    }

    // Bind to the port number with the IP address specified as INADDR_ANY.
    memset((char*)&localSock, 0, sizeof(localSock));
    localSock.sin_family = AF_INET;
    localSock.sin_port = htons(SMV_MCAST_PORT);
    localSock.sin_addr.s_addr = INADDR_ANY;
    if (bind(receiver->sock_fd, (struct sockaddr*)&localSock, sizeof(localSock))) {
        perror("Failed to bind socket");
        smv_destroy_receiver(&receiver);
        return receiver;
    }

//    // Join the multicast group
//    group.imr_multiaddr.s_addr = inet_addr(SMV_GROUP1);
//    group.imr_interface.s_addr = inet_addr(interface_ip);
//    if (setsockopt(receiver->sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&group, sizeof(group)) < 0) {
//        perror("Adding multicast group error");
//        smv_destroy_receiver(&receiver);
//        return receiver;
//    }
    return receiver;
}

bool smv_receiver_join(smv_receiver_t* receiver, smv_if_addr_t ifaddr) {
    const char* interface_ip = INTERFACE_IP;
    struct ip_mreq group;
    // Join the multicast group
    group.imr_interface.s_addr = inet_addr(interface_ip);
    group.imr_multiaddr.s_addr = inet_addr(SMV_GROUP1);
    group.imr_multiaddr.s_addr &= 0x00ffffff;
    group.imr_multiaddr.s_addr |= ifaddr.ipv4 &= 0xff000000;
    {
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &group.imr_multiaddr.s_addr, str, INET_ADDRSTRLEN);
        printf("multicast receive on %s\n", str);
    }

    if (setsockopt(receiver->sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&group, sizeof(group)) < 0) {
        return false;
    }

    return true;
}

// receiver sockets are set non blocking, so this call returns immediately
// if no data is available. Callers should sleep between calls when looping on
// calls to smv_receive
smv_packet_t* smv_receive(smv_receiver_t* receiver, uint8_t* buffer, size_t buffer_size) {
    if (receiver->sock_fd < 0) {
        fprintf(stderr, "smv_receive on closed socket\n");
        return NULL;
    }
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    ssize_t len = recvfrom(receiver->sock_fd, buffer, buffer_size, 0, (struct sockaddr*)&sender_addr, &sender_len);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return NULL;
        }
        perror("Receive datagram error");
        // FIXME: close the socket here?
        return NULL;
    }
    smv_packet_t* pkt = (smv_packet_t*)buffer;
    smv_deserialise_packet(pkt);
    return pkt;
}
