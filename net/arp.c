#include "arp.h"
#include "ethernet.h"
#include "ipv4.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../kernel/timer.h"
#include "../include/darknode.h"

static arp_entry_t cache[ARP_CACHE_SIZE];

void arp_init(void) {
    memset(cache, 0, sizeof(cache));
}

static void arp_cache_add(uint32_t ip, const uint8_t mac[6]) {
    /* Update existing entry */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].ip == ip) {
            memcpy(cache[i].mac, mac, 6);
            cache[i].timestamp = timer_get_ticks() / 1000;
            return;
        }
    }
    /* Find empty or oldest slot */
    int oldest = 0;
    uint32_t oldest_ts = 0xFFFFFFFF;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!cache[i].valid) { oldest = i; break; }
        if (cache[i].timestamp < oldest_ts) {
            oldest_ts = cache[i].timestamp;
            oldest = i;
        }
    }
    cache[oldest].ip = ip;
    memcpy(cache[oldest].mac, mac, 6);
    cache[oldest].timestamp = timer_get_ticks() / 1000;
    cache[oldest].valid = 1;
}

uint8_t *arp_lookup(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].ip == ip)
            return cache[i].mac;
    }
    /* Not in cache — send an ARP request and wait briefly */
    arp_request(ip);
    /* Poll for a response (up to 2 seconds) */
    for (int t = 0; t < 20; t++) {
        timer_wait(100);
        /* Check if a packet arrived */
        uint8_t pkt[1536];
        int len;
        extern int ne2000_receive(void *buf, uint16_t max_len);
        extern void ne2000_poll(void);
        ne2000_poll();
        while ((len = ne2000_receive(pkt, sizeof(pkt))) > 0)
            eth_receive(pkt, len);
        /* Check cache again */
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (cache[i].valid && cache[i].ip == ip)
                return cache[i].mac;
        }
    }
    return NULL;
}

void arp_receive(const void *data, uint16_t length) {
    if (length < sizeof(arp_packet_t)) return;

    const arp_packet_t *arp = (const arp_packet_t *)data;
    if (ntohs(arp->hw_type) != ARP_HW_ETHERNET) return;
    if (ntohs(arp->proto_type) != ARP_PROTO_IPV4) return;

    /* Always learn from sender */
    arp_cache_add(arp->sender_ip, arp->sender_mac);

    uint16_t op = ntohs(arp->opcode);
    uint32_t our_ip = ipv4_get_ip();

    if (op == ARP_OP_REQUEST && arp->target_ip == our_ip) {
        /* Someone is asking for our MAC — reply */
        arp_packet_t reply;
        reply.hw_type    = htons(ARP_HW_ETHERNET);
        reply.proto_type = htons(ARP_PROTO_IPV4);
        reply.hw_len     = 6;
        reply.proto_len  = 4;
        reply.opcode     = htons(ARP_OP_REPLY);
        eth_get_mac(reply.sender_mac);
        reply.sender_ip  = our_ip;
        memcpy(reply.target_mac, arp->sender_mac, 6);
        reply.target_ip  = arp->sender_ip;
        eth_send(arp->sender_mac, ETH_TYPE_ARP, &reply, sizeof(reply));
    }
}

int arp_request(uint32_t target_ip) {
    arp_packet_t req;
    req.hw_type    = htons(ARP_HW_ETHERNET);
    req.proto_type = htons(ARP_PROTO_IPV4);
    req.hw_len     = 6;
    req.proto_len  = 4;
    req.opcode     = htons(ARP_OP_REQUEST);
    eth_get_mac(req.sender_mac);
    req.sender_ip  = ipv4_get_ip();
    memset(req.target_mac, 0, 6);
    req.target_ip  = target_ip;
    return eth_send(ETH_BROADCAST, ETH_TYPE_ARP, &req, sizeof(req));
}

void arp_cache_dump(void) {
    console_write_color("ARP Cache:\n", DN_COLOR_HEADER);
    int count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!cache[i].valid) continue;
        count++;
        uint8_t *ip = (uint8_t *)&cache[i].ip;
        console_write("  ");
        console_write_dec(ip[0]); console_write(".");
        console_write_dec(ip[1]); console_write(".");
        console_write_dec(ip[2]); console_write(".");
        console_write_dec(ip[3]);
        console_write(" -> ");
        for (int j = 0; j < 6; j++) {
            console_write_hex(cache[i].mac[j]);
            if (j < 5) console_write(":");
        }
        console_write("\n");
    }
    if (!count) console_write_color("  (empty)\n", DN_COLOR_DIM);
}
