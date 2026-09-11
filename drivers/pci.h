#ifndef PCI_H
#define PCI_H
#include "../include/types.h"

typedef struct {
    uint8_t  bus, slot, func;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass;
    uint8_t  header_type;
} pci_device_t;

void     pci_init(void);
uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
int      pci_device_count(void);
pci_device_t *pci_get_device(int index);

#endif
