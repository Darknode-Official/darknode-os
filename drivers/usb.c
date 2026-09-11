#include "usb.h"
#include "pci.h"
#include "../kernel/io.h"
#include "../kernel/heap.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../kernel/timer.h"

/* UHCI register offsets (from I/O base) */
#define UHCI_CMD        0x00
#define UHCI_STS        0x02
#define UHCI_INTR       0x04
#define UHCI_FRNUM      0x06
#define UHCI_FLBASEADD  0x08
#define UHCI_SOFMOD     0x0C
#define UHCI_PORTSC1    0x10
#define UHCI_PORTSC2    0x12

/* UHCI command bits */
#define UHCI_CMD_RS     0x0001
#define UHCI_CMD_HCRESET 0x0002
#define UHCI_CMD_GRESET 0x0004
#define UHCI_CMD_MAXP   0x0080

/* UHCI status bits */
#define UHCI_STS_INT    0x0001
#define UHCI_STS_ERR    0x0002
#define UHCI_STS_HCHALTED 0x0020

/* Port status bits */
#define UHCI_PORT_CONN      0x0001
#define UHCI_PORT_CONN_CHG  0x0002
#define UHCI_PORT_ENABLE    0x0004
#define UHCI_PORT_ENABLE_CHG 0x0008
#define UHCI_PORT_LOSPEED   0x0100
#define UHCI_PORT_RESET     0x0200
#define UHCI_PORT_SUSP      0x1000

/* TD status bits */
#define TD_STATUS_ACTIVE    (1 << 23)
#define TD_STATUS_STALL     (1 << 22)
#define TD_STATUS_DBUF      (1 << 21)
#define TD_STATUS_BABBLE    (1 << 20)
#define TD_STATUS_NAK       (1 << 19)
#define TD_STATUS_CRC       (1 << 18)
#define TD_STATUS_BITSTUFF  (1 << 17)
#define TD_STATUS_SPD       (1 << 29)
#define TD_STATUS_IOC       (1 << 24)
#define TD_STATUS_LS        (1 << 26)

/* PID tokens */
#define TD_PID_SETUP  0x2D
#define TD_PID_IN     0x69
#define TD_PID_OUT    0xE1

/* UHCI Transfer Descriptor — must be 16-byte aligned */
typedef struct uhci_td {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
    /* software fields (not read by hardware) */
    uint32_t _pad[4];
} __attribute__((packed, aligned(16))) uhci_td_t;

/* UHCI Queue Head — must be 16-byte aligned */
typedef struct uhci_qh {
    uint32_t head_link;
    uint32_t element;
    uint32_t _pad[2];
} __attribute__((packed, aligned(16))) uhci_qh_t;

#define UHCI_LINK_TERMINATE 0x01
#define UHCI_LINK_QH        0x02

/* Controller state */
static uint16_t uhci_iobase = 0;
static uint32_t *frame_list = 0;
static uhci_qh_t *qh_pool = 0;
static uhci_td_t *td_pool = 0;
static int td_next = 0;
#define TD_POOL_SIZE 64

static usb_device_t devices[USB_MAX_DEVICES];
static int dev_count = 0;
static uint8_t next_address = 1;

static void uhci_delay(int ms) {
    uint32_t end = timer_get_ticks() + ms;
    while (timer_get_ticks() < end) __asm__ volatile("pause");
}

uint32_t timer_get_ticks(void);

static uhci_td_t *alloc_td(void) {
    if (td_next >= TD_POOL_SIZE) return 0;
    uhci_td_t *td = &td_pool[td_next++];
    memset(td, 0, sizeof(uhci_td_t));
    return td;
}

static void reset_td_pool(void) { td_next = 0; }

static void uhci_port_reset(uint16_t port_reg) {
    outw(uhci_iobase + port_reg, UHCI_PORT_RESET);
    uhci_delay(50);
    outw(uhci_iobase + port_reg, 0);
    uhci_delay(10);
    /* enable port */
    uint16_t st = inw(uhci_iobase + port_reg);
    if (st & UHCI_PORT_CONN) {
        outw(uhci_iobase + port_reg, UHCI_PORT_ENABLE | UHCI_PORT_CONN_CHG | UHCI_PORT_ENABLE_CHG);
        uhci_delay(10);
    }
}

