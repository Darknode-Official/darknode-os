#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "string.h"
#include "timer.h"
#include "pmm.h"
#include "heap.h"
#include "io.h"
#include "process.h"
#include "../include/darknode.h"
#include "../drivers/rtc.h"
#include "../drivers/ata.h"
#include "../drivers/ne2000.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "../net/ethernet.h"
#include "../net/arp.h"
#include "../net/ipv4.h"
#include "../net/icmp.h"
#include "../net/udp.h"
#include "../net/dhcp.h"
#include "../net/dns.h"

#define CMD_BUF 256
#define READ_BUF 4096

static char cmd_buf[CMD_BUF];
static int cmd_len = 0;
static char cwd[VFS_NAME_MAX] = "/";

static void prompt(void) {
    console_write_color("darknode", DN_COLOR_ACCENT);
    console_write_color("@", DN_COLOR_DIM);
    console_write_color("kernel", DN_COLOR_OK);
    console_write_color(":", DN_COLOR_DIM);
    console_write_color(cwd, DN_COLOR_HEADER);
    console_write_color(" $ ", DN_COLOR_DIM);
}

static void cmd_help(void) {
    console_write_color("Commands:\n", DN_COLOR_HEADER);
    console_write_color("  help      ", DN_COLOR_ACCENT); console_write("show this help\n");
    console_write_color("  clear     ", DN_COLOR_ACCENT); console_write("clear the screen\n");
    console_write_color("  version   ", DN_COLOR_ACCENT); console_write("show OS version\n");
    console_write_color("  time      ", DN_COLOR_ACCENT); console_write("show current time (RTC)\n");
    console_write_color("  uptime    ", DN_COLOR_ACCENT); console_write("show system uptime\n");
    console_write_color("  mem       ", DN_COLOR_ACCENT); console_write("show memory usage\n");
    console_write_color("  free      ", DN_COLOR_ACCENT); console_write("show memory (physical + heap)\n");
    console_write_color("  cpuinfo   ", DN_COLOR_ACCENT); console_write("show CPU information\n");
    console_write_color("  echo      ", DN_COLOR_ACCENT); console_write("echo text back\n");
    console_write_color("  ls [path] ", DN_COLOR_ACCENT); console_write("list directory contents\n");
    console_write_color("  cat <path>", DN_COLOR_ACCENT); console_write("read file contents\n");
    console_write_color("  mkdir <p> ", DN_COLOR_ACCENT); console_write("create a directory\n");
    console_write_color("  touch <p> ", DN_COLOR_ACCENT); console_write("create an empty file\n");
    console_write_color("  write <p> ", DN_COLOR_ACCENT); console_write("write text to a file\n");
    console_write_color("  rm <path> ", DN_COLOR_ACCENT); console_write("remove a file\n");
    console_write_color("  cd <path> ", DN_COLOR_ACCENT); console_write("change directory\n");
    console_write_color("  pwd       ", DN_COLOR_ACCENT); console_write("print working directory\n");
    console_write_color("  ps        ", DN_COLOR_ACCENT); console_write("list processes\n");
    console_write_color("  mount     ", DN_COLOR_ACCENT); console_write("list mounted filesystems\n");
    console_write_color("  disk      ", DN_COLOR_ACCENT); console_write("show detected ATA drives\n");
    console_write_color("  ifconfig  ", DN_COLOR_ACCENT); console_write("show network configuration\n");
    console_write_color("  ping <ip> ", DN_COLOR_ACCENT); console_write("ICMP ping (4 packets)\n");
    console_write_color("  arp       ", DN_COLOR_ACCENT); console_write("show ARP cache\n");
    console_write_color("  dhcp      ", DN_COLOR_ACCENT); console_write("request IP via DHCP\n");
    console_write_color("  dns <host>", DN_COLOR_ACCENT); console_write("resolve hostname to IP\n");
    console_write_color("  netstat   ", DN_COLOR_ACCENT); console_write("show UDP bound ports\n");
    console_write_color("  reboot    ", DN_COLOR_ACCENT); console_write("reboot the system\n");
    console_write_color("  shutdown  ", DN_COLOR_ACCENT); console_write("halt the system\n");
}

static void cmd_version(void) {
    console_write_color(DARKNODE_NAME, DN_COLOR_ACCENT);
    console_write(" version ");
    console_write_color(DARKNODE_VERSION, DN_COLOR_OK);
    console_write(" (");
    console_write(DARKNODE_YEAR);
    console_write(") by ");
    console_write(DARKNODE_AUTHOR);
    console_write("\n");
}

