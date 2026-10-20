#include <assert.h>
#include <byteswap.h>

#include "smv.h"

// sender
// single threaded!
struct smv_sender_s {
    mac_address_t player_mac;
    uint16_t vbo;
    // total count of frames sent
    uint64_t total_frames_sent;
    // keep_alive_packet
    smv_packet_t keep_alive_packet;
    // silent audio packet
    smv_packet_t silent_packet;
    // socket descriptor
    int sock_fd;
    // multicast group
    struct sockaddr_in groupSock;
};

int platform_get_if_addresses(smv_if_addr_t* smv_ifaddrs, int size) {

    static mac_address_t zeromac = {0, 0, 0, 0, 0, 0};
    struct ifconf ifc;
    struct ifreq *ifr, *ifend;
    struct ifreq ifreq;
    struct ifreq ifs[size - 1];
    int count = 0;

    int s = socket(AF_INET, SOCK_DGRAM, 0);

    ifc.ifc_len = sizeof(ifs);
    ifc.ifc_req = ifs;

    if (ioctl(s, SIOCGIFCONF, &ifc) == 0) {
        ifend = ifs + (ifc.ifc_len / sizeof(struct ifreq));

        for (ifr = ifc.ifc_req; ifr < ifend; ifr++) {
            if (ifr->ifr_addr.sa_family == AF_INET) {
                strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));
                if (ioctl(s, SIOCGIFADDR, &ifreq) == 0) {
                    /*
                    uint32_t server_ip = ((struct sockaddr_in*)&ifreq.ifr_addr)->sin_addr.s_addr;
                    char str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(server_ip), str, INET_ADDRSTRLEN);
                    printf("%08x %s\n", server_ip, str);
                    */
                    smv_ifaddrs[count].ipv4 = ((struct sockaddr_in*)&ifreq.ifr_addr)->sin_addr.s_addr;
                }
                if (ioctl(s, SIOCGIFHWADDR, &ifreq) == 0) {
                    memcpy(smv_ifaddrs[count].mac_addr, ifreq.ifr_hwaddr.sa_data, 6);
                    if (memcmp(zeromac, smv_ifaddrs[count].mac_addr, sizeof(zeromac))) {
                        // skip lo interface
                        ++count;
                    } else {
                        smv_ifaddrs[count].ipv4 = 0;
                    }
                } else {
                    smv_ifaddrs[count].ipv4 = 0;
                }
            }
        }
    }
    close(s);
    return count;
}

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

smv_sender_t* smv_create_sender(smv_if_addr_t ifaddr, uint16_t version) {
    const char* interface_ip = INTERFACE_IP;
    struct in_addr localInterface;

    smv_sender_t* sender = calloc(1, sizeof(*sender));
    if (NULL == sender) {
        return NULL;
    }
    memset(sender, 0, sizeof(&sender));
    sender->sock_fd = -1;
    mac_address_cpy(sender->player_mac, ifaddr.mac_addr);
    sender->vbo = MAKE_VBO(version);
#ifdef DEBUG
    printf("sender vbo %hx network-order=%s version=%hd\n",
            sender->vbo,
            HOST_IS_NETWORK_ORDER ? "true" : "false",
            version);
#endif

    mac_address_cpy(sender->keep_alive_packet.player_mac, ifaddr.mac_addr);
    sender->keep_alive_packet.vbo = sender->vbo;
    sender->keep_alive_packet.pkt_id = SMV_PKT_KEEPALIVE;

    mac_address_cpy(sender->silent_packet.player_mac, ifaddr.mac_addr);
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
    sender->groupSock.sin_addr.s_addr &= 0x00ffffff;
    sender->groupSock.sin_addr.s_addr |= ifaddr.ipv4 &= 0xff000000;
    {
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender->groupSock.sin_addr.s_addr, str, INET_ADDRSTRLEN);
        printf("multicasting on %s\n", str);
    }
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
    sender->total_frames_sent += pkt->sample_count / 2;
    pkt->header.total_frames = sender->total_frames_sent;
    __smv_send_packet(sender, (smv_packet_t*)pkt);
}

// send keep alive packet
void smv_send_keep_alive(smv_sender_t* sender) {
    sender->keep_alive_packet.total_frames = sender->total_frames_sent;
    __smv_send_packet(sender, &sender->keep_alive_packet);
}

// send silent audio packet
void smv_send_silent(smv_sender_t* sender) {
    sender->silent_packet.total_frames = sender->total_frames_sent;
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

void smv_send_data(smv_sender_t* sender, int16_t* data, uint32_t sample_count) {
    smv_audio_mtu_packet_t pkt;
    // setup the invariant part of the audio packet
    mac_address_cpy(pkt.header.player_mac, sender->player_mac);
    pkt.header.vbo = sender->vbo;
    pkt.header.pkt_id = SMV_PKT_AUDIO_SAMPLES;

    while(sample_count) {
        uint32_t pkt_sample_count = sample_count > MTU_SAMPLE_COUNT ? MTU_SAMPLE_COUNT : sample_count;
        pkt.header.payload_len =  (pkt_sample_count * sizeof(*data)) + sizeof(smv_audio_packet_t) - sizeof(smv_packet_t);
        pkt.sample_count = pkt_sample_count;
        memcpy(pkt.samples, data, sizeof(pkt.samples[0]) * pkt_sample_count);
        smv_send_audio_packet(sender, (smv_audio_packet_t*)&pkt);
        // advance
        data += sample_count;
        sample_count -= pkt_sample_count;
    }
}
