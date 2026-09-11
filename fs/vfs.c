#include "vfs.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../include/darknode.h"

static vfs_node_t *root_node = NULL;
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int mount_count = 0;

void vfs_init(void) {
    root_node = NULL;
    mount_count = 0;
    memset(mounts, 0, sizeof(mounts));
}

uint32_t vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    if (!node) return 0;
    vfs_node_t *target = (node->mount) ? node->mount : node;
    if (target->read)
        return target->read(target, offset, size, buf);
    return 0;
}

uint32_t vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf) {
    if (!node) return 0;
    vfs_node_t *target = (node->mount) ? node->mount : node;
    if (target->write)
        return target->write(target, offset, size, buf);
    return 0;
}

void vfs_open(vfs_node_t *node) {
    if (!node) return;
    vfs_node_t *target = (node->mount) ? node->mount : node;
    if (target->open)
        target->open(target);
}

void vfs_close(vfs_node_t *node) {
    if (!node) return;
    vfs_node_t *target = (node->mount) ? node->mount : node;
    if (target->close)
        target->close(target);
}

dirent_t *vfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!node) return NULL;
    vfs_node_t *target = (node->mount) ? node->mount : node;
    if ((target->flags & FS_DIRECTORY) && target->readdir)
        return target->readdir(target, index);
    return NULL;
}

vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name) {
    if (!node || !name) return NULL;
    vfs_node_t *target = (node->mount) ? node->mount : node;
    if ((target->flags & FS_DIRECTORY) && target->finddir)
        return target->finddir(target, name);
    return NULL;
}

int vfs_mount(const char *path, vfs_node_t *fs_root) {
    if (!path || !fs_root || mount_count >= VFS_MAX_MOUNTS)
        return -1;

    if (strcmp(path, "/") == 0) {
        root_node = fs_root;
        strncpy(mounts[mount_count].path, path, VFS_NAME_MAX - 1);
        mounts[mount_count].root = fs_root;
        mount_count++;
        return 0;
    }

    vfs_node_t *mount_point = vfs_resolve(path);
    if (!mount_point || !(mount_point->flags & FS_DIRECTORY))
        return -1;

    mount_point->mount = fs_root;
    mount_point->flags |= FS_MOUNTPOINT;
    strncpy(mounts[mount_count].path, path, VFS_NAME_MAX - 1);
    mounts[mount_count].root = fs_root;
    mount_count++;
    return 0;
}

vfs_node_t *vfs_resolve(const char *path) {
    if (!path || !root_node) return NULL;
    if (strcmp(path, "/") == 0) return root_node;

    vfs_node_t *current = root_node;
    char buf[VFS_NAME_MAX];
    strncpy(buf, path, VFS_NAME_MAX - 1);
    buf[VFS_NAME_MAX - 1] = '\0';

    char *token = buf;
    if (*token == '/') token++;

    while (*token) {
        char *end = token;
        while (*end && *end != '/') end++;
        char saved = *end;
        *end = '\0';

        if (strlen(token) == 0) {
            if (saved == '\0') break;
            token = end + 1;
            continue;
        }

        vfs_node_t *child = vfs_finddir(current, token);
        if (!child) return NULL;

        if (child->mount)
            child = child->mount;

        current = child;
        if (saved == '\0') break;
        token = end + 1;
    }

    return current;
}

vfs_node_t *vfs_root(void) {
    return root_node;
}

int vfs_mount_count(void) {
    return mount_count;
}

vfs_mount_t *vfs_get_mount(int index) {
    if (index < 0 || index >= mount_count) return NULL;
    return &mounts[index];
}
