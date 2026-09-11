#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "../kernel/string.h"
#include "../kernel/console.h"

static uint32_t our_ip      = 0;
static uint32_t our_subnet  = 0;
static uint32_t our_gateway = 0;
static uint16_t ip_ident    = 1;

void ipv4_init(void) {
    our_ip     = 0;
    our_subnet = 0;
    our_gateway = 0;
    ip_ident   = 1;
}

void ipv4_config(uint32_t ip, uint32_t subnet, uint32_t gateway) {
    our_ip      = ip;
    our_subnet  = subnet;
    our_gateway = gateway;
}

uint32_t ipv4_get_ip(void)      { return our_ip; }
uint32_t ipv4_get_subnet(void)  { return our_subnet; }
uint32_t ipv4_get_gateway(void) { return our_gateway; }

uint32_t ip_make(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

void ip_format(uint32_t ip, char *buf) {
    uint8_t *b = (uint8_t *)&ip;
    char tmp[4];
    buf[0] = '\0';
    for (int i = 0; i < 4; i++) {
        utoa(b[i], tmp, 10);
        strcat(buf, tmp);
        if (i < 3) strcat(buf, ".");
    }
}

uint16_t ip_checksum(const void *data, uint16_t length) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;
    while (length > 1) {
        sum += *ptr++;
        length -= 2;
    }
    if (length) sum += *(const uint8_t *)ptr;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

int ipv4_send(uint32_t dst_ip, uint8_t protocol, const void *payload, uint16_t length) {
    if (!our_ip) return -1;

    uint8_t packet[ETH_MTU];
    ipv4_header_t *hdr = (ipv4_header_t *)packet;

    hdr->ver_ihl   = 0x45; /* IPv4, IHL=5 (20 bytes) */
    hdr->dscp_ecn  = 0;
    hdr->total_len = htons(IP_HEADER_LEN + length);
    hdr->ident     = htons(ip_ident++);
    hdr->flags_frag = htons(0x4000); /* Don't Fragment */
    hdr->ttl       = IP_TTL_DEFAULT;
    hdr->protocol  = protocol;
    hdr->checksum  = 0;
    hdr->src_ip    = our_ip;
    hdr->dst_ip    = dst_ip;
    hdr->checksum  = ip_checksum(hdr, IP_HEADER_LEN);

    if (length > ETH_MTU - IP_HEADER_LEN) return -1;
    memcpy(packet + IP_HEADER_LEN, payload, length);

    /* Determine next-hop: same subnet → direct, otherwise → gateway */
    uint32_t next_hop = dst_ip;
    if ((dst_ip & our_subnet) != (our_ip & our_subnet))
        next_hop = our_gateway;

    /* Broadcast goes to broadcast MAC */
    if (dst_ip == 0xFFFFFFFF)
        return eth_send(ETH_BROADCAST, ETH_TYPE_IPV4, packet, IP_HEADER_LEN + length);

    /* Resolve MAC via ARP */
    uint8_t *dst_mac = arp_lookup(next_hop);
    if (!dst_mac) return -1;

    return eth_send(dst_mac, ETH_TYPE_IPV4, packet, IP_HEADER_LEN + length);
}

void ipv4_receive(const void *data, uint16_t length) {
    if (length < IP_HEADER_LEN) return;

    const ipv4_header_t *hdr = (const ipv4_header_t *)data;
    if ((hdr->ver_ihl >> 4) != 4) return; /* not IPv4 */

    uint16_t ihl = (hdr->ver_ihl & 0x0F) * 4;
    uint16_t total = ntohs(hdr->total_len);
    if (total > length) return;

    /* Verify checksum */
    if (ip_checksum(data, ihl) != 0) return;

    /* Only accept packets for us or broadcast */
    if (hdr->dst_ip != our_ip && hdr->dst_ip != 0xFFFFFFFF)
        return;

    const uint8_t *payload = (const uint8_t *)data + ihl;
    uint16_t payload_len = total - ihl;

    switch (hdr->protocol) {
    case IP_PROTO_ICMP:
        icmp_receive(hdr->src_ip, payload, payload_len);
        break;
    case IP_PROTO_UDP:
        udp_receive(hdr->src_ip, payload, payload_len);
        break;
    default:
        break;
    }
}
