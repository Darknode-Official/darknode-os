#include "vbox.h"
#include "pci.h"
#include "mouse.h"
#include "../kernel/io.h"
#include "../kernel/heap.h"
#include "../kernel/string.h"
#include "../kernel/console.h"

static int vbox_found = 0;
static uint16_t vbox_iobase = 0;
static vmmdev_mouse_t *mouse_req = 0;
static int screen_w = 1024;
static int screen_h = 768;

/* VMMDev protocol version */
#define VMMDEV_VERSION 0x00010001

static void vbox_request(vmmdev_header_t *req) {
    /* Submit request by writing its physical address to the I/O port */
    outl(vbox_iobase, (uint32_t)req);
}

int vbox_init(void) {
    /* Scan PCI for VBoxGuest device */
    int count = pci_device_count();
    for (int i = 0; i < count; i++) {
        pci_device_t *dev = pci_get_device(i);
        if (!dev) continue;
        if (dev->vendor_id == VBOX_PCI_VENDOR && dev->device_id == VBOX_PCI_DEVICE) {
            /* Found VBoxGuest — read I/O base from BAR0 */
            uint32_t bar0 = pci_read(dev->bus, dev->slot, dev->func, 0x10);
            if (bar0 & 0x01) {
                vbox_iobase = (uint16_t)(bar0 & 0xFFFC);
            } else {
                return -1;
            }

            /* Enable PCI bus mastering */
            uint32_t cmd = pci_read(dev->bus, dev->slot, dev->func, 0x04);
            cmd |= 0x05;  /* I/O space + bus master */
            pci_write(dev->bus, dev->slot, dev->func, 0x04, cmd);

            /* Allocate aligned request buffer */
            mouse_req = (vmmdev_mouse_t *)kmalloc(sizeof(vmmdev_mouse_t));
            if (!mouse_req) return -1;

            /* Tell VBox we support absolute mouse */
            memset(mouse_req, 0, sizeof(vmmdev_mouse_t));
            mouse_req->header.size = sizeof(vmmdev_mouse_t);
            mouse_req->header.version = VMMDEV_VERSION;
            mouse_req->header.type = VMMDEV_REQ_SET_MOUSE;
            mouse_req->features = VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE
                                | VMMDEV_MOUSE_GUEST_NEEDS_HOST;
            vbox_request(&mouse_req->header);

            vbox_found = 1;

            char buf[32];
            buf[0] = 'I'; buf[1] = '/'; buf[2] = 'O'; buf[3] = ' ';
            buf[4] = '0'; buf[5] = 'x';
            utoa(vbox_iobase, buf + 6, 16);
            console_write("  [");
            console_write_color("OK", 0x0A);
            console_write("] VBoxGuest — absolute mouse at ");
            console_write(buf);
            console_write("\n");

            return 0;
        }
    }
    return -1;
}

void vbox_mouse_poll(void) {
    if (!vbox_found || !mouse_req) return;

    /* Request current mouse position */
    memset(mouse_req, 0, sizeof(vmmdev_mouse_t));
    mouse_req->header.size = sizeof(vmmdev_mouse_t);
    mouse_req->header.version = VMMDEV_VERSION;
    mouse_req->header.type = VMMDEV_REQ_MOUSE_STATUS;
    mouse_req->features = VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE
                        | VMMDEV_MOUSE_GUEST_NEEDS_HOST;

    vbox_request(&mouse_req->header);

    /* Check if request succeeded */
    if (mouse_req->header.rc != 0) return;

    /* Convert absolute coords (0-65535) to screen coords */
    if (mouse_req->features & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE) {
        int mx = (mouse_req->x * screen_w) / 0xFFFF;
        int my = (mouse_req->y * screen_h) / 0xFFFF;

        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx >= screen_w) mx = screen_w - 1;
        if (my >= screen_h) my = screen_h - 1;

        /* Update mouse position directly — bypass relative movement */
        mouse_usb_update(mx - mouse_get_x(), my - mouse_get_y(), mouse_get_buttons());
    }
}

int vbox_detected(void) { return vbox_found; }

void vbox_set_resolution(int w, int h) {
    screen_w = w;
    screen_h = h;
}
