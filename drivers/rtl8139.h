#ifndef RTL8139_H
#define RTL8139_H
#include "../include/types.h"

#define RTL8139_VENDOR  0x10EC
#define RTL8139_DEVICE  0x8139

/* Register offsets */
#define RTL_MAC0        0x00
#define RTL_MAR0        0x08
#define RTL_TXSTATUS0   0x10
#define RTL_TXADDR0     0x20
#define RTL_RXBUF       0x30
#define RTL_CMD         0x37
#define RTL_CAPR        0x38
#define RTL_CBR         0x3A
#define RTL_IMR         0x3C
#define RTL_ISR         0x3E
#define RTL_TXCONFIG    0x40
#define RTL_RXCONFIG    0x44
#define RTL_CONFIG1     0x52
#define RTL_BMCR        0x62

/* CMD bits */
#define RTL_CMD_RST     0x10
#define RTL_CMD_RE      0x08
#define RTL_CMD_TE      0x04
#define RTL_CMD_BUFE    0x01

/* ISR/IMR bits */
#define RTL_INT_ROK     0x0001
#define RTL_INT_RER     0x0002
#define RTL_INT_TOK     0x0004
#define RTL_INT_TER     0x0008
#define RTL_INT_RXOVW   0x0010

/* RxConfig bits */
#define RTL_RX_WRAP     (1 << 7)
#define RTL_RX_AB       (1 << 3)   /* accept broadcast */
#define RTL_RX_AM       (1 << 2)   /* accept multicast */
#define RTL_RX_APM      (1 << 1)   /* accept physical match */
#define RTL_RX_AAP      (1 << 0)   /* accept all (promisc) */

/* Rx buffer: 8K + 16 header + 1500 wrap */
#define RTL_RXBUF_SIZE  (8192 + 16 + 1500)

int  rtl8139_init(void);
int  rtl8139_send(const void *buf, uint16_t length);
int  rtl8139_receive(void *buf, uint16_t max_len);
void rtl8139_get_mac(uint8_t mac[6]);
int  rtl8139_available(void);

#endif
