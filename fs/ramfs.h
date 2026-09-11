#ifndef RAMFS_H
#define RAMFS_H
#include "vfs.h"

#define RAMFS_MAX_CHILDREN 64
#define RAMFS_BLOCK_SIZE   4096
#define RAMFS_MAX_BLOCKS   256

typedef struct ramfs_entry {
    vfs_node_t           node;
    uint8_t             *data;
    uint32_t             data_cap;
    struct ramfs_entry  *children[RAMFS_MAX_CHILDREN];
    uint32_t             child_count;
    struct ramfs_entry  *parent;
} ramfs_entry_t;

vfs_node_t   *ramfs_init(void);
ramfs_entry_t *ramfs_create(ramfs_entry_t *parent, const char *name, uint32_t flags);
int            ramfs_delete(ramfs_entry_t *parent, const char *name);
int            ramfs_write_string(ramfs_entry_t *entry, const char *str);

#endif
