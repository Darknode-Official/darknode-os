#include "pmm.h"
#include "string.h"
#include "console.h"

#define BITMAP_SIZE (1024 * 1024 / 8)  /* enough for 4 GiB (1M pages × 4 KiB) */

static uint8_t bitmap[BITMAP_SIZE];
static uint32_t total_pages = 0;
static uint32_t used_pages_count = 0;
static uint32_t total_mem = 0;

static void set_page(uint32_t page) {
    bitmap[page / 8] |= (1 << (page % 8));
}
static void clear_page(uint32_t page) {
    bitmap[page / 8] &= ~(1 << (page % 8));
}
static bool test_page(uint32_t page) {
    return bitmap[page / 8] & (1 << (page % 8));
}

void pmm_init(multiboot2_info_t *mbi) {
    memset(bitmap, 0xFF, BITMAP_SIZE);
    used_pages_count = 0;
    total_pages = 0;
    total_mem = 0;

    multiboot2_tag_t *tag = (multiboot2_tag_t *)((uint32_t)mbi + 8);
    while (tag->type != MULTIBOOT2_TAG_END) {
        if (tag->type == MULTIBOOT2_TAG_MMAP) {
            multiboot2_tag_mmap_t *mmap = (multiboot2_tag_mmap_t *)tag;
            multiboot2_mmap_entry_t *entry = (multiboot2_mmap_entry_t *)((uint32_t)mmap + 16);
            multiboot2_mmap_entry_t *end = (multiboot2_mmap_entry_t *)((uint32_t)mmap + mmap->size);
            while (entry < end) {
                if (entry->type == MULTIBOOT2_MMAP_AVAILABLE) {
                    uint64_t base = entry->base_addr;
                    uint64_t len  = entry->length;
                    total_mem += (uint32_t)(len / 1024);
                    uint32_t start_page = (uint32_t)((base + PAGE_SIZE - 1) / PAGE_SIZE);
                    uint32_t page_count = (uint32_t)(len / PAGE_SIZE);
                    for (uint32_t i = 0; i < page_count && (start_page + i) < BITMAP_SIZE * 8; i++) {
                        clear_page(start_page + i);
                        total_pages++;
                    }
                }
                entry = (multiboot2_mmap_entry_t *)((uint32_t)entry + mmap->entry_size);
            }
        }
        tag = (multiboot2_tag_t *)((uint32_t)tag + ((tag->size + 7) & ~7));
    }

    /* Reserve first 2 MiB (kernel + VGA + BIOS) */
    for (uint32_t i = 0; i < 512; i++) {
        if (!test_page(i)) { set_page(i); used_pages_count++; }
    }
}

uint32_t pmm_alloc_page(void) {
    for (uint32_t i = 0; i < BITMAP_SIZE * 8; i++) {
        if (!test_page(i)) {
            set_page(i);
            used_pages_count++;
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_page(uint32_t addr) {
    uint32_t page = addr / PAGE_SIZE;
    if (test_page(page)) {
        clear_page(page);
        used_pages_count--;
    }
}

uint32_t pmm_total_memory(void) { return total_mem; }
uint32_t pmm_used_pages(void)  { return used_pages_count; }
uint32_t pmm_free_pages(void)  { return total_pages - used_pages_count; }
