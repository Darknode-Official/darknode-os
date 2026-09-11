#include "pci.h"
#include "../kernel/io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define MAX_PCI_DEVICES 64

static pci_device_t devices[MAX_PCI_DEVICES];
static int num_devices = 0;

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8) | (offset & 0xFC);
    outw(PCI_CONFIG_ADDR, addr >> 16); outw(PCI_CONFIG_ADDR, addr & 0xFFFF);
    /* use 32-bit I/O */
    __asm__ volatile("outl %0, %1" : : "a"(addr), "Nd"((uint16_t)PCI_CONFIG_ADDR));
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"((uint16_t)PCI_CONFIG_DATA));
    return val;
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1 << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8) | (offset & 0xFC);
    __asm__ volatile("outl %0, %1" : : "a"(addr), "Nd"((uint16_t)PCI_CONFIG_ADDR));
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"((uint16_t)PCI_CONFIG_DATA));
}

static void scan_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32 && num_devices < MAX_PCI_DEVICES; slot++) {
        for (uint8_t func = 0; func < 8; func++) {
            uint32_t reg0 = pci_read(bus, slot, func, 0);
            uint16_t vendor = reg0 & 0xFFFF;
            if (vendor == 0xFFFF) { if (func == 0) break; continue; }
            uint32_t reg2 = pci_read(bus, slot, func, 8);
            pci_device_t *d = &devices[num_devices++];
            d->bus = bus; d->slot = slot; d->func = func;
            d->vendor_id = vendor;
            d->device_id = (reg0 >> 16) & 0xFFFF;
            d->class_code = (reg2 >> 24) & 0xFF;
            d->subclass   = (reg2 >> 16) & 0xFF;
            d->header_type = (pci_read(bus, slot, func, 0x0C) >> 16) & 0xFF;
            if (func == 0 && !(d->header_type & 0x80)) break;
        }
    }
}

void pci_init(void) {
    num_devices = 0;
    for (int bus = 0; bus < 256; bus++) scan_bus((uint8_t)bus);
}

int pci_device_count(void) { return num_devices; }
pci_device_t *pci_get_device(int index) {
    return (index >= 0 && index < num_devices) ? &devices[index] : NULL;
}
