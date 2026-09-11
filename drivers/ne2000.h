#ifndef NE2000_H
#define NE2000_H
#include "../include/types.h"

#define NE2K_VENDOR  0x10EC
#define NE2K_DEVICE  0x8029

/* NE2000 register offsets (page 0) */
#define NE_CR        0x00  /* command register */
#define NE_PSTART    0x01  /* page start (write) */
#define NE_PSTOP     0x02  /* page stop (write) */
#define NE_BNRY      0x03  /* boundary pointer */
#define NE_TSR       0x04  /* transmit status (read) */
#define NE_TPSR      0x04  /* transmit page start (write) */
#define NE_TBCR0     0x05  /* transmit byte count low (write) */
#define NE_TBCR1     0x06  /* transmit byte count high (write) */
#define NE_ISR       0x07  /* interrupt status */
#define NE_RSAR0     0x08  /* remote start address low */
#define NE_RSAR1     0x09  /* remote start address high */
#define NE_RBCR0     0x0A  /* remote byte count low */
#define NE_RBCR1     0x0B  /* remote byte count high */
#define NE_RCR       0x0C  /* receive config (write) */
#define NE_TCR       0x0D  /* transmit config (write) */
#define NE_DCR       0x0E  /* data config (write) */
#define NE_IMR       0x0F  /* interrupt mask (write) */

/* Page 1 registers */
#define NE_PAR0      0x01  /* physical address 0 */
#define NE_CURR      0x07  /* current page (read/write) */

/* NE2000 data port */
#define NE_DATA      0x10
#define NE_RESET     0x1F

/* Command register bits */
#define NE_CR_STP    0x01  /* stop */
#define NE_CR_STA    0x02  /* start */
#define NE_CR_TXP    0x04  /* transmit packet */
#define NE_CR_RD0    0x08  /* remote DMA command bit 0 */
#define NE_CR_RD1    0x10  /* remote DMA command bit 1 */
#define NE_CR_RD2    0x20  /* remote DMA command bit 2 — abort/complete */
#define NE_CR_PS0    0x40  /* page select bit 0 */
#define NE_CR_PS1    0x80  /* page select bit 1 */

/* ISR bits */
#define NE_ISR_PRX   0x01  /* packet received */
#define NE_ISR_PTX   0x02  /* packet transmitted */
#define NE_ISR_RXE   0x04  /* receive error */
#define NE_ISR_TXE   0x08  /* transmit error */
#define NE_ISR_OVW   0x10  /* overwrite warning */
#define NE_ISR_RDC   0x40  /* remote DMA complete */
#define NE_ISR_RST   0x80  /* reset status */

/* Buffer layout: 256-byte pages. Total 32KB = 128 pages (0x40-0xBF) */
#define NE_TX_START  0x40
#define NE_TX_PAGES  6     /* 6 pages = 1536 bytes max packet */
#define NE_RX_START  0x46
#define NE_RX_STOP   0x80

#define NE_MAX_PACKET 1536

int  ne2000_init(void);
int  ne2000_send(const void *buf, uint16_t length);
int  ne2000_receive(void *buf, uint16_t max_len);
void ne2000_get_mac(uint8_t mac[6]);
int  ne2000_available(void);
void ne2000_poll(void);

#endif