static void cmd_mem(void) {
    console_write("Total:   ");
    console_write_dec(pmm_total_memory());
    console_write(" KiB\n");
    console_write("Pages:   ");
    console_write_dec(pmm_used_pages());
    console_write(" used / ");
    console_write_dec(pmm_free_pages());
    console_write(" free (");
    console_write_dec(PAGE_SIZE / 1024);
    console_write(" KiB each)\n");
    console_write("Heap:    ");
    console_write_dec((uint32_t)heap_used());
    console_write(" bytes used\n");
}

static void cmd_free(void) {
    uint32_t total = pmm_total_memory();
    uint32_t used_kb = pmm_used_pages() * (PAGE_SIZE / 1024);
    uint32_t free_kb = pmm_free_pages() * (PAGE_SIZE / 1024);
    console_write_color("              total       used       free\n", DN_COLOR_DIM);
    console_write("Physical:  ");
    console_write_dec(total); console_write(" KiB  ");
    console_write_dec(used_kb); console_write(" KiB  ");
    console_write_dec(free_kb); console_write(" KiB\n");
    console_write("Heap:      ");
    console_write_dec((uint32_t)heap_used()); console_write(" bytes used\n");
}

static void cmd_cpuinfo(void) {
    char vendor[13] = {0};
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(0));
    *(uint32_t *)&vendor[0] = b;
    *(uint32_t *)&vendor[4] = d;
    *(uint32_t *)&vendor[8] = c;
    console_write("Vendor: ");
    console_write_color(vendor, DN_COLOR_ACCENT);
    console_write("\n");

    __asm__ volatile("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(1));
    uint32_t family = (a >> 8) & 0xF;
    uint32_t model  = (a >> 4) & 0xF;
    uint32_t stepping = a & 0xF;
    console_write("Family: "); console_write_dec(family);
    console_write("  Model: "); console_write_dec(model);
    console_write("  Stepping: "); console_write_dec(stepping);
    console_write("\n");
    console_write("Features: ");
    if (d & (1 << 0))  console_write("FPU ");
    if (d & (1 << 4))  console_write("TSC ");
    if (d & (1 << 5))  console_write("MSR ");
    if (d & (1 << 9))  console_write("APIC ");
    if (d & (1 << 15)) console_write("CMOV ");
    if (d & (1 << 23)) console_write("MMX ");
    if (d & (1 << 25)) console_write("SSE ");
    if (d & (1 << 26)) console_write("SSE2 ");
    if (c & (1 << 0))  console_write("SSE3 ");
    if (c & (1 << 19)) console_write("SSE4.1 ");
    if (c & (1 << 20)) console_write("SSE4.2 ");
    if (c & (1 << 28)) console_write("AVX ");
    console_write("\n");
}

static void cmd_uptime(void) {
    uint32_t secs = timer_get_ticks() / 1000;
    uint32_t mins = secs / 60;
    secs %= 60;
    console_write("Uptime: ");
    console_write_dec(mins);
    console_write("m ");
    console_write_dec(secs);
    console_write("s\n");
}

static void cmd_time(void) {
    uint8_t h = rtc_read(0x04);
    uint8_t m = rtc_read(0x02);
    uint8_t s = rtc_read(0x00);
    h = (h >> 4) * 10 + (h & 0x0F);
    m = (m >> 4) * 10 + (m & 0x0F);
    s = (s >> 4) * 10 + (s & 0x0F);
    char buf[9];
    buf[0] = '0' + h / 10; buf[1] = '0' + h % 10; buf[2] = ':';
    buf[3] = '0' + m / 10; buf[4] = '0' + m % 10; buf[5] = ':';
    buf[6] = '0' + s / 10; buf[7] = '0' + s % 10; buf[8] = '\0';
    console_write("Time: ");
    console_write_color(buf, DN_COLOR_ACCENT);
    console_write(" (UTC)\n");
}

NORETURN static void cmd_reboot(void) {
    console_write_color("Rebooting...\n", DN_COLOR_WARN);
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    __asm__ volatile("cli; hlt");
    __builtin_unreachable();
}

NORETURN static void cmd_shutdown(void) {
    console_write_color("Halting system.\n", DN_COLOR_WARN);
    outw(0x604, 0x2000);
    __asm__ volatile("cli; hlt");
    __builtin_unreachable();
}

static void cmd_echo(const char *args) {
    console_write(args);
    console_write("\n");
}

