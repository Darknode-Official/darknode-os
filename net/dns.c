#include "dns.h"
#include "udp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../kernel/timer.h"
#include "../drivers/ne2000.h"

static uint32_t dns_server_ip = 0;
static uint16_t dns_next_id   = 0x4400;

/* Simple DNS cache */
typedef struct {
    char     hostname[64];
    uint32_t ip;
    uint32_t timestamp;
    uint8_t  valid;
} dns_cache_entry_t;

static dns_cache_entry_t dns_cache[DNS_CACHE_SIZE];

/* Response state */
static volatile int      dns_got_reply = 0;
static volatile uint32_t dns_result_ip = 0;
static uint16_t dns_current_id = 0;

void dns_init(void) {
    memset(dns_cache, 0, sizeof(dns_cache));
}

void dns_set_server(uint32_t ip) {
    dns_server_ip = ip;
}

/* Encode a hostname into DNS wire format: "example.com" → "\x07example\x03com\x00" */
static int dns_encode_name(const char *name, uint8_t *buf, int max_len) {
    int pos = 0;
    const char *p = name;
    while (*p && pos < max_len - 2) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int len = dot - p;
        if (len == 0 || len > 63) return -1;
        buf[pos++] = (uint8_t)len;
        for (int i = 0; i < len && pos < max_len - 1; i++)
            buf[pos++] = p[i];
        p = *dot ? dot + 1 : dot;
    }
    buf[pos++] = 0; /* root label */
    return pos;
}

/* Skip a DNS name (handles compression pointers) */
static int dns_skip_name(const uint8_t *buf, int offset, int buf_len) {
    int pos = offset;
    while (pos < buf_len) {
        uint8_t len = buf[pos];
        if (len == 0) { pos++; break; }
        if ((len & 0xC0) == 0xC0) { pos += 2; break; } /* compression pointer */
        pos += 1 + len;
    }
    return pos;
}

static void dns_response_handler(uint32_t src_ip, uint16_t src_port,
                                  const void *data, uint16_t length)
{
    (void)src_ip; (void)src_port;
    if (length < sizeof(dns_header_t)) return;

    const dns_header_t *hdr = (const dns_header_t *)data;
    if (ntohs(hdr->id) != dns_current_id) return;
    if (!(ntohs(hdr->flags) & DNS_FLAG_QR)) return; /* not a response */

    uint16_t ancount = ntohs(hdr->ancount);
    if (ancount == 0) return;

    /* Skip the question section */
    const uint8_t *buf = (const uint8_t *)data;
    int pos = sizeof(dns_header_t);
    uint16_t qdcount = ntohs(hdr->qdcount);
    for (uint16_t i = 0; i < qdcount && pos < length; i++) {
        pos = dns_skip_name(buf, pos, length);
        pos += 4; /* QTYPE + QCLASS */
    }

    /* Parse answers — look for an A record */
    for (uint16_t i = 0; i < ancount && pos < length; i++) {
        pos = dns_skip_name(buf, pos, length);
        if (pos + 10 > length) break;
        uint16_t rtype  = (buf[pos] << 8) | buf[pos + 1];
        uint16_t rdlen  = (buf[pos + 8] << 8) | buf[pos + 9];
        pos += 10;
        if (rtype == DNS_TYPE_A && rdlen == 4 && pos + 4 <= length) {
            dns_result_ip = *(uint32_t *)&buf[pos];
            dns_got_reply = 1;
            return;
        }
        pos += rdlen;
    }
}

static void dns_cache_add(const char *hostname, uint32_t ip) {
    /* Update existing */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].hostname, hostname) == 0) {
            dns_cache[i].ip = ip;
            dns_cache[i].timestamp = timer_get_ticks() / 1000;
            return;
        }
    }
    /* Find free or oldest slot */
    int slot = 0;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) { slot = i; break; }
        if (dns_cache[i].timestamp < oldest) { oldest = dns_cache[i].timestamp; slot = i; }
    }
    strncpy(dns_cache[slot].hostname, hostname, 63);
    dns_cache[slot].hostname[63] = '\0';
    dns_cache[slot].ip = ip;
    dns_cache[slot].timestamp = timer_get_ticks() / 1000;
    dns_cache[slot].valid = 1;
}

uint32_t dns_resolve(const char *hostname) {
    if (!dns_server_ip) return 0;

    /* Check cache first */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].hostname, hostname) == 0)
            return dns_cache[i].ip;
    }

    /* Build DNS query */
    uint8_t pkt[512];
    memset(pkt, 0, sizeof(pkt));
    dns_header_t *hdr = (dns_header_t *)pkt;
    dns_current_id = dns_next_id++;
    hdr->id      = htons(dns_current_id);
    hdr->flags   = htons(DNS_FLAG_RD); /* recursion desired */
    hdr->qdcount = htons(1);

    int pos = sizeof(dns_header_t);
    int name_len = dns_encode_name(hostname, pkt + pos, sizeof(pkt) - pos - 4);
    if (name_len < 0) return 0;
    pos += name_len;

    /* QTYPE = A (1), QCLASS = IN (1) */
    pkt[pos++] = 0; pkt[pos++] = DNS_TYPE_A;
    pkt[pos++] = 0; pkt[pos++] = DNS_CLASS_IN;

    /* Bind a listener and send */
    dns_got_reply = 0;
    dns_result_ip = 0;
    uint16_t src_port = 10053 + (dns_next_id & 0xFF);
    udp_bind(src_port, dns_response_handler);
    udp_send(dns_server_ip, DNS_PORT, src_port, pkt, pos);

    /* Wait for response (up to 3 seconds) */
    uint32_t start = timer_get_ticks();
    while (!dns_got_reply && (timer_get_ticks() - start) < 3000) {
        uint8_t rxbuf[1536];
        int len;
        ne2000_poll();
        while ((len = ne2000_receive(rxbuf, sizeof(rxbuf))) > 0)
            eth_receive(rxbuf, len);
    }

    udp_unbind(src_port);

    if (dns_got_reply) {
        dns_cache_add(hostname, dns_result_ip);
        return dns_result_ip;
    }
    return 0;
}
