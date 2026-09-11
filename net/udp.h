#ifndef UDP_H
#define UDP_H
#include "../include/types.h"

#define UDP_HEADER_LEN 8
#define UDP_MAX_SOCKETS 8

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} PACKED udp_header_t;

typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port,
                               const void *data, uint16_t length);

void udp_init(void);
int  udp_bind(uint16_t port, udp_handler_t handler);
void udp_unbind(uint16_t port);
int  udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
              const void *data, uint16_t length);
void udp_receive(uint32_t src_ip, const void *data, uint16_t length);
void udp_dump_sockets(void);

#endif
