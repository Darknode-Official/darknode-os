#ifndef USB_H
#define USB_H
#include "../include/types.h"

/* USB descriptor types */
#define USB_DESC_DEVICE        1
#define USB_DESC_CONFIG        2
#define USB_DESC_STRING        3
#define USB_DESC_INTERFACE     4
#define USB_DESC_ENDPOINT      5

/* Standard requests */
#define USB_REQ_GET_STATUS     0x00
#define USB_REQ_SET_ADDRESS    0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_CONFIG     0x09

/* Request type bits */
#define USB_DIR_IN             0x80
#define USB_DIR_OUT            0x00
#define USB_TYPE_STANDARD      0x00
#define USB_TYPE_CLASS         0x20
#define USB_RECIP_DEVICE       0x00
#define USB_RECIP_INTERFACE    0x01

/* Speeds */
#define USB_SPEED_LOW          0
#define USB_SPEED_FULL         1

/* Max devices */
#define USB_MAX_DEVICES 8
#define USB_MAX_ENDPOINTS 4

/* Setup packet */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

/* Device descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* Config descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

/* Interface descriptor */
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed)) usb_interface_desc_t;

/* Endpoint descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_desc_t;

/* Tracked endpoint */
typedef struct {
    uint8_t  address;
    uint8_t  attributes;
    uint16_t max_packet;
    uint8_t  interval;
    int      toggle;
} usb_endpoint_t;

/* Tracked device */
typedef struct {
    int      present;
    int      port;
    uint8_t  address;
    uint8_t  speed;
    uint8_t  max_packet0;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  dev_class;
    uint8_t  dev_subclass;
    uint8_t  dev_protocol;
    uint8_t  if_class;
    uint8_t  if_subclass;
    uint8_t  if_protocol;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    int      num_endpoints;
} usb_device_t;

void usb_init(void);
int  usb_device_count(void);
usb_device_t *usb_get_device(int index);
int  usb_control_transfer(usb_device_t *dev, usb_setup_t *setup,
                          void *data, int len);
int  usb_interrupt_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                            void *data, int len);

#endif
