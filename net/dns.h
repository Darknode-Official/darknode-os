#ifndef DNS_H
#define DNS_H
#include "../include/types.h"

#define DNS_PORT 53
#define DNS_CACHE_SIZE 8

#define DNS_FLAG_QR    0x8000
#define DNS_FLAG_RD    0x0100
#define DNS_FLAG_RA    0x0080

#define DNS_TYPE_A     1
#define DNS_CLASS_IN   1

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} PACKED dns_header_t;

void     dns_init(void);
void     dns_set_server(uint32_t server_ip);
uint32_t dns_resolve(const char *hostname);

#endif
