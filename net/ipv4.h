#ifndef IPV4_H
#define IPV4_H
#include "../include/types.h"

#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17

#define IP_HEADER_LEN  20
#define IP_TTL_DEFAULT 64

typedef struct {
    uint8_t  ver_ihl;     /* version (4) + IHL (4) */
    uint8_t  dscp_ecn;    /* DSCP (6) + ECN (2) */
    uint16_t total_len;
    uint16_t ident;
    uint16_t flags_frag;  /* flags (3) + fragment offset (13) */
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} PACKED ipv4_header_t;

void     ipv4_init(void);
void     ipv4_config(uint32_t ip, uint32_t subnet, uint32_t gateway);
uint32_t ipv4_get_ip(void);
uint32_t ipv4_get_subnet(void);
uint32_t ipv4_get_gateway(void);
int      ipv4_send(uint32_t dst_ip, uint8_t protocol, const void *payload, uint16_t length);
void     ipv4_receive(const void *data, uint16_t length);
uint16_t ip_checksum(const void *data, uint16_t length);
uint32_t ip_make(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
void     ip_format(uint32_t ip, char *buf);

#endif
