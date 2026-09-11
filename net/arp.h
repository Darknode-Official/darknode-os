#ifndef ARP_H
#define ARP_H
#include "../include/types.h"

#define ARP_HW_ETHERNET  1
#define ARP_PROTO_IPV4   0x0800
#define ARP_OP_REQUEST   1
#define ARP_OP_REPLY     2

#define ARP_CACHE_SIZE   16
#define ARP_ENTRY_TTL    300 /* seconds */

typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} PACKED arp_packet_t;

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    uint32_t timestamp;
    uint8_t  valid;
} arp_entry_t;

void     arp_init(void);
void     arp_receive(const void *data, uint16_t length);
int      arp_request(uint32_t target_ip);
uint8_t *arp_lookup(uint32_t ip);
void     arp_cache_dump(void);

#endif
