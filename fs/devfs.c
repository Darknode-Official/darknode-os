#include "devfs.h"
#include "ramfs.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../kernel/keyboard.h"
#include "../kernel/io.h"
#include "../kernel/serial.h"
#include "../drivers/rtc.h"

/* /dev/null — read returns 0, write discards */
static uint32_t null_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node; (void)offset; (void)size; (void)buf;
    return 0;
}
static uint32_t null_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf) {
    (void)node; (void)offset; (void)buf;
    return size;
}

/* /dev/zero — read returns zero bytes */
static uint32_t zero_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node; (void)offset;
    memset(buf, 0, size);
    return size;
}

/* /dev/random — xorshift32 PRNG */
static uint32_t rand_state = 0xDEADBEEF;
static uint32_t xorshift32(void) {
    rand_state ^= rand_state << 13;
    rand_state ^= rand_state >> 17;
    rand_state ^= rand_state << 5;
    return rand_state;
}
static uint32_t random_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++)
        buf[i] = (uint8_t)(xorshift32() & 0xFF);
    return size;
}

/* /dev/console — read from keyboard, write to screen */
static uint32_t console_dev_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node; (void)offset;
    uint32_t i = 0;
    while (i < size) {
        if (keyboard_has_key()) {
            buf[i++] = (uint8_t)keyboard_getchar();
        } else {
            break;
        }
    }
    return i;
}
static uint32_t console_dev_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++)
        console_putchar((char)buf[i]);
    return size;
}

/* /dev/serial — read/write COM1 */
static uint32_t serial_dev_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node; (void)offset;
    /* Non-blocking: read what's available from COM1 receive buffer */
    uint32_t i = 0;
    while (i < size) {
        uint8_t lsr = inb(COM1 + 5);
        if (!(lsr & 0x01)) break;
        buf[i++] = inb(COM1);
    }
    return i;
}
static uint32_t serial_dev_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++)
        serial_putchar((char)buf[i]);
    return size;
}

/* /dev/rtc — read returns date/time string */
static uint32_t rtc_dev_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    (void)node;
    uint8_t sec = rtc_read(0x00);
    uint8_t min = rtc_read(0x02);
    uint8_t hr  = rtc_read(0x04);
    uint8_t day = rtc_read(0x07);
    uint8_t mon = rtc_read(0x08);
    uint8_t yr  = rtc_read(0x09);
    /* BCD → binary */
    sec = (sec >> 4) * 10 + (sec & 0x0F);
    min = (min >> 4) * 10 + (min & 0x0F);
    hr  = (hr  >> 4) * 10 + (hr  & 0x0F);
    day = (day >> 4) * 10 + (day & 0x0F);
    mon = (mon >> 4) * 10 + (mon & 0x0F);
    yr  = (yr  >> 4) * 10 + (yr  & 0x0F);

    char tmp[32];
    char n[4];
    strcpy(tmp, "20");
    itoa(yr, n, 10); if (yr < 10) strcat(tmp, "0"); strcat(tmp, n);
    strcat(tmp, "-");
    itoa(mon, n, 10); if (mon < 10) strcat(tmp, "0"); strcat(tmp, n);
    strcat(tmp, "-");
    itoa(day, n, 10); if (day < 10) strcat(tmp, "0"); strcat(tmp, n);
    strcat(tmp, " ");
    itoa(hr, n, 10); if (hr < 10) strcat(tmp, "0"); strcat(tmp, n);
    strcat(tmp, ":");
    itoa(min, n, 10); if (min < 10) strcat(tmp, "0"); strcat(tmp, n);
    strcat(tmp, ":");
    itoa(sec, n, 10); if (sec < 10) strcat(tmp, "0"); strcat(tmp, n);
    strcat(tmp, " UTC\n");

    uint32_t len = strlen(tmp);
    if (offset >= len) return 0;
    uint32_t avail = len - offset;
    uint32_t count = (size < avail) ? size : avail;
    memcpy(buf, tmp + offset, count);
    return count;
}

void devfs_init(ramfs_entry_t *dev_dir) {
    if (!dev_dir) return;

    ramfs_entry_t *null_dev = ramfs_create(dev_dir, "null", FS_CHARDEV | FS_FILE);
    null_dev->node.read  = null_read;
    null_dev->node.write = null_write;

    ramfs_entry_t *zero_dev = ramfs_create(dev_dir, "zero", FS_CHARDEV | FS_FILE);
    zero_dev->node.read  = zero_read;
    zero_dev->node.write = null_write;

    ramfs_entry_t *rand_dev = ramfs_create(dev_dir, "random", FS_CHARDEV | FS_FILE);
    rand_dev->node.read  = random_read;
    rand_dev->node.write = null_write;

    ramfs_entry_t *con_dev = ramfs_create(dev_dir, "console", FS_CHARDEV | FS_FILE);
    con_dev->node.read  = console_dev_read;
    con_dev->node.write = console_dev_write;

    ramfs_entry_t *ser_dev = ramfs_create(dev_dir, "serial", FS_CHARDEV | FS_FILE);
    ser_dev->node.read  = serial_dev_read;
    ser_dev->node.write = serial_dev_write;

    ramfs_entry_t *rtc_d = ramfs_create(dev_dir, "rtc", FS_CHARDEV | FS_FILE);
    rtc_d->node.read = rtc_dev_read;
}
