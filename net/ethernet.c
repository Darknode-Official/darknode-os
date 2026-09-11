#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "../drivers/ne2000.h"
#include "../kernel/string.h"
#include "../kernel/console.h"

const uint8_t ETH_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t our_mac[6];

uint16_t htons(uint16_t val) {
    return (val >> 8) | (val << 8);
}

uint16_t ntohs(uint16_t val) {
    return (val >> 8) | (val << 8);
}

uint32_t htonl(uint32_t val) {
    return ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
           ((val << 8) & 0xFF0000) | ((val << 24) & 0xFF000000);
}

uint32_t ntohl(uint32_t val) {
    return htonl(val);
}

void eth_init(void) {
    ne2000_get_mac(our_mac);
}

void eth_get_mac(uint8_t mac[6]) {
    memcpy(mac, our_mac, 6);
}

int eth_mac_eq(const uint8_t a[6], const uint8_t b[6]) {
    return memcmp(a, b, 6) == 0;
}

int eth_is_broadcast(const uint8_t mac[6]) {
    return eth_mac_eq(mac, ETH_BROADCAST);
}

int eth_send(const uint8_t dst[6], uint16_t ethertype, const void *payload, uint16_t length) {
    if (length > ETH_MTU) return -1;

    uint8_t frame[ETH_FRAME_MAX];
    eth_header_t *hdr = (eth_header_t *)frame;

    memcpy(hdr->dst, dst, 6);
    memcpy(hdr->src, our_mac, 6);
    hdr->ethertype = htons(ethertype);
    memcpy(frame + ETH_HEADER_LEN, payload, length);

    uint16_t total = ETH_HEADER_LEN + length;
    if (total < 60) total = 60; /* pad to minimum */

    return ne2000_send(frame, total);
}

void eth_receive(const void *frame, uint16_t length) {
    if (length < ETH_HEADER_LEN) return;

    const eth_header_t *hdr = (const eth_header_t *)frame;
    const uint8_t *payload = (const uint8_t *)frame + ETH_HEADER_LEN;
    uint16_t payload_len = length - ETH_HEADER_LEN;
    uint16_t type = ntohs(hdr->ethertype);

    /* Only accept frames destined for us or broadcast */
    if (!eth_mac_eq(hdr->dst, our_mac) && !eth_is_broadcast(hdr->dst))
        return;

    switch (type) {
    case ETH_TYPE_ARP:
        arp_receive(payload, payload_len);
        break;
    case ETH_TYPE_IPV4:
        ipv4_receive(payload, payload_len);
        break;
    default:
        break;
    }
}
