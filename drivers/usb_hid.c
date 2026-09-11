#include "usb_hid.h"
#include "usb.h"
#include "../kernel/io.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../kernel/keyboard.h"
#include "../drivers/mouse.h"

/* HID boot protocol keyboard scancode-to-ASCII (US layout, subset) */
static const char hid_keymap[128] = {
    0,0,0,0,
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',
    '1','2','3','4','5','6','7','8','9','0',
    '\n',  /* enter */
    0x1B,  /* escape */
    '\b',  /* backspace */
    '\t',  /* tab */
    ' ',   /* space */
    '-','=','[',']','\\',
    0,     /* non-US # */
    ';','\'','`',',','.','/',
    0,     /* caps lock */
    /* F1-F12 */
    0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
    0x41, 0x42, 0x43, 0x44, 0x85, 0x86,
};

static const char hid_keymap_shift[128] = {
    0,0,0,0,
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')',
    '\n',0x1B,'\b','\t',' ',
    '_','+','{','}','|',
    0,':','"','~','<','>','?',
    0,
    0x3B,0x3C,0x3D,0x3E,0x3F,0x40,
    0x41,0x42,0x43,0x44,0x85,0x86,
};

/* Track which HID devices we found */
#define MAX_HID 4
static struct {
    usb_device_t *dev;
    usb_endpoint_t *ep_in;
    int type; /* 1 = keyboard, 2 = mouse */
} hid_devs[MAX_HID];
static int hid_count = 0;

/* keyboard state */
static uint8_t prev_keys[6];

/* External keyboard buffer push (declared in keyboard.h or keyboard.c) */
void keyboard_push_char(char c);

/* External mouse position update */
void mouse_usb_update(int dx, int dy, int buttons);

static int hid_set_protocol(usb_device_t *dev, uint8_t iface, uint8_t protocol) {
    usb_setup_t setup;
    setup.bmRequestType = 0x21; /* host-to-device, class, interface */
    setup.bRequest = HID_REQ_SET_PROTOCOL;
    setup.wValue = protocol;
    setup.wIndex = iface;
    setup.wLength = 0;
    return usb_control_transfer(dev, &setup, 0, 0);
}

static int hid_set_idle(usb_device_t *dev, uint8_t iface, uint8_t duration, uint8_t report_id) {
    usb_setup_t setup;
    setup.bmRequestType = 0x21;
    setup.bRequest = HID_REQ_SET_IDLE;
    setup.wValue = (duration << 8) | report_id;
    setup.wIndex = iface;
    setup.wLength = 0;
    return usb_control_transfer(dev, &setup, 0, 0);
}

void usb_hid_init(void) {
    hid_count = 0;
    memset(prev_keys, 0, sizeof(prev_keys));
    memset(hid_devs, 0, sizeof(hid_devs));

    int n = usb_device_count();
    for (int i = 0; i < n && hid_count < MAX_HID; i++) {
        usb_device_t *dev = usb_get_device(i);
        if (!dev || !dev->present) continue;

        uint8_t cls = dev->if_class ? dev->if_class : dev->dev_class;
        if (cls != 0x03) continue; /* not HID */

        /* Determine type from interface protocol */
        int type = 0;
        if (dev->if_subclass == HID_SUBCLASS_BOOT) {
            if (dev->if_protocol == HID_BOOT_KEYBOARD) type = 1;
            else if (dev->if_protocol == HID_BOOT_MOUSE) type = 2;
        }
        if (type == 0) {
            /* Try to guess from endpoints */
            if (dev->num_endpoints > 0) type = 1; /* default to keyboard */
        }

        /* Find interrupt IN endpoint */
        usb_endpoint_t *ep_in = 0;
        for (int e = 0; e < dev->num_endpoints; e++) {
            if ((dev->endpoints[e].address & 0x80) &&
                (dev->endpoints[e].attributes & 0x03) == 0x03) {
                ep_in = &dev->endpoints[e];
                break;
            }
        }
        if (!ep_in) continue;

        /* Set boot protocol and idle */
        hid_set_protocol(dev, 0, HID_PROTOCOL_BOOT);
        hid_set_idle(dev, 0, 0, 0);

        hid_devs[hid_count].dev = dev;
        hid_devs[hid_count].ep_in = ep_in;
        hid_devs[hid_count].type = type;
        hid_count++;

        console_write("  [OK] USB HID: ");
        console_write(type == 1 ? "keyboard" : type == 2 ? "mouse" : "device");
        console_write(" at addr ");
        char ab[4]; itoa(dev->address, ab, 10);
        console_write(ab);
        console_write("\n");
    }
}

static void process_keyboard_report(uint8_t *report) {
    uint8_t modifiers = report[0];
    /* report[1] is reserved */
    /* report[2..7] are keycodes */

    for (int i = 2; i < 8; i++) {
        uint8_t key = report[i];
        if (key == 0 || key == 1) continue; /* no key or error rollover */

        /* Check if this key was in the previous report */
        int was_pressed = 0;
        for (int j = 0; j < 6; j++) {
            if (prev_keys[j] == key) { was_pressed = 1; break; }
        }
        if (was_pressed) continue; /* key held, not new press */

        /* Convert to ASCII */
        if (key < 128) {
            int shift = (modifiers & 0x22); /* left or right shift */
            char ch = shift ? hid_keymap_shift[key] : hid_keymap[key];
            if (ch) keyboard_push_char(ch);
        }
    }

    /* Save current keys for next comparison */
    for (int i = 0; i < 6; i++)
        prev_keys[i] = report[i + 2];
}

static void process_mouse_report(uint8_t *report) {
    int buttons = report[0] & 0x07;
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];
    mouse_usb_update(dx, dy, buttons);
}

void usb_hid_poll(void) {
    for (int i = 0; i < hid_count; i++) {
        uint8_t buf[8];
        memset(buf, 0, sizeof(buf));

        int ret = usb_interrupt_transfer(hid_devs[i].dev, hid_devs[i].ep_in,
                                         buf, hid_devs[i].ep_in->max_packet);
        if (ret != 0) continue; /* NAK or error — no data ready */

        if (hid_devs[i].type == 1) {
            process_keyboard_report(buf);
        } else if (hid_devs[i].type == 2) {
            process_mouse_report(buf);
        }
    }
}
