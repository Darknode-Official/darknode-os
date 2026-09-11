#include "rtl8139.h"
#include "pci.h"
#include "../kernel/io.h"
#include "../kernel/heap.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../kernel/idt.h"

static uint16_t iobase = 0;
static uint8_t  mac[6];
static uint8_t *rx_buffer = 0;
static uint32_t rx_offset = 0;
static uint8_t  tx_slot = 0;
static int      rtl_found = 0;
static int      rx_ready = 0;

/* 4 transmit buffers, 2K each */
static uint8_t tx_bufs[4][2048] __attribute__((aligned(4)));

static void rtl8139_handler(registers_t *regs) {
    (void)regs;
    uint16_t status = inw(iobase + RTL_ISR);
    if (status & RTL_INT_ROK) {
        rx_ready = 1;
    }
    /* acknowledge all */
    outw(iobase + RTL_ISR, status);
}

int rtl8139_init(void) {
    /* find RTL8139 on PCI bus */
    int count = pci_device_count();
    pci_device_t *dev = 0;
    for (int i = 0; i < count; i++) {
        pci_device_t *d = pci_get_device(i);
        if (d && d->vendor_id == RTL8139_VENDOR && d->device_id == RTL8139_DEVICE) {
            dev = d;
            break;
        }
    }
    if (!dev) return -1;

    /* read I/O base from BAR0 */
    uint32_t bar0 = pci_read(dev->bus, dev->slot, dev->func, 0x10);
    iobase = (uint16_t)(bar0 & 0xFFFC);

    /* enable PCI bus mastering */
    uint32_t cmd = pci_read(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1 << 2) | (1 << 0); /* bus master + I/O space */
    pci_write(dev->bus, dev->slot, dev->func, 0x04, cmd);

    /* power on */
    outb(iobase + RTL_CONFIG1, 0x00);

    /* software reset */
    outb(iobase + RTL_CMD, RTL_CMD_RST);
    for (int i = 0; i < 100000; i++) {
        if (!(inb(iobase + RTL_CMD) & RTL_CMD_RST)) break;
    }

    /* allocate Rx buffer (8K + 16 + 1500, 4-byte aligned) */
    rx_buffer = (uint8_t *)kmalloc(RTL_RXBUF_SIZE + 16);
    if (!rx_buffer) return -2;
    /* align to 4 bytes */
    uint32_t rx_phys = (uint32_t)rx_buffer;
    if (rx_phys & 3) rx_phys = (rx_phys + 3) & ~3;
    rx_buffer = (uint8_t *)rx_phys;
    memset(rx_buffer, 0, RTL_RXBUF_SIZE);

    /* set Rx buffer address */
    outl(iobase + RTL_RXBUF, rx_phys);

    /* read MAC address */
    for (int i = 0; i < 6; i++)
        mac[i] = inb(iobase + RTL_MAC0 + i);

    /* configure interrupts: ROK + TOK */
    outw(iobase + RTL_IMR, RTL_INT_ROK | RTL_INT_TOK);

    /* configure Rx: accept broadcast + physical match + multicast, wrap, 8K buffer */
    outl(iobase + RTL_RXCONFIG, RTL_RX_WRAP | RTL_RX_AB | RTL_RX_AM | RTL_RX_APM);

    /* configure Tx: default DMA burst, interframe gap */
    outl(iobase + RTL_TXCONFIG, 0x03000700);

    /* register IRQ handler */
    uint8_t irq_line = (uint8_t)(pci_read(dev->bus, dev->slot, dev->func, 0x3C) & 0xFF);
    if (irq_line > 0 && irq_line < 16)
        register_interrupt_handler(32 + irq_line, rtl8139_handler);

    /* enable Rx and Tx */
    outb(iobase + RTL_CMD, RTL_CMD_RE | RTL_CMD_TE);

    rx_offset = 0;
    tx_slot = 0;
    rtl_found = 1;

    console_write("  RTL8139: iobase=0x");
    char buf[8];
    /* quick hex conversion */
    for (int i = 3; i >= 0; i--) {
        int nibble = (iobase >> (i * 4)) & 0xF;
        buf[3 - i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    buf[4] = '\0';
    console_write(buf);
    console_write(" MAC=");
    for (int i = 0; i < 6; i++) {
        buf[0] = "0123456789AB"[mac[i] >> 4];
        buf[1] = "0123456789AB"[mac[i] & 0xF];
        buf[2] = (i < 5) ? ':' : '\0';
        buf[3] = '\0';
        console_write(buf);
    }
    console_write("\n");

    return 0;
}

int rtl8139_send(const void *data, uint16_t length) {
    if (!rtl_found || !data || length == 0 || length > 1792) return -1;

    memcpy(tx_bufs[tx_slot], data, length);
    /* pad to minimum 60 bytes */
    if (length < 60) {
        memset(tx_bufs[tx_slot] + length, 0, 60 - length);
        length = 60;
    }

    /* set Tx address and status */
    outl(iobase + RTL_TXADDR0 + tx_slot * 4, (uint32_t)tx_bufs[tx_slot]);
    outl(iobase + RTL_TXSTATUS0 + tx_slot * 4, length & 0x1FFF);

    /* wait for Tx to complete (OWN bit set) */
    for (int i = 0; i < 100000; i++) {
        uint32_t st = inl(iobase + RTL_TXSTATUS0 + tx_slot * 4);
        if (st & (1 << 15)) break; /* TOK */
        if (st & (1 << 30)) return -2; /* TUN - underrun */
    }

    tx_slot = (tx_slot + 1) & 3;
    return 0;
}

int rtl8139_receive(void *buf, uint16_t max_len) {
    if (!rtl_found || !rx_buffer) return 0;

    /* check if buffer is empty */
    uint8_t cmd = inb(iobase + RTL_CMD);
    if (cmd & RTL_CMD_BUFE) return 0;

    /* read packet header: status(16) + length(16) */
    uint32_t *header = (uint32_t *)(rx_buffer + rx_offset);
    uint16_t rx_status = (uint16_t)(*header & 0xFFFF);
    uint16_t rx_length = (uint16_t)(*header >> 16);

    if (rx_length == 0 || rx_length > 1600) {
        /* bad packet, reset offset */
        rx_offset = (inw(iobase + RTL_CBR)) % RTL_RXBUF_SIZE;
        return 0;
    }

    if (!(rx_status & 0x01)) return 0; /* ROK not set */

    /* copy packet data (skip 4-byte header) */
    uint16_t copy_len = rx_length - 4; /* subtract CRC */
    if (copy_len > max_len) copy_len = max_len;

    uint32_t data_offset = rx_offset + 4;
    if (data_offset + copy_len <= RTL_RXBUF_SIZE) {
        memcpy(buf, rx_buffer + data_offset, copy_len);
    } else {
        /* wrap around */
        uint32_t first = RTL_RXBUF_SIZE - data_offset;
        memcpy(buf, rx_buffer + data_offset, first);
        memcpy((uint8_t *)buf + first, rx_buffer, copy_len - first);
    }

    /* advance rx_offset: header(4) + length, aligned to 4, +4 for CRC */
    rx_offset = (rx_offset + rx_length + 4 + 3) & ~3;
    rx_offset %= RTL_RXBUF_SIZE;

    /* update CAPR (read pointer) */
    outw(iobase + RTL_CAPR, rx_offset - 16);

    rx_ready = 0;
    return copy_len;
}

void rtl8139_get_mac(uint8_t out[6]) {
    memcpy(out, mac, 6);
}

int rtl8139_available(void) {
    if (!rtl_found) return 0;
    uint8_t cmd = inb(iobase + RTL_CMD);
    return !(cmd & RTL_CMD_BUFE);
}
