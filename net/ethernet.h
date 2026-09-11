#ifndef ETHERNET_H
#define ETHERNET_H
#include "../include/types.h"

#define ETH_TYPE_ARP   0x0806
#define ETH_TYPE_IPV4  0x0800
#define ETH_TYPE_IPV6  0x86DD

#define ETH_HEADER_LEN 14
#define ETH_MTU        1500
#define ETH_FRAME_MAX  1514

typedef struct {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} PACKED eth_header_t;

void    eth_init(void);
int     eth_send(const uint8_t dst[6], uint16_t ethertype, const void *payload, uint16_t length);
void    eth_receive(const void *frame, uint16_t length);
void    eth_get_mac(uint8_t mac[6]);
int     eth_mac_eq(const uint8_t a[6], const uint8_t b[6]);
int     eth_is_broadcast(const uint8_t mac[6]);
uint16_t htons(uint16_t val);
uint16_t ntohs(uint16_t val);
uint32_t htonl(uint32_t val);
uint32_t ntohl(uint32_t val);

extern const uint8_t ETH_BROADCAST[6];

#endif