/* Resolve a path argument relative to cwd */
static vfs_node_t *resolve_path(const char *path) {
    if (!path || !*path) return vfs_resolve(cwd);
    if (path[0] == '/') return vfs_resolve(path);

    char full[VFS_NAME_MAX];
    strncpy(full, cwd, VFS_NAME_MAX - 1);
    if (full[strlen(full) - 1] != '/') strcat(full, "/");
    strcat(full, path);
    return vfs_resolve(full);
}

static void build_full_path(const char *path, char *out) {
    if (path[0] == '/') {
        strncpy(out, path, VFS_NAME_MAX - 1);
    } else {
        strncpy(out, cwd, VFS_NAME_MAX - 1);
        if (out[strlen(out) - 1] != '/') strcat(out, "/");
        strcat(out, path);
    }
}

static void cmd_ls(const char *path) {
    vfs_node_t *dir = resolve_path(path);
    if (!dir) {
        console_write_color("ls: ", DN_COLOR_ERR);
        console_write("no such directory: ");
        console_write(path && *path ? path : cwd);
        console_write("\n");
        return;
    }
    if (!(dir->flags & FS_DIRECTORY)) {
        console_write(dir->name);
        console_write("  ");
        console_write_dec(dir->length);
        console_write(" bytes\n");
        return;
    }

    uint32_t i = 0;
    dirent_t *entry;
    while ((entry = vfs_readdir(dir, i)) != NULL) {
        vfs_node_t *child = vfs_finddir(dir, entry->name);
        if (child && (child->flags & FS_DIRECTORY)) {
            console_write_color(entry->name, DN_COLOR_ACCENT);
            console_write("/");
        } else if (child && (child->flags & FS_CHARDEV)) {
            console_write_color(entry->name, DN_COLOR_WARN);
        } else {
            console_write(entry->name);
        }
        if (child) {
            console_write_color("  ", DN_COLOR_DIM);
            console_write_dec(child->length);
        }
        console_write("\n");
        i++;
    }
    if (i == 0) {
        console_write_color("  (empty)\n", DN_COLOR_DIM);
    }
}

static void cmd_cat(const char *path) {
    if (!path || !*path) {
        console_write_color("cat: ", DN_COLOR_ERR);
        console_write("usage: cat <path>\n");
        return;
    }
    vfs_node_t *node = resolve_path(path);
    if (!node) {
        console_write_color("cat: ", DN_COLOR_ERR);
        console_write("file not found: ");
        console_write(path);
        console_write("\n");
        return;
    }
    if (node->flags & FS_DIRECTORY) {
        console_write_color("cat: ", DN_COLOR_ERR);
        console_write("is a directory\n");
        return;
    }

    uint8_t buf[READ_BUF];
    uint32_t offset = 0;
    uint32_t read;
    while ((read = vfs_read(node, offset, sizeof(buf) - 1, buf)) > 0) {
        buf[read] = '\0';
        console_write((const char *)buf);
        offset += read;
        if (read < sizeof(buf) - 1) break;
    }
}

