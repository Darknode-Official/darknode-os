#include "udp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../include/darknode.h"

typedef struct {
    uint16_t      port;
    udp_handler_t handler;
    uint8_t       active;
} udp_socket_t;

static udp_socket_t sockets[UDP_MAX_SOCKETS];

/* Pseudo-header for UDP checksum */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t udp_length;
} PACKED udp_pseudo_t;

void udp_init(void) {
    memset(sockets, 0, sizeof(sockets));
}

int udp_bind(uint16_t port, udp_handler_t handler) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (sockets[i].active && sockets[i].port == port)
            return -1; /* already bound */
    }
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!sockets[i].active) {
            sockets[i].port    = port;
            sockets[i].handler = handler;
            sockets[i].active  = 1;
            return 0;
        }
    }
    return -1; /* no slots */
}

void udp_unbind(uint16_t port) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (sockets[i].active && sockets[i].port == port) {
            sockets[i].active = 0;
            return;
        }
    }
}

int udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
             const void *data, uint16_t length)
{
    uint16_t total = UDP_HEADER_LEN + length;
    uint8_t buf[1480];
    if (total > sizeof(buf)) return -1;

    udp_header_t *hdr = (udp_header_t *)buf;
    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->length   = htons(total);
    hdr->checksum = 0;

    memcpy(buf + UDP_HEADER_LEN, data, length);

    /* Compute UDP checksum with pseudo-header */
    udp_pseudo_t pseudo;
    pseudo.src_ip     = ipv4_get_ip();
    pseudo.dst_ip     = dst_ip;
    pseudo.zero       = 0;
    pseudo.protocol   = IP_PROTO_UDP;
    pseudo.udp_length = htons(total);

    uint32_t sum = 0;
    const uint16_t *p = (const uint16_t *)&pseudo;
    for (int i = 0; i < 6; i++) sum += p[i];
    p = (const uint16_t *)buf;
    uint16_t len = total;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    hdr->checksum = (uint16_t)~sum;
    if (hdr->checksum == 0) hdr->checksum = 0xFFFF;

    return ipv4_send(dst_ip, IP_PROTO_UDP, buf, total);
}

void udp_receive(uint32_t src_ip, const void *data, uint16_t length) {
    if (length < UDP_HEADER_LEN) return;

    const udp_header_t *hdr = (const udp_header_t *)data;
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t udp_len  = ntohs(hdr->length);
    if (udp_len > length) return;

    const uint8_t *payload = (const uint8_t *)data + UDP_HEADER_LEN;
    uint16_t payload_len = udp_len - UDP_HEADER_LEN;

    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (sockets[i].active && sockets[i].port == dst_port) {
            sockets[i].handler(src_ip, src_port, payload, payload_len);
            return;
        }
    }
}

void udp_dump_sockets(void) {
    console_write_color("UDP Sockets:\n", DN_COLOR_HEADER);
    int count = 0;
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (sockets[i].active) {
            count++;
            console_write("  port ");
            console_write_dec(sockets[i].port);
            console_write_color("  LISTEN\n", DN_COLOR_OK);
        }
    }
    if (!count) console_write_color("  (none)\n", DN_COLOR_DIM);
}
