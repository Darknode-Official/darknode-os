#include "icmp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../kernel/timer.h"
#include "../include/darknode.h"

static volatile int     ping_replied = 0;
static volatile uint32_t ping_rtt    = 0;
static uint16_t ping_ident = 0x4E44; /* "DN" */
static uint16_t ping_seq   = 0;

void icmp_init(void) {
    ping_seq = 0;
}

void icmp_receive(uint32_t src_ip, const void *data, uint16_t length) {
    if (length < sizeof(icmp_header_t)) return;

    const icmp_header_t *hdr = (const icmp_header_t *)data;

    if (hdr->type == ICMP_ECHO_REQUEST) {
        /* Send echo reply */
        uint8_t reply_buf[1500];
        icmp_header_t *reply = (icmp_header_t *)reply_buf;
        reply->type     = ICMP_ECHO_REPLY;
        reply->code     = 0;
        reply->checksum = 0;
        reply->ident    = hdr->ident;
        reply->sequence = hdr->sequence;
        /* Copy any payload data after the header */
        uint16_t payload_len = length - sizeof(icmp_header_t);
        if (payload_len > 0 && payload_len < 1492)
            memcpy(reply_buf + sizeof(icmp_header_t),
                   (const uint8_t *)data + sizeof(icmp_header_t), payload_len);
        reply->checksum = ip_checksum(reply_buf, sizeof(icmp_header_t) + payload_len);
        ipv4_send(src_ip, IP_PROTO_ICMP, reply_buf, sizeof(icmp_header_t) + payload_len);
    }
    else if (hdr->type == ICMP_ECHO_REPLY) {
        if (ntohs(hdr->ident) == ping_ident) {
            ping_replied = 1;
            ping_rtt = timer_get_ticks();
        }
    }
}

int icmp_ping(uint32_t dst_ip, int count) {
    char ipbuf[20];
    ip_format(dst_ip, ipbuf);
    console_write_color("PING ", DN_COLOR_ACCENT);
    console_write(ipbuf);
    console_write(" — ");
    console_write_dec(count);
    console_write(" packets\n");

    int received = 0;

    for (int i = 0; i < count; i++) {
        /* Build echo request */
        uint8_t pkt[64];
        icmp_header_t *req = (icmp_header_t *)pkt;
        req->type     = ICMP_ECHO_REQUEST;
        req->code     = 0;
        req->checksum = 0;
        req->ident    = htons(ping_ident);
        req->sequence = htons(++ping_seq);
        /* 56 bytes of payload (total 64 with header) */
        memset(pkt + sizeof(icmp_header_t), 0xDA, 56);
        for (int j = 0; j < 56; j++)
            pkt[sizeof(icmp_header_t) + j] = (uint8_t)j;
        req->checksum = ip_checksum(pkt, 64);

        ping_replied = 0;
        uint32_t send_time = timer_get_ticks();

        if (ipv4_send(dst_ip, IP_PROTO_ICMP, pkt, 64) < 0) {
            console_write_color("  send failed\n", DN_COLOR_ERR);
            continue;
        }

        /* Wait for reply — poll NIC for up to 3 seconds */
        int got_reply = 0;
        while (timer_get_ticks() - send_time < 3000) {
            uint8_t rxbuf[1536];
            int len;
            extern int ne2000_receive(void *buf, uint16_t max_len);
            extern void ne2000_poll(void);
            ne2000_poll();
            while ((len = ne2000_receive(rxbuf, sizeof(rxbuf))) > 0)
                eth_receive(rxbuf, len);
            if (ping_replied) {
                got_reply = 1;
                break;
            }
        }

        if (got_reply) {
            uint32_t rtt = timer_get_ticks() - send_time;
            received++;
            console_write("  reply from ");
            console_write(ipbuf);
            console_write(" seq=");
            console_write_dec(ping_seq);
            console_write(" time=");
            console_write_dec(rtt);
            console_write("ms\n");
        } else {
            console_write_color("  request timed out\n", DN_COLOR_WARN);
        }

        if (i < count - 1) timer_wait(1000);
    }

    console_write("--- ");
    console_write(ipbuf);
    console_write(" ping statistics ---\n");
    console_write_dec(count);
    console_write(" sent, ");
    console_write_dec(received);
    console_write(" received, ");
    console_write_dec(((count - received) * 100) / count);
    console_write("% loss\n");

    return received;
}
