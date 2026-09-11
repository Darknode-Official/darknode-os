#ifndef USB_HID_H
#define USB_HID_H
#include "../include/types.h"

/* HID class requests */
#define HID_REQ_GET_REPORT   0x01
#define HID_REQ_SET_IDLE     0x0A
#define HID_REQ_SET_PROTOCOL 0x0B

/* HID protocols */
#define HID_PROTOCOL_BOOT   0
#define HID_PROTOCOL_REPORT 1

/* HID subclass */
#define HID_SUBCLASS_BOOT 1

/* HID boot protocol types */
#define HID_BOOT_KEYBOARD 1
#define HID_BOOT_MOUSE    2

void usb_hid_init(void);
void usb_hid_poll(void);

#endif
