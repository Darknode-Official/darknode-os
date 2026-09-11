#include "heap.h"
#include "pmm.h"
#include "string.h"

#define HEAP_START 0x00400000   /* 4 MiB */
#define HEAP_SIZE  (4 * 1024 * 1024)  /* 4 MiB heap */

typedef struct block {
    size_t        size;
    bool          free;
    struct block *next;
} block_t;

#define BLOCK_HDR sizeof(block_t)

static block_t *heap_head = NULL;
static size_t   heap_used_bytes = 0;

void heap_init(void) {
    heap_head = (block_t *)HEAP_START;
    heap_head->size = HEAP_SIZE - BLOCK_HDR;
    heap_head->free = true;
    heap_head->next = NULL;
    heap_used_bytes = 0;
}

static void split_block(block_t *b, size_t size) {
    if (b->size >= size + BLOCK_HDR + 16) {
        block_t *new = (block_t *)((uint8_t *)b + BLOCK_HDR + size);
        new->size = b->size - size - BLOCK_HDR;
        new->free = true;
        new->next = b->next;
        b->size = size;
        b->next = new;
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 7) & ~7;  /* 8-byte alignment */
    block_t *b = heap_head;
    while (b) {
        if (b->free && b->size >= size) {
            split_block(b, size);
            b->free = false;
            heap_used_bytes += b->size;
            return (void *)((uint8_t *)b + BLOCK_HDR);
        }
        b = b->next;
    }
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_t *b = (block_t *)((uint8_t *)ptr - BLOCK_HDR);
    b->free = true;
    heap_used_bytes -= b->size;

    /* coalesce adjacent free blocks */
    block_t *c = heap_head;
    while (c) {
        if (c->free && c->next && c->next->free) {
            c->size += c->next->size + BLOCK_HDR;
            c->next = c->next->next;
            continue;
        }
        c = c->next;
    }
}

size_t heap_used(void) { return heap_used_bytes; }
