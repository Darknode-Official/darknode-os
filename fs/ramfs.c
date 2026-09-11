#include "ramfs.h"
#include "../kernel/string.h"
#include "../kernel/heap.h"
#include "../kernel/timer.h"
#include "../kernel/pmm.h"
#include "../include/darknode.h"

static uint32_t next_inode = 1;

static uint32_t ramfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    ramfs_entry_t *entry = (ramfs_entry_t *)node;
    if (!entry->data || offset >= node->length) return 0;
    uint32_t avail = node->length - offset;
    uint32_t count = (size < avail) ? size : avail;
    memcpy(buf, entry->data + offset, count);
    return count;
}

static uint32_t ramfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf) {
    ramfs_entry_t *entry = (ramfs_entry_t *)node;
    uint32_t needed = offset + size;

    if (needed > entry->data_cap) {
        uint32_t new_cap = entry->data_cap ? entry->data_cap : RAMFS_BLOCK_SIZE;
        while (new_cap < needed) new_cap *= 2;
        if (new_cap > RAMFS_MAX_BLOCKS * RAMFS_BLOCK_SIZE) return 0;

        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return 0;
        memset(new_data, 0, new_cap);
        if (entry->data) {
            memcpy(new_data, entry->data, node->length);
            kfree(entry->data);
        }
        entry->data = new_data;
        entry->data_cap = new_cap;
    }

    memcpy(entry->data + offset, buf, size);
    if (needed > node->length) node->length = needed;
    return size;
}

static dirent_t *ramfs_readdir(vfs_node_t *node, uint32_t index) {
    ramfs_entry_t *entry = (ramfs_entry_t *)node;
    if (index >= entry->child_count) return NULL;

    static dirent_t d;
    strncpy(d.name, entry->children[index]->node.name, VFS_NAME_MAX - 1);
    d.name[VFS_NAME_MAX - 1] = '\0';
    d.inode = entry->children[index]->node.inode;
    return &d;
}

static vfs_node_t *ramfs_finddir(vfs_node_t *node, const char *name) {
    ramfs_entry_t *entry = (ramfs_entry_t *)node;
    for (uint32_t i = 0; i < entry->child_count; i++) {
        if (strcmp(entry->children[i]->node.name, name) == 0)
            return &entry->children[i]->node;
    }
    return NULL;
}

ramfs_entry_t *ramfs_create(ramfs_entry_t *parent, const char *name, uint32_t flags) {
    if (!parent || parent->child_count >= RAMFS_MAX_CHILDREN) return NULL;

    ramfs_entry_t *entry = (ramfs_entry_t *)kmalloc(sizeof(ramfs_entry_t));
    if (!entry) return NULL;
    memset(entry, 0, sizeof(ramfs_entry_t));

    strncpy(entry->node.name, name, VFS_NAME_MAX - 1);
    entry->node.flags       = flags;
    entry->node.inode       = next_inode++;
    entry->node.length      = 0;
    entry->node.permissions = 0755;
    entry->node.impl        = entry;
    entry->node.parent      = &parent->node;
    entry->parent           = parent;
    entry->data             = NULL;
    entry->data_cap         = 0;
    entry->child_count      = 0;

    if (flags & FS_DIRECTORY) {
        entry->node.readdir  = ramfs_readdir;
        entry->node.finddir  = ramfs_finddir;
    }
    if (flags & FS_FILE) {
        entry->node.read  = ramfs_read;
        entry->node.write = ramfs_write;
    }

    parent->children[parent->child_count++] = entry;
    return entry;
}

int ramfs_delete(ramfs_entry_t *parent, const char *name) {
    if (!parent) return -1;
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->node.name, name) == 0) {
            ramfs_entry_t *victim = parent->children[i];
            if ((victim->node.flags & FS_DIRECTORY) && victim->child_count > 0)
                return -1;
            if (victim->data) kfree(victim->data);
            kfree(victim);
            for (uint32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->children[--parent->child_count] = NULL;
            return 0;
        }
    }
    return -1;
}

int ramfs_write_string(ramfs_entry_t *entry, const char *str) {
    if (!entry) return -1;
    uint32_t len = strlen(str);
    return (ramfs_write(&entry->node, 0, len, (const uint8_t *)str) == len) ? 0 : -1;
}

