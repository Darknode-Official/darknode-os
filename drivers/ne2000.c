#include "ne2000.h"
#include "pci.h"
#include "../kernel/io.h"
#include "../kernel/console.h"
#include "../kernel/string.h"
#include "../kernel/idt.h"

static uint16_t iobase = 0;
static uint8_t  mac_addr[6];
static uint8_t  irq_num = 0;
static volatile int rx_ready = 0;
static uint8_t  rx_buf[NE_MAX_PACKET];
static uint16_t rx_len = 0;

static void ne_write(uint8_t reg, uint8_t val) { outb(iobase + reg, val); }
static uint8_t ne_read(uint8_t reg) { return inb(iobase + reg); }

/* Read count bytes from NIC memory at src into dst via remote DMA */
static void ne_dma_read(uint16_t src, void *dst, uint16_t count) {
    ne_write(NE_CR, NE_CR_RD2 | NE_CR_STA);
    ne_write(NE_RBCR0, count & 0xFF);
    ne_write(NE_RBCR1, count >> 8);
    ne_write(NE_RSAR0, src & 0xFF);
    ne_write(NE_RSAR1, src >> 8);
    ne_write(NE_CR, NE_CR_RD0 | NE_CR_STA); /* remote read */
    uint16_t *p = (uint16_t *)dst;
    for (uint16_t i = 0; i < count; i += 2)
        *p++ = inw(iobase + NE_DATA);
    ne_write(NE_ISR, NE_ISR_RDC);
}

/* Write count bytes from src into NIC memory at dst via remote DMA */
static void ne_dma_write(uint16_t dst_addr, const void *src, uint16_t count) {
    ne_write(NE_CR, NE_CR_RD2 | NE_CR_STA);
    ne_write(NE_ISR, NE_ISR_RDC);
    ne_write(NE_RBCR0, count & 0xFF);
    ne_write(NE_RBCR1, count >> 8);
    ne_write(NE_RSAR0, dst_addr & 0xFF);
    ne_write(NE_RSAR1, dst_addr >> 8);
    ne_write(NE_CR, NE_CR_RD1 | NE_CR_STA); /* remote write */
    const uint16_t *p = (const uint16_t *)src;
    for (uint16_t i = 0; i < count; i += 2)
        outw(iobase + NE_DATA, *p++);
    /* Wait for DMA complete */
    int timeout = 100000;
    while (!(ne_read(NE_ISR) & NE_ISR_RDC) && --timeout > 0);
    ne_write(NE_ISR, NE_ISR_RDC);
}

static void ne2000_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t isr = ne_read(NE_ISR);
    if (isr & NE_ISR_PRX) {
        rx_ready = 1;
        ne_write(NE_ISR, NE_ISR_PRX);
    }
    if (isr & NE_ISR_PTX)  ne_write(NE_ISR, NE_ISR_PTX);
    if (isr & NE_ISR_RXE)  ne_write(NE_ISR, NE_ISR_RXE);
    if (isr & NE_ISR_TXE)  ne_write(NE_ISR, NE_ISR_TXE);
    if (isr & NE_ISR_OVW)  ne_write(NE_ISR, NE_ISR_OVW);
}