static int uhci_wait_td(uhci_td_t *td, int timeout_ms) {
    uint32_t end = timer_get_ticks() + timeout_ms;
    while (timer_get_ticks() < end) {
        if (!(td->status & TD_STATUS_ACTIVE)) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int uhci_transfer(uint8_t addr, uint8_t speed, usb_setup_t *setup,
                         uint8_t pid_data, void *data, int len, int *toggle) {
    reset_td_pool();
    uhci_qh_t *qh = &qh_pool[0];
    memset(qh, 0, sizeof(uhci_qh_t));
    qh->head_link = UHCI_LINK_TERMINATE;

    uhci_td_t *first_td = 0, *prev_td = 0;
    int max_pkt = 8;

    /* Setup stage (if setup packet provided) */
    if (setup) {
        uhci_td_t *td = alloc_td();
        if (!td) return -1;
        td->link = UHCI_LINK_TERMINATE;
        td->status = TD_STATUS_ACTIVE | (3 << 27); /* 3 retries */
        if (speed == USB_SPEED_LOW) td->status |= TD_STATUS_LS;
        td->token = (7 << 21) | (0 << 19) | (addr << 8) | TD_PID_SETUP;
        td->buffer = (uint32_t)setup;
        first_td = td;
        prev_td = td;
        if (toggle) *toggle = 1;
    }

    /* Data stage */
    int offset = 0;
    while (offset < len) {
        uhci_td_t *td = alloc_td();
        if (!td) return -1;
        int chunk = len - offset;
        if (chunk > max_pkt) chunk = max_pkt;

        td->link = UHCI_LINK_TERMINATE;
        td->status = TD_STATUS_ACTIVE | (3 << 27);
        if (speed == USB_SPEED_LOW) td->status |= TD_STATUS_LS;
        int tog = toggle ? *toggle : 0;
        td->token = ((chunk - 1) << 21) | (tog << 19) | (addr << 8) | pid_data;
        td->buffer = (uint32_t)((uint8_t *)data + offset);

        if (prev_td) prev_td->link = (uint32_t)td & ~0xF;
        else first_td = td;
        prev_td = td;
        offset += chunk;
        if (toggle) *toggle ^= 1;
    }

    /* Status stage (if setup packet — opposite direction, zero length) */
    if (setup) {
        uhci_td_t *td = alloc_td();
        if (!td) return -1;
        td->link = UHCI_LINK_TERMINATE;
        td->status = TD_STATUS_ACTIVE | TD_STATUS_IOC | (3 << 27);
        if (speed == USB_SPEED_LOW) td->status |= TD_STATUS_LS;
        uint8_t status_pid = (pid_data == TD_PID_IN) ? TD_PID_OUT : TD_PID_IN;
        /* data toggle 1 for status */
        td->token = (0x7FF << 21) | (1 << 19) | (addr << 8) | status_pid;
        td->buffer = 0;
        if (prev_td) prev_td->link = (uint32_t)td & ~0xF;
        prev_td = td;
    }

    if (!first_td) return 0;

    /* Point QH at first TD */
    qh->element = (uint32_t)first_td & ~0xF;

    /* Insert QH into frame list */
    frame_list[0] = ((uint32_t)qh & ~0xF) | UHCI_LINK_QH;

    /* Start controller */
    outw(uhci_iobase + UHCI_FRNUM, 0);
    outw(uhci_iobase + UHCI_CMD, UHCI_CMD_RS | UHCI_CMD_MAXP);

    /* Wait for last TD to complete */
    int ret = uhci_wait_td(prev_td, 500);

    /* Stop controller */
    outw(uhci_iobase + UHCI_CMD, 0);
    uhci_delay(1);

    /* Clear frame list entry */
    frame_list[0] = UHCI_LINK_TERMINATE;

    if (ret != 0) return -1;

    /* Check for errors in all TDs */
    for (int i = 0; i < td_next; i++) {
        uint32_t st = td_pool[i].status;
        if (st & (TD_STATUS_STALL | TD_STATUS_DBUF | TD_STATUS_BABBLE | TD_STATUS_CRC | TD_STATUS_BITSTUFF))
            return -1;
    }

    return 0;
}

int usb_control_transfer(usb_device_t *dev, usb_setup_t *setup,
                         void *data, int len) {
    uint8_t pid = (setup->bmRequestType & USB_DIR_IN) ? TD_PID_IN : TD_PID_OUT;
    int toggle = 0;
    return uhci_transfer(dev->address, dev->speed, setup, pid, data, len, &toggle);
}

int usb_interrupt_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                           void *data, int len) {
    uint8_t pid = (ep->address & 0x80) ? TD_PID_IN : TD_PID_OUT;
    return uhci_transfer(dev->address, dev->speed, 0, pid, data, len, &ep->toggle);
}

static int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                              void *buf, int len) {
    usb_setup_t setup;
    setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (type << 8) | index;
    setup.wIndex = 0;
    setup.wLength = len;
    return usb_control_transfer(dev, &setup, buf, len);
}

