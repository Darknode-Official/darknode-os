#ifndef ICMP_H
#define ICMP_H
#include "../include/types.h"

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8
#define ICMP_DEST_UNREACH 3
#define ICMP_TIME_EXCEEDED 11

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t sequence;
} PACKED icmp_header_t;

void icmp_init(void);
void icmp_receive(uint32_t src_ip, const void *data, uint16_t length);
int  icmp_ping(uint32_t dst_ip, int count);

#endif