int ne2000_init(void) {
    /* Find NE2000 on PCI bus */
    int count = pci_device_count();
    pci_device_t *dev = NULL;
    for (int i = 0; i < count; i++) {
        pci_device_t *d = pci_get_device(i);
        if (d && d->vendor_id == NE2K_VENDOR && d->device_id == NE2K_DEVICE) {
            dev = d;
            break;
        }
        /* Also check for generic NE2000 (class 0x02 network) */
        if (d && d->class_code == 0x02 && d->subclass == 0x00 && !dev)
            dev = d;
    }
    if (!dev) return -1;

    /* Read BAR0 for I/O base */
    uint32_t bar0 = pci_read(dev->bus, dev->slot, dev->func, 0x10);
    iobase = bar0 & 0xFFFC;
    if (!iobase) return -1;

    /* Read IRQ line */
    uint32_t irq_reg = pci_read(dev->bus, dev->slot, dev->func, 0x3C);
    irq_num = irq_reg & 0xFF;
    if (irq_num == 0 || irq_num == 0xFF) irq_num = 11;

    /* Enable bus mastering */
    uint32_t cmd = pci_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1 << 2) | (1 << 0); /* bus master + I/O space */
    pci_write(dev->bus, dev->slot, dev->func, 0x04, cmd);

    /* Reset the NIC */
    uint8_t tmp = inb(iobase + NE_RESET);
    outb(iobase + NE_RESET, tmp);
    int timeout = 100000;
    while (!(ne_read(NE_ISR) & NE_ISR_RST) && --timeout > 0);
    ne_write(NE_ISR, 0xFF); /* clear all interrupts */

    /* Stop and configure */
    ne_write(NE_CR, NE_CR_RD2 | NE_CR_STP);
    ne_write(NE_DCR, 0x49);   /* word transfer, 8-byte FIFO, normal mode */
    ne_write(NE_RBCR0, 0);
    ne_write(NE_RBCR1, 0);
    ne_write(NE_RCR, 0x04);   /* accept broadcast */
    ne_write(NE_TCR, 0x02);   /* internal loopback during setup */
    ne_write(NE_PSTART, NE_RX_START);
    ne_write(NE_BNRY, NE_RX_START);
    ne_write(NE_PSTOP, NE_RX_STOP);

    /* Read MAC from PROM (first 6 words at address 0) */
    uint8_t prom[32];
    ne_dma_read(0, prom, 32);
    for (int i = 0; i < 6; i++)
        mac_addr[i] = prom[i * 2]; /* NE2000 stores MAC in even bytes */

    /* Set physical address on page 1 */
    ne_write(NE_CR, NE_CR_RD2 | NE_CR_STP | NE_CR_PS0); /* page 1 */
    for (int i = 0; i < 6; i++)
        ne_write(NE_PAR0 + i, mac_addr[i]);
    ne_write(NE_CURR, NE_RX_START + 1); /* current page = rx_start + 1 */
    /* Clear multicast filter (accept none) */
    for (int i = 0; i < 8; i++)
        ne_write(0x08 + i, 0xFF);

    /* Back to page 0, start the NIC */
    ne_write(NE_CR, NE_CR_RD2 | NE_CR_STA);
    ne_write(NE_TCR, 0x00);   /* normal transmit mode */
    ne_write(NE_RCR, 0x0C);   /* accept broadcast + multicast */
    ne_write(NE_ISR, 0xFF);   /* clear pending interrupts */
    ne_write(NE_IMR, NE_ISR_PRX | NE_ISR_PTX | NE_ISR_RXE | NE_ISR_TXE);

    /* Register IRQ handler */
    register_interrupt_handler(32 + irq_num, ne2000_irq_handler);

    return 0;
}

int ne2000_send(const void *buf, uint16_t length) {
    if (!iobase || length > NE_MAX_PACKET) return -1;
    if (length < 60) length = 60; /* minimum ethernet frame */

    /* Write packet to TX buffer via DMA */
    ne_dma_write(NE_TX_START << 8, buf, length);

    /* Set TX page start and byte count, then trigger TX */
    ne_write(NE_TPSR, NE_TX_START);
    ne_write(NE_TBCR0, length & 0xFF);
    ne_write(NE_TBCR1, length >> 8);
    ne_write(NE_CR, NE_CR_RD2 | NE_CR_STA | NE_CR_TXP);

    /* Wait for transmit complete */
    int timeout = 500000;
    while (!(ne_read(NE_ISR) & (NE_ISR_PTX | NE_ISR_TXE)) && --timeout > 0);
    ne_write(NE_ISR, NE_ISR_PTX | NE_ISR_TXE);

    return (timeout > 0) ? 0 : -1;
}

int ne2000_receive(void *buf, uint16_t max_len) {
    /* Check boundary vs current page to see if packets are available */
    uint8_t bnry = ne_read(NE_BNRY) + 1;
    if (bnry >= NE_RX_STOP) bnry = NE_RX_START;

    /* Read CURR from page 1 */
    ne_write(NE_CR, NE_CR_RD2 | NE_CR_STA | NE_CR_PS0);
    uint8_t curr = ne_read(NE_CURR);
    ne_write(NE_CR, NE_CR_RD2 | NE_CR_STA);

    if (bnry == curr) return 0; /* no packets */

    /* Read the 4-byte receive header: status, next_page, len_lo, len_hi */
    uint8_t hdr[4];
    ne_dma_read(bnry << 8, hdr, 4);
    uint16_t pkt_len = hdr[2] | (hdr[3] << 8);
    pkt_len -= 4; /* subtract header */

    if (pkt_len > max_len) pkt_len = max_len;
    if (pkt_len > NE_MAX_PACKET) pkt_len = NE_MAX_PACKET;

    /* Read the actual packet data (after the 4-byte header) */
    ne_dma_read((bnry << 8) + 4, buf, pkt_len);

    /* Update boundary pointer */
    uint8_t next = hdr[1];
    if (next == NE_RX_START) next = NE_RX_STOP;
    ne_write(NE_BNRY, next - 1);

    rx_ready = 0;
    return pkt_len;
}

void ne2000_get_mac(uint8_t mac[6]) {
    memcpy(mac, mac_addr, 6);
}

int ne2000_available(void) {
    return iobase != 0;
}

void ne2000_poll(void) {
    if (!iobase) return;
    uint8_t isr = ne_read(NE_ISR);
    if (isr & NE_ISR_PRX) {
        rx_ready = 1;
        ne_write(NE_ISR, NE_ISR_PRX);
    }
}
