#ifndef VBOX_H
#define VBOX_H
#include "../include/types.h"

#define VBOX_PCI_VENDOR  0x80EE
#define VBOX_PCI_DEVICE  0xCAFE

/* VMMDev request types */
#define VMMDEV_REQ_MOUSE_STATUS     1
#define VMMDEV_REQ_SET_MOUSE        2
#define VMMDEV_REQ_HOST_VERSION     4
#define VMMDEV_REQ_REPORT_GUEST     50

/* Mouse capability flags */
#define VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE   (1 << 0)
#define VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE  (1 << 1)
#define VMMDEV_MOUSE_GUEST_NEEDS_HOST     (1 << 2)
#define VMMDEV_MOUSE_HOST_HAS_ABS         (1 << 3)
#define VMMDEV_MOUSE_NEW_PROTOCOL         (1 << 4)

/* Request header */
typedef struct {
    uint32_t size;
    uint32_t version;
    uint32_t type;
    int32_t  rc;
    uint32_t reserved1;
    uint32_t reserved2;
} __attribute__((packed)) vmmdev_header_t;

/* Mouse status request */
typedef struct {
    vmmdev_header_t header;
    uint32_t features;
    int32_t  x;
    int32_t  y;
} __attribute__((packed)) vmmdev_mouse_t;

int  vbox_init(void);
void vbox_mouse_poll(void);
int  vbox_detected(void);

#endif
