#ifndef DEVFS_H
#define DEVFS_H
#include "vfs.h"
#include "ramfs.h"

void devfs_init(ramfs_entry_t *dev_dir);

#endif
