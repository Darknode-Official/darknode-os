#include "dhcp.h"
#include "udp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../kernel/timer.h"
#include "../include/darknode.h"
#include "../drivers/ne2000.h"

static uint32_t xid = 0x44415243; /* "DARC" */
static volatile uint8_t  dhcp_state = 0; /* 0=idle, 1=discovering, 2=requesting, 3=bound */
static uint32_t offered_ip   = 0;
static uint32_t server_ip    = 0;
static uint32_t dns_server   = 0;

uint32_t dhcp_get_dns(void) { return dns_server; }

static uint8_t *dhcp_find_option(const uint8_t *opts, uint16_t opts_len, uint8_t opt_code) {
    uint16_t i = 0;
    while (i < opts_len) {
        uint8_t code = opts[i++];
        if (code == DHCP_OPT_END) break;
        if (code == 0) continue; /* pad */
        if (i >= opts_len) break;
        uint8_t len = opts[i++];
        if (code == opt_code) return (uint8_t *)&opts[i];
        i += len;
    }
    return NULL;
}

static void dhcp_udp_handler(uint32_t src_ip, uint16_t src_port,
                              const void *data, uint16_t length)
{
    (void)src_ip; (void)src_port;
    if (length < 240) return;
    const dhcp_packet_t *pkt = (const dhcp_packet_t *)data;
    if (pkt->op != 2) return; /* not a reply */
    if (pkt->xid != xid) return;

    uint16_t opts_len = length - 240;
    const uint8_t *opts = pkt->options;

    uint8_t *msg_type = dhcp_find_option(opts, opts_len, DHCP_OPT_MSG_TYPE);
    if (!msg_type) return;

    if (*msg_type == DHCP_OFFER && dhcp_state == 1) {
        /* Got an offer — send DHCP REQUEST */
        offered_ip = pkt->yiaddr;
        uint8_t *sid = dhcp_find_option(opts, opts_len, DHCP_OPT_SERVER_ID);
        if (sid) server_ip = *(uint32_t *)sid;

        dhcp_packet_t req;
        memset(&req, 0, sizeof(req));
        req.op    = 1;
        req.htype = 1;
        req.hlen  = 6;
        req.xid   = xid;
        req.flags = htons(0x8000); /* broadcast */
        eth_get_mac(req.chaddr);
        req.magic = htonl(DHCP_MAGIC);

        int oi = 0;
        req.options[oi++] = DHCP_OPT_MSG_TYPE;
        req.options[oi++] = 1;
        req.options[oi++] = DHCP_REQUEST;
        req.options[oi++] = DHCP_OPT_REQ_IP;
        req.options[oi++] = 4;
        memcpy(&req.options[oi], &offered_ip, 4); oi += 4;
        req.options[oi++] = DHCP_OPT_SERVER_ID;
        req.options[oi++] = 4;
        memcpy(&req.options[oi], &server_ip, 4); oi += 4;
        req.options[oi++] = DHCP_OPT_HOSTNAME;
        req.options[oi++] = 8;
        memcpy(&req.options[oi], "darknode", 8); oi += 8;
        req.options[oi++] = DHCP_OPT_END;

        dhcp_state = 2;
        udp_send(0xFFFFFFFF, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, &req, 240 + oi);
    }
    else if (*msg_type == DHCP_ACK && dhcp_state == 2) {
        /* Got ACK — configure the network */
        uint32_t subnet  = 0xFFFFFF00; /* default /24 */
        uint32_t gateway = 0;

        uint8_t *sub = dhcp_find_option(opts, opts_len, DHCP_OPT_SUBNET);
        if (sub) subnet = *(uint32_t *)sub;

        uint8_t *gw = dhcp_find_option(opts, opts_len, DHCP_OPT_ROUTER);
        if (gw) gateway = *(uint32_t *)gw;

        uint8_t *dns = dhcp_find_option(opts, opts_len, DHCP_OPT_DNS);
        if (dns) dns_server = *(uint32_t *)dns;

        ipv4_config(offered_ip, subnet, gateway);
        dhcp_state = 3;

        char ipbuf[20];
        ip_format(offered_ip, ipbuf);
        console_write("  [");
        console_write_color("OK", DN_COLOR_OK);
        console_write("] DHCP — ");
        console_write_color(ipbuf, DN_COLOR_ACCENT);
        if (gateway) {
            ip_format(gateway, ipbuf);
            console_write_color(" gw ", DN_COLOR_DIM);
            console_write(ipbuf);
        }
        if (dns_server) {
            ip_format(dns_server, ipbuf);
            console_write_color(" dns ", DN_COLOR_DIM);
            console_write(ipbuf);
        }
        console_write("\n");
    }
}

void dhcp_init(void) {
    dhcp_state = 0;
    offered_ip = 0;
    server_ip  = 0;
    dns_server = 0;
}

int dhcp_discover(void) {
    udp_bind(DHCP_CLIENT_PORT, dhcp_udp_handler);

    /* Build DHCP DISCOVER */
    dhcp_packet_t disc;
    memset(&disc, 0, sizeof(disc));
    disc.op    = 1;
    disc.htype = 1;
    disc.hlen  = 6;
    disc.xid   = xid;
    disc.flags = htons(0x8000);
    eth_get_mac(disc.chaddr);
    disc.magic = htonl(DHCP_MAGIC);

    int oi = 0;
    disc.options[oi++] = DHCP_OPT_MSG_TYPE;
    disc.options[oi++] = 1;
    disc.options[oi++] = DHCP_DISCOVER;
    disc.options[oi++] = DHCP_OPT_HOSTNAME;
    disc.options[oi++] = 8;
    memcpy(&disc.options[oi], "darknode", 8); oi += 8;
    disc.options[oi++] = DHCP_OPT_PARAM_REQ;
    disc.options[oi++] = 3;
    disc.options[oi++] = DHCP_OPT_SUBNET;
    disc.options[oi++] = DHCP_OPT_ROUTER;
    disc.options[oi++] = DHCP_OPT_DNS;
    disc.options[oi++] = DHCP_OPT_END;

    dhcp_state = 1;

    /* Need to temporarily set IP to 0.0.0.0 for DHCP */
    ipv4_config(0, 0, 0);

    /* Send via raw ethernet broadcast since we have no IP yet */
    /* Build full UDP+IP manually for 0.0.0.0 → 255.255.255.255 */
    udp_send(0xFFFFFFFF, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, &disc, 240 + oi);

    /* Poll for responses (up to 5 seconds) */
    uint32_t start = timer_get_ticks();
    while (dhcp_state != 3 && (timer_get_ticks() - start) < 5000) {
        uint8_t rxbuf[1536];
        int len;
        ne2000_poll();
        while ((len = ne2000_receive(rxbuf, sizeof(rxbuf))) > 0)
            eth_receive(rxbuf, len);
    }

    udp_unbind(DHCP_CLIENT_PORT);

    return (dhcp_state == 3) ? 0 : -1;
}