static int usb_set_address(usb_device_t *dev, uint8_t addr) {
    usb_setup_t setup;
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = addr;
    setup.wIndex = 0;
    setup.wLength = 0;
    return uhci_transfer(0, dev->speed, &setup, TD_PID_IN, 0, 0, 0);
}

static int usb_set_config(usb_device_t *dev, uint8_t config) {
    usb_setup_t setup;
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_CONFIG;
    setup.wValue = config;
    setup.wIndex = 0;
    setup.wLength = 0;
    return usb_control_transfer(dev, &setup, 0, 0);
}

static void enumerate_port(int port_num, uint16_t port_reg) {
    uint16_t st = inw(uhci_iobase + port_reg);
    if (!(st & UHCI_PORT_CONN)) return;

    uhci_port_reset(port_reg);
    uhci_delay(20);

    st = inw(uhci_iobase + port_reg);
    if (!(st & UHCI_PORT_CONN)) return;

    if (dev_count >= USB_MAX_DEVICES) return;
    usb_device_t *dev = &devices[dev_count];
    memset(dev, 0, sizeof(usb_device_t));
    dev->present = 1;
    dev->port = port_num;
    dev->address = 0;
    dev->speed = (st & UHCI_PORT_LOSPEED) ? USB_SPEED_LOW : USB_SPEED_FULL;
    dev->max_packet0 = 8;

    /* Get first 8 bytes of device descriptor at address 0 */
    usb_device_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    if (usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &desc, 8) != 0) {
        console_write("  [!!] USB: failed to read device descriptor\n");
        dev->present = 0;
        return;
    }
    dev->max_packet0 = desc.bMaxPacketSize0 ? desc.bMaxPacketSize0 : 8;

    /* Set address */
    uint8_t addr = next_address++;
    if (usb_set_address(dev, addr) != 0) {
        console_write("  [!!] USB: set address failed\n");
        dev->present = 0;
        return;
    }
    dev->address = addr;
    uhci_delay(10);

    /* Get full device descriptor */
    if (usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &desc, sizeof(desc)) != 0) {
        console_write("  [!!] USB: full descriptor read failed\n");
        dev->present = 0;
        return;
    }
    dev->vendor_id = desc.idVendor;
    dev->product_id = desc.idProduct;
    dev->dev_class = desc.bDeviceClass;
    dev->dev_subclass = desc.bDeviceSubClass;
    dev->dev_protocol = desc.bDeviceProtocol;

    /* Get config descriptor (first 64 bytes) */
    uint8_t cbuf[64];
    memset(cbuf, 0, sizeof(cbuf));
    if (usb_get_descriptor(dev, USB_DESC_CONFIG, 0, cbuf, sizeof(cbuf)) == 0) {
        /* Parse interface + endpoint descriptors */
        int pos = ((usb_config_desc_t *)cbuf)->bLength;
        int total = ((usb_config_desc_t *)cbuf)->wTotalLength;
        if (total > (int)sizeof(cbuf)) total = sizeof(cbuf);

        while (pos + 2 <= total) {
            uint8_t dlen = cbuf[pos];
            uint8_t dtype = cbuf[pos + 1];
            if (dlen == 0) break;

            if (dtype == USB_DESC_INTERFACE && pos + 9 <= total) {
                usb_interface_desc_t *iface = (usb_interface_desc_t *)&cbuf[pos];
                dev->if_class = iface->bInterfaceClass;
                dev->if_subclass = iface->bInterfaceSubClass;
                dev->if_protocol = iface->bInterfaceProtocol;
            } else if (dtype == USB_DESC_ENDPOINT && pos + 7 <= total && dev->num_endpoints < USB_MAX_ENDPOINTS) {
                usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&cbuf[pos];
                usb_endpoint_t *e = &dev->endpoints[dev->num_endpoints++];
                e->address = ep->bEndpointAddress;
                e->attributes = ep->bmAttributes;
                e->max_packet = ep->wMaxPacketSize;
                e->interval = ep->bInterval;
                e->toggle = 0;
            }
            pos += dlen;
        }

        /* Set configuration */
        usb_set_config(dev, ((usb_config_desc_t *)cbuf)->bConfigurationValue);
    }

    dev_count++;

    /* Print device info */
    char buf[80];
    buf[0] = '\0';
    strcat(buf, "  [OK] USB port ");
    char pn[4]; itoa(port_num + 1, pn, 10); strcat(buf, pn);
    strcat(buf, ": addr=");
    char an[4]; itoa(addr, an, 10); strcat(buf, an);
    strcat(buf, " class=");
    char cn[4]; utoa(dev->if_class ? dev->if_class : dev->dev_class, cn, 16);
    strcat(buf, cn);
    if (dev->if_class == 0x03) strcat(buf, " (HID)");
    else if (dev->if_class == 0x08) strcat(buf, " (Mass Storage)");
    else if (dev->if_class == 0x09) strcat(buf, " (Hub)");
    strcat(buf, "\n");
    console_write(buf);
}