/* Virtual /proc file readers */
static uint32_t proc_uptime_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node;
    char tmp[64];
    uint32_t ticks = timer_get_ticks();
    uint32_t secs = ticks / 1000;
    uint32_t mins = secs / 60;
    uint32_t hours = mins / 60;
    itoa(hours, tmp, 10);
    strcat(tmp, "h ");
    char m[8]; itoa(mins % 60, m, 10); strcat(tmp, m); strcat(tmp, "m ");
    char s[8]; itoa(secs % 60, s, 10); strcat(tmp, s); strcat(tmp, "s\n");
    uint32_t len = strlen(tmp);
    if (offset >= len) return 0;
    uint32_t avail = len - offset;
    uint32_t count = (size < avail) ? size : avail;
    memcpy(buf, tmp + offset, count);
    return count;
}

static uint32_t proc_meminfo_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node;
    char tmp[256];
    char n[16];
    strcpy(tmp, "total_kb: "); utoa(pmm_total_memory(), n, 10); strcat(tmp, n); strcat(tmp, "\n");
    strcat(tmp, "used_pages: "); utoa(pmm_used_pages(), n, 10); strcat(tmp, n); strcat(tmp, "\n");
    strcat(tmp, "free_pages: "); utoa(pmm_free_pages(), n, 10); strcat(tmp, n); strcat(tmp, "\n");
    strcat(tmp, "page_size: "); utoa(PAGE_SIZE, n, 10); strcat(tmp, n); strcat(tmp, "\n");
    strcat(tmp, "heap_used: "); utoa((uint32_t)heap_used(), n, 10); strcat(tmp, n); strcat(tmp, "\n");
    uint32_t len = strlen(tmp);
    if (offset >= len) return 0;
    uint32_t avail = len - offset;
    uint32_t count = (size < avail) ? size : avail;
    memcpy(buf, tmp + offset, count);
    return count;
}

static uint32_t proc_cpuinfo_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node;
    char tmp[128];
    char vendor[13] = {0};
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(0));
    *(uint32_t *)&vendor[0] = b;
    *(uint32_t *)&vendor[4] = d;
    *(uint32_t *)&vendor[8] = c;
    strcpy(tmp, "vendor: "); strcat(tmp, vendor); strcat(tmp, "\n");
    uint32_t len = strlen(tmp);
    if (offset >= len) return 0;
    uint32_t avail = len - offset;
    uint32_t count = (size < avail) ? size : avail;
    memcpy(buf, tmp + offset, count);
    return count;
}

vfs_node_t *ramfs_init(void) {
    ramfs_entry_t *root = (ramfs_entry_t *)kmalloc(sizeof(ramfs_entry_t));
    memset(root, 0, sizeof(ramfs_entry_t));
    strcpy(root->node.name, "/");
    root->node.flags   = FS_DIRECTORY;
    root->node.inode   = next_inode++;
    root->node.readdir = ramfs_readdir;
    root->node.finddir = ramfs_finddir;

    ramfs_entry_t *dev  = ramfs_create(root, "dev",  FS_DIRECTORY);
    ramfs_entry_t *tmp  = ramfs_create(root, "tmp",  FS_DIRECTORY);
    ramfs_entry_t *proc = ramfs_create(root, "proc", FS_DIRECTORY);
    ramfs_entry_t *etc  = ramfs_create(root, "etc",  FS_DIRECTORY);
    (void)dev; (void)tmp;

    /* /etc files */
    ramfs_entry_t *hostname = ramfs_create(etc, "hostname", FS_FILE);
    ramfs_write_string(hostname, "darknode\n");
    ramfs_entry_t *version = ramfs_create(etc, "version", FS_FILE);
    ramfs_write_string(version, DARKNODE_VERSION "\n");
    ramfs_entry_t *motd = ramfs_create(etc, "motd", FS_FILE);
    ramfs_write_string(motd, "Welcome to Darknode OS — the operator's own kernel.\n");

    /* /proc virtual files */
    ramfs_entry_t *uptime_f = ramfs_create(proc, "uptime", FS_FILE);
    uptime_f->node.read = proc_uptime_read;

    ramfs_entry_t *meminfo_f = ramfs_create(proc, "meminfo", FS_FILE);
    meminfo_f->node.read = proc_meminfo_read;

    ramfs_entry_t *cpuinfo_f = ramfs_create(proc, "cpuinfo", FS_FILE);
    cpuinfo_f->node.read = proc_cpuinfo_read;

    return &root->node;
}
