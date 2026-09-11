#ifndef VFS_H
#define VFS_H
#include "../include/types.h"

#define FS_FILE       0x01
#define FS_DIRECTORY  0x02
#define FS_MOUNTPOINT 0x04
#define FS_CHARDEV    0x08
#define FS_BLOCKDEV   0x10
#define FS_SYMLINK    0x20

#define VFS_NAME_MAX 128
#define VFS_MAX_MOUNTS 16

struct vfs_node;

typedef struct dirent {
    char     name[VFS_NAME_MAX];
    uint32_t inode;
} dirent_t;

typedef uint32_t (*read_fn_t)(struct vfs_node *, uint32_t offset, uint32_t size, uint8_t *buf);
typedef uint32_t (*write_fn_t)(struct vfs_node *, uint32_t offset, uint32_t size, const uint8_t *buf);
typedef void     (*open_fn_t)(struct vfs_node *);
typedef void     (*close_fn_t)(struct vfs_node *);
typedef dirent_t *(*readdir_fn_t)(struct vfs_node *, uint32_t index);
typedef struct vfs_node *(*finddir_fn_t)(struct vfs_node *, const char *name);

typedef struct vfs_node {
    char        name[VFS_NAME_MAX];
    uint32_t    flags;
    uint32_t    inode;
    uint32_t    length;
    uint32_t    permissions;
    uint32_t    uid;
    uint32_t    gid;
    void       *impl;
    read_fn_t   read;
    write_fn_t  write;
    open_fn_t   open;
    close_fn_t  close;
    readdir_fn_t readdir;
    finddir_fn_t finddir;
    struct vfs_node *mount;
    struct vfs_node *parent;
} vfs_node_t;

typedef struct {
    char        path[VFS_NAME_MAX];
    vfs_node_t *root;
} vfs_mount_t;

void        vfs_init(void);
uint32_t    vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf);
uint32_t    vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf);
void        vfs_open(vfs_node_t *node);
void        vfs_close(vfs_node_t *node);
dirent_t   *vfs_readdir(vfs_node_t *node, uint32_t index);
vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name);
int         vfs_mount(const char *path, vfs_node_t *fs_root);
vfs_node_t *vfs_resolve(const char *path);
vfs_node_t *vfs_root(void);
int         vfs_mount_count(void);
vfs_mount_t *vfs_get_mount(int index);

#endif
