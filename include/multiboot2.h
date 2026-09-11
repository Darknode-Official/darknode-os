#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include "types.h"

#define MULTIBOOT2_MAGIC          0x36d76289
#define MULTIBOOT2_HEADER_MAGIC   0xe85250d6
#define MULTIBOOT2_ARCH_I386      0

#define MULTIBOOT2_TAG_END        0
#define MULTIBOOT2_TAG_CMDLINE    1
#define MULTIBOOT2_TAG_BOOTLOADER 2
#define MULTIBOOT2_TAG_MODULE     3
#define MULTIBOOT2_TAG_MMAP       6
#define MULTIBOOT2_TAG_FRAMEBUF   8
#define MULTIBOOT2_TAG_ACPI_OLD   14
#define MULTIBOOT2_TAG_ACPI_NEW   15

#define MULTIBOOT2_MMAP_AVAILABLE 1
#define MULTIBOOT2_MMAP_RESERVED  2
#define MULTIBOOT2_MMAP_ACPI      3
#define MULTIBOOT2_MMAP_HIBERNATE 4
#define MULTIBOOT2_MMAP_DEFECTIVE 5

typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} PACKED multiboot2_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} PACKED multiboot2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} PACKED multiboot2_tag_mmap_t;

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} PACKED multiboot2_mmap_entry_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    char     string[];
} PACKED multiboot2_tag_string_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
    uint8_t  reserved;
} PACKED multiboot2_tag_framebuffer_t;

#endif
