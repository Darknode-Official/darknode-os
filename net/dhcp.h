#ifndef DHCP_H
#define DHCP_H
#include "../include/types.h"

#define DHCP_SERVER_PORT  67
#define DHCP_CLIENT_PORT  68

#define DHCP_DISCOVER  1
#define DHCP_OFFER     2
#define DHCP_REQUEST   3
#define DHCP_DECLINE   4
#define DHCP_ACK       5
#define DHCP_NAK       6
#define DHCP_RELEASE   7

#define DHCP_MAGIC 0x63825363

/* DHCP options */
#define DHCP_OPT_SUBNET     1
#define DHCP_OPT_ROUTER     3
#define DHCP_OPT_DNS        6
#define DHCP_OPT_HOSTNAME   12
#define DHCP_OPT_REQ_IP     50
#define DHCP_OPT_LEASE      51
#define DHCP_OPT_MSG_TYPE   53
#define DHCP_OPT_SERVER_ID  54
#define DHCP_OPT_PARAM_REQ  55
#define DHCP_OPT_END        255

typedef struct {
    uint8_t  op;          /* 1=request, 2=reply */
    uint8_t  htype;       /* 1=ethernet */
    uint8_t  hlen;        /* 6 for ethernet */
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;      /* client IP */
    uint32_t yiaddr;      /* your (offered) IP */
    uint32_t siaddr;      /* server IP */
    uint32_t giaddr;      /* gateway IP */
    uint8_t  chaddr[16];  /* client hardware address */
    uint8_t  sname[64];   /* server hostname */
    uint8_t  file[128];   /* boot filename */
    uint32_t magic;       /* DHCP magic cookie */
    uint8_t  options[312];
} PACKED dhcp_packet_t;

void dhcp_init(void);
int  dhcp_discover(void);
uint32_t dhcp_get_dns(void);

#endif