void usb_init(void) {
    /* Find a UHCI controller on PCI (class 0x0C, subclass 0x03) */
    int n = pci_device_count();
    for (int i = 0; i < n; i++) {
        pci_device_t *d = pci_get_device(i);
        if (!d) continue;
        if (d->class_code != 0x0C || d->subclass != 0x03) continue;
        /* Check prog IF = 0x00 for UHCI */
        uint32_t reg2 = pci_read(d->bus, d->slot, d->func, 0x08);
        uint8_t prog_if = (reg2 >> 8) & 0xFF;
        if (prog_if != 0x00) continue;

        /* Read BAR4 (I/O base for UHCI) */
        uint32_t bar4 = pci_read(d->bus, d->slot, d->func, 0x20);
        if (!(bar4 & 0x01)) continue;
        uhci_iobase = bar4 & 0xFFE0;

        /* Enable bus mastering + I/O space */
        uint32_t cmd = pci_read(d->bus, d->slot, d->func, 0x04);
        cmd |= 0x05;
        pci_write(d->bus, d->slot, d->func, 0x04, cmd);

        break;
    }

    if (!uhci_iobase) return;

    console_write("  [OK] USB UHCI controller at I/O ");
    char iobuf[8]; utoa(uhci_iobase, iobuf, 16);
    console_write("0x"); console_write(iobuf); console_write("\n");

    /* Allocate frame list (4096-byte aligned) and TD/QH pools */
    frame_list = kmalloc(4096 + 4096);
    frame_list = (uint32_t *)(((uint32_t)frame_list + 4095) & ~4095);
    for (int i = 0; i < 1024; i++)
        frame_list[i] = UHCI_LINK_TERMINATE;

    td_pool = kmalloc(sizeof(uhci_td_t) * TD_POOL_SIZE + 16);
    td_pool = (uhci_td_t *)(((uint32_t)td_pool + 15) & ~15);
    memset(td_pool, 0, sizeof(uhci_td_t) * TD_POOL_SIZE);

    qh_pool = kmalloc(sizeof(uhci_qh_t) * 4 + 16);
    qh_pool = (uhci_qh_t *)(((uint32_t)qh_pool + 15) & ~15);
    memset(qh_pool, 0, sizeof(uhci_qh_t) * 4);

    memset(devices, 0, sizeof(devices));
    dev_count = 0;

    /* Global reset */
    outw(uhci_iobase + UHCI_CMD, UHCI_CMD_GRESET);
    uhci_delay(50);
    outw(uhci_iobase + UHCI_CMD, 0);
    uhci_delay(10);

    /* Host controller reset */
    outw(uhci_iobase + UHCI_CMD, UHCI_CMD_HCRESET);
    uhci_delay(10);
    int timeout = 100;
    while ((inw(uhci_iobase + UHCI_CMD) & UHCI_CMD_HCRESET) && timeout-- > 0)
        uhci_delay(1);

    /* Configure */
    outw(uhci_iobase + UHCI_INTR, 0);
    outw(uhci_iobase + UHCI_FRNUM, 0);
    outl(uhci_iobase + UHCI_FLBASEADD, (uint32_t)frame_list);
    outw(uhci_iobase + UHCI_STS, 0xFFFF);

    /* Enumerate ports */
    enumerate_port(0, UHCI_PORTSC1);
    enumerate_port(1, UHCI_PORTSC2);

    if (dev_count == 0) {
        console_write("  [--] USB: no devices found\n");
    }
}

int usb_device_count(void) { return dev_count; }

usb_device_t *usb_get_device(int index) {
    if (index < 0 || index >= dev_count) return 0;
    return &devices[index];
}