static void cmd_mkdir(const char *path) {
    if (!path || !*path) {
        console_write_color("mkdir: ", DN_COLOR_ERR);
        console_write("usage: mkdir <path>\n");
        return;
    }

    /* Find parent directory and new name */
    char full[VFS_NAME_MAX];
    build_full_path(path, full);

    /* Split into parent path and leaf name */
    char *last_slash = full;
    for (char *p = full; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    char name[VFS_NAME_MAX];
    strcpy(name, last_slash + 1);
    if (last_slash == full) {
        /* Creating in root */
    } else {
        *last_slash = '\0';
    }

    vfs_node_t *parent = vfs_resolve(last_slash == full ? "/" : full);
    if (!parent || !(parent->flags & FS_DIRECTORY)) {
        console_write_color("mkdir: ", DN_COLOR_ERR);
        console_write("parent not found\n");
        return;
    }

    ramfs_entry_t *p = (ramfs_entry_t *)parent;
    if (ramfs_create(p, name, FS_DIRECTORY)) {
        console_write_color("  created: ", DN_COLOR_OK);
        console_write(name);
        console_write("/\n");
    } else {
        console_write_color("mkdir: ", DN_COLOR_ERR);
        console_write("failed\n");
    }
}

static void cmd_touch(const char *path) {
    if (!path || !*path) {
        console_write_color("touch: ", DN_COLOR_ERR);
        console_write("usage: touch <path>\n");
        return;
    }

    char full[VFS_NAME_MAX];
    build_full_path(path, full);

    char *last_slash = full;
    for (char *p = full; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    char name[VFS_NAME_MAX];
    strcpy(name, last_slash + 1);
    if (last_slash != full) *last_slash = '\0';

    vfs_node_t *parent = vfs_resolve(last_slash == full ? "/" : full);
    if (!parent) {
        console_write_color("touch: ", DN_COLOR_ERR);
        console_write("parent not found\n");
        return;
    }

    ramfs_entry_t *p = (ramfs_entry_t *)parent;
    if (ramfs_create(p, name, FS_FILE)) {
        console_write_color("  created: ", DN_COLOR_OK);
        console_write(name);
        console_write("\n");
    } else {
        console_write_color("touch: ", DN_COLOR_ERR);
        console_write("failed\n");
    }
}

static void cmd_write_file(const char *args) {
    if (!args || !*args) {
        console_write_color("write: ", DN_COLOR_ERR);
        console_write("usage: write <path> <content>\n");
        return;
    }

    /* Split into path and content at first space */
    char path[VFS_NAME_MAX];
    const char *content = args;
    int i = 0;
    while (*content && *content != ' ' && i < (int)sizeof(path) - 1) {
        path[i++] = *content++;
    }
    path[i] = '\0';
    if (*content == ' ') content++;

    vfs_node_t *node = resolve_path(path);
    if (!node) {
        /* Create the file first */
        cmd_touch(path);
        node = resolve_path(path);
    }
    if (!node) {
        console_write_color("write: ", DN_COLOR_ERR);
        console_write("cannot create file\n");
        return;
    }

    uint32_t len = strlen(content);
    uint32_t written = vfs_write(node, 0, len, (const uint8_t *)content);
    /* Append newline */
    uint8_t nl = '\n';
    vfs_write(node, written, 1, &nl);

    console_write_color("  wrote ", DN_COLOR_OK);
    console_write_dec(written + 1);
    console_write(" bytes\n");
}

static void cmd_rm(const char *path) {
    if (!path || !*path) {
        console_write_color("rm: ", DN_COLOR_ERR);
        console_write("usage: rm <path>\n");
        return;
    }

    vfs_node_t *node = resolve_path(path);
    if (!node) {
        console_write_color("rm: ", DN_COLOR_ERR);
        console_write("file not found\n");
        return;
    }
    if (!node->parent) {
        console_write_color("rm: ", DN_COLOR_ERR);
        console_write("cannot remove root\n");
        return;
    }

    ramfs_entry_t *parent = (ramfs_entry_t *)node->parent;
    if (ramfs_delete(parent, node->name) == 0) {
        console_write_color("  removed: ", DN_COLOR_OK);
        console_write(path);
        console_write("\n");
    } else {
        console_write_color("rm: ", DN_COLOR_ERR);
        console_write("failed (directory not empty?)\n");
    }
}

static void cmd_cd(const char *path) {
    if (!path || !*path || strcmp(path, "/") == 0) {
        strcpy(cwd, "/");
        return;
    }
    if (strcmp(path, "..") == 0) {
        /* Go up one level */
        int len = strlen(cwd);
        if (len <= 1) return;
        /* Remove trailing slash if present */
        if (cwd[len - 1] == '/' && len > 1) cwd[--len] = '\0';
        /* Find last slash */
        while (len > 0 && cwd[len - 1] != '/') len--;
        if (len == 0) len = 1;
        cwd[len] = '\0';
        return;
    }

    vfs_node_t *dir = resolve_path(path);
    if (!dir || !(dir->flags & FS_DIRECTORY)) {
        console_write_color("cd: ", DN_COLOR_ERR);
        console_write("not a directory: ");
        console_write(path);
        console_write("\n");
        return;
    }

    build_full_path(path, cwd);
    /* Ensure trailing slash for root only */
    int len = strlen(cwd);
    if (len > 1 && cwd[len - 1] == '/') cwd[len - 1] = '\0';
}

static void cmd_ps(void) {
    console_write_color("PID  STATE    NAME\n", DN_COLOR_HEADER);
    const char *states[] = {"CREATED", "READY", "RUNNING", "BLOCKED", "ZOMBIE"};
    process_t *head = process_list();
    if (!head) {
        console_write_color("  (no processes)\n", DN_COLOR_DIM);
        return;
    }
    process_t *p = head;
    do {
        char pid_s[8];
        itoa(p->pid, pid_s, 10);
        console_write("  ");
        console_write(pid_s);
        /* Pad to column */
        for (int i = strlen(pid_s); i < 5; i++) console_write(" ");
        if (p->state == PROC_RUNNING)
            console_write_color(states[p->state], DN_COLOR_OK);
        else
            console_write(states[p->state]);
        for (int i = strlen(states[p->state]); i < 9; i++) console_write(" ");
        console_write(p->name);
        console_write("\n");
        p = p->next;
    } while (p != head);
}

static void cmd_mount(void) {
    console_write_color("Mounted filesystems:\n", DN_COLOR_HEADER);
    int count = vfs_mount_count();
    for (int i = 0; i < count; i++) {
        vfs_mount_t *m = vfs_get_mount(i);
        if (m) {
            console_write("  ");
            console_write_color(m->path, DN_COLOR_ACCENT);
            console_write("  → ");
            console_write(m->root->name);
            console_write("\n");
        }
    }
    if (count == 0) {
        console_write_color("  (none)\n", DN_COLOR_DIM);
    }
}

static void cmd_disk(void) {
    uint32_t count = ata_drive_count();
    if (count == 0) {
        console_write_color("  No ATA drives detected.\n", DN_COLOR_DIM);
        return;
    }
    console_write_color("ATA drives:\n", DN_COLOR_HEADER);
    for (uint32_t i = 0; i < count; i++) {
        ata_drive_t *d = ata_get_drive(i);
        if (!d || !d->present) continue;
        console_write("  ");
        console_write_color(d->model, DN_COLOR_ACCENT);
        console_write("  ");
        console_write_dec(d->size_mb);
        console_write(" MiB  (");
        console_write_dec(d->sectors);
        console_write(" sectors)\n");
    }
}

static void execute(const char *cmd) {
    if (!*cmd) return;

    /* Split command and arguments */
    const char *args = cmd;
    while (*args && *args != ' ') args++;
    int cmd_word_len = args - cmd;
    if (*args == ' ') args++;
    else args = "";

    if (strncmp(cmd, "help", cmd_word_len) == 0 && cmd_word_len == 4) cmd_help();
    else if (strncmp(cmd, "clear", cmd_word_len) == 0 && cmd_word_len == 5) console_clear();
    else if (strncmp(cmd, "version", cmd_word_len) == 0 && cmd_word_len == 7) cmd_version();
    else if (strncmp(cmd, "time", cmd_word_len) == 0 && cmd_word_len == 4) cmd_time();
    else if (strncmp(cmd, "uptime", cmd_word_len) == 0 && cmd_word_len == 6) cmd_uptime();
    else if (strncmp(cmd, "mem", cmd_word_len) == 0 && cmd_word_len == 3) cmd_mem();
    else if (strncmp(cmd, "free", cmd_word_len) == 0 && cmd_word_len == 4) cmd_free();
    else if (strncmp(cmd, "cpuinfo", cmd_word_len) == 0 && cmd_word_len == 7) cmd_cpuinfo();
    else if (strncmp(cmd, "reboot", cmd_word_len) == 0 && cmd_word_len == 6) cmd_reboot();
    else if (strncmp(cmd, "shutdown", cmd_word_len) == 0 && cmd_word_len == 8) cmd_shutdown();
    else if (strncmp(cmd, "echo", cmd_word_len) == 0 && cmd_word_len == 4) cmd_echo(args);
    else if (strncmp(cmd, "ls", cmd_word_len) == 0 && cmd_word_len == 2) cmd_ls(args);
    else if (strncmp(cmd, "cat", cmd_word_len) == 0 && cmd_word_len == 3) cmd_cat(args);
    else if (strncmp(cmd, "mkdir", cmd_word_len) == 0 && cmd_word_len == 5) cmd_mkdir(args);
    else if (strncmp(cmd, "touch", cmd_word_len) == 0 && cmd_word_len == 5) cmd_touch(args);
    else if (strncmp(cmd, "write", cmd_word_len) == 0 && cmd_word_len == 5) cmd_write_file(args);
    else if (strncmp(cmd, "rm", cmd_word_len) == 0 && cmd_word_len == 2) cmd_rm(args);
    else if (strncmp(cmd, "cd", cmd_word_len) == 0 && cmd_word_len == 2) cmd_cd(args);
    else if (strncmp(cmd, "pwd", cmd_word_len) == 0 && cmd_word_len == 3) { console_write(cwd); console_write("\n"); }
    else if (strncmp(cmd, "ps", cmd_word_len) == 0 && cmd_word_len == 2) cmd_ps();
    else if (strncmp(cmd, "mount", cmd_word_len) == 0 && cmd_word_len == 5) cmd_mount();
    else if (strncmp(cmd, "disk", cmd_word_len) == 0 && cmd_word_len == 4) cmd_disk();
    else if (strncmp(cmd, "ifconfig", cmd_word_len) == 0 && cmd_word_len == 8) {
        uint8_t mac[6]; eth_get_mac(mac);
        char ipbuf[20];
        console_write_color("Network Interface:\n", DN_COLOR_HEADER);
        console_write("  MAC:     ");
        for (int i = 0; i < 6; i++) { console_write_hex(mac[i]); if (i < 5) console_write(":"); }
        console_write("\n");
        ip_format(ipv4_get_ip(), ipbuf);
        console_write("  IP:      "); console_write_color(ipbuf, DN_COLOR_ACCENT); console_write("\n");
        ip_format(ipv4_get_subnet(), ipbuf);
        console_write("  Subnet:  "); console_write(ipbuf); console_write("\n");
        ip_format(ipv4_get_gateway(), ipbuf);
        console_write("  Gateway: "); console_write(ipbuf); console_write("\n");
        ip_format(dhcp_get_dns(), ipbuf);
        console_write("  DNS:     "); console_write(ipbuf); console_write("\n");
        console_write("  NIC:     "); console_write(ne2000_available() ? "NE2000 (up)" : "not detected"); console_write("\n");
    }
    else if (strncmp(cmd, "ping", cmd_word_len) == 0 && cmd_word_len == 4) {
        if (!*args) { console_write_color("usage: ping <ip>\n", DN_COLOR_DIM); }
        else {
            uint8_t a=0,b=0,c=0,d=0; int p=0;
            const char *s = args;
            while (*s >= '0' && *s <= '9') { a = a*10+(*s-'0'); s++; } if (*s=='.') s++;
            while (*s >= '0' && *s <= '9') { b = b*10+(*s-'0'); s++; } if (*s=='.') s++;
            while (*s >= '0' && *s <= '9') { c = c*10+(*s-'0'); s++; } if (*s=='.') s++;
            while (*s >= '0' && *s <= '9') { d = d*10+(*s-'0'); s++; }
            (void)p;
            icmp_ping(ip_make(a,b,c,d), 4);
        }
    }
    else if (strncmp(cmd, "arp", cmd_word_len) == 0 && cmd_word_len == 3) { arp_cache_dump(); }
    else if (strncmp(cmd, "dhcp", cmd_word_len) == 0 && cmd_word_len == 4) {
        console_write_color("Requesting IP via DHCP...\n", DN_COLOR_DIM);
        if (dhcp_discover() == 0) console_write_color("DHCP complete.\n", DN_COLOR_OK);
        else console_write_color("DHCP failed — no response.\n", DN_COLOR_ERR);
    }
    else if (strncmp(cmd, "dns", cmd_word_len) == 0 && cmd_word_len == 3) {
        if (!*args) { console_write_color("usage: dns <hostname>\n", DN_COLOR_DIM); }
        else {
            console_write("Resolving "); console_write(args); console_write("...\n");
            uint32_t ip = dns_resolve(args);
            if (ip) { char buf[20]; ip_format(ip, buf); console_write_color("  ", DN_COLOR_OK); console_write(buf); console_write("\n"); }
            else console_write_color("  resolution failed\n", DN_COLOR_ERR);
        }
    }
    else if (strncmp(cmd, "netstat", cmd_word_len) == 0 && cmd_word_len == 7) { udp_dump_sockets(); }
    else {
        console_write_color("unknown command: ", DN_COLOR_ERR);
        console_write(cmd);
        console_write_color("  (type 'help')\n", DN_COLOR_DIM);
    }
}

void shell_init(void) {
    cmd_len = 0;
    memset(cmd_buf, 0, CMD_BUF);
    strcpy(cwd, "/");
}

void shell_run(void) {
    prompt();
    while (1) {
        char c = keyboard_getchar_blocking();
        if (c == '\n') {
            console_putchar('\n');
            cmd_buf[cmd_len] = '\0';
            execute(cmd_buf);
            cmd_len = 0;
            memset(cmd_buf, 0, CMD_BUF);
            prompt();
        } else if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                console_backspace();
            }
        } else if (c == '\t') {
            /* Tab completion placeholder */
        } else if (cmd_len < CMD_BUF - 1) {
            cmd_buf[cmd_len++] = c;
            console_putchar(c);
        }
    }
}
