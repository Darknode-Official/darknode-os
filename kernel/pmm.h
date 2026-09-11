#ifndef PMM_H
#define PMM_H
#include "../include/types.h"
#include "../include/multiboot2.h"

#define PAGE_SIZE 4096

void     pmm_init(multiboot2_info_t *mbi);
uint32_t pmm_alloc_page(void);
void     pmm_free_page(uint32_t addr);
uint32_t pmm_total_memory(void);
uint32_t pmm_used_pages(void);
uint32_t pmm_free_pages(void);

#endif
