#include "ata.h"
#include "../kernel/io.h"
#include "../kernel/string.h"
#include "../kernel/console.h"
#include "../include/darknode.h"

#define MAX_DRIVES 4

static ata_drive_t drives[MAX_DRIVES];
static uint32_t drive_count = 0;

static void ata_delay400(uint16_t base) {
    /* Read alternate status 4 times for ~400ns delay */
    inb(base + 0x206); inb(base + 0x206);
    inb(base + 0x206); inb(base + 0x206);
}

static int ata_wait_bsy(uint16_t base) {
    int timeout = 100000;
    while ((inb(base + 7) & ATA_SR_BSY) && --timeout > 0);
    return timeout > 0 ? 0 : -1;
}

static int ata_wait_drq(uint16_t base) {
    int timeout = 100000;
    while (!(inb(base + 7) & ATA_SR_DRQ) && --timeout > 0) {
        if (inb(base + 7) & (ATA_SR_ERR | ATA_SR_DF)) return -1;
    }
    return timeout > 0 ? 0 : -1;
}

static void ata_identify(uint16_t base, uint8_t slave) {
    /* Select drive */
    outb(base + 6, 0xA0 | (slave << 4));
    ata_delay400(base);

    /* Zero out sector count and LBA registers */
    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);

    /* Send IDENTIFY */
    outb(base + 7, ATA_CMD_IDENTIFY);
    ata_delay400(base);

    uint8_t status = inb(base + 7);
    if (status == 0) return; /* drive doesn't exist */

    /* Wait for BSY to clear */
    if (ata_wait_bsy(base) < 0) return;

    /* Check for ATAPI (mid/hi != 0 means not ATA) */
    if (inb(base + 4) != 0 || inb(base + 5) != 0) return;

    /* Wait for DRQ or ERR */
    int timeout = 100000;
    while (timeout-- > 0) {
        status = inb(base + 7);
        if (status & ATA_SR_ERR) return;
        if (status & ATA_SR_DRQ) break;
    }
    if (timeout <= 0) return;

    /* Read 256 words of identification data */
    uint16_t ident[256];
    for (int i = 0; i < 256; i++)
        ident[i] = inw(base);

    /* Extract model string (words 27-46, byte-swapped) */
    ata_drive_t *drv = &drives[drive_count];
    drv->present = true;
    drv->base = base;
    drv->drive = slave;

    for (int i = 0; i < 20; i++) {
        drv->model[i * 2]     = (char)(ident[27 + i] >> 8);
        drv->model[i * 2 + 1] = (char)(ident[27 + i] & 0xFF);
    }
    drv->model[40] = '\0';
    /* Trim trailing spaces */
    int end = 39;
    while (end >= 0 && drv->model[end] == ' ') drv->model[end--] = '\0';

    /* Total sectors (28-bit LBA) — words 60-61 */
    drv->sectors = (uint32_t)ident[61] << 16 | (uint32_t)ident[60];
    drv->size_mb = drv->sectors / 2048; /* 512 bytes/sector, 2048 sectors/MiB */

    drive_count++;
}

void ata_init(void) {
    memset(drives, 0, sizeof(drives));
    drive_count = 0;

    /* Probe primary master/slave */
    ata_identify(ATA_PRIMARY_DATA, ATA_MASTER);
    ata_identify(ATA_PRIMARY_DATA, ATA_SLAVE);
    /* Probe secondary master/slave */
    ata_identify(ATA_SECONDARY_DATA, ATA_MASTER);
    ata_identify(ATA_SECONDARY_DATA, ATA_SLAVE);
}

int ata_read_sectors(uint8_t drive_idx, uint32_t lba, uint8_t count, uint8_t *buffer) {
    if (drive_idx >= drive_count || !drives[drive_idx].present) return -1;
    ata_drive_t *drv = &drives[drive_idx];
    uint16_t base = drv->base;

    if (ata_wait_bsy(base) < 0) return -1;

    /* Select drive + LBA bits 24-27 */
    outb(base + 6, 0xE0 | (drv->drive << 4) | ((lba >> 24) & 0x0F));
    ata_delay400(base);

    outb(base + 1, 0x00);                    /* features = 0 */
    outb(base + 2, count);                   /* sector count */
    outb(base + 3, (uint8_t)(lba & 0xFF));   /* LBA low */
    outb(base + 4, (uint8_t)((lba >> 8) & 0xFF));   /* LBA mid */
    outb(base + 5, (uint8_t)((lba >> 16) & 0xFF));  /* LBA high */
    outb(base + 7, ATA_CMD_READ_PIO);        /* command */

    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq(base) < 0) return -1;
        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            uint16_t w = inw(base);
            buffer[(s * 512) + i * 2]     = (uint8_t)(w & 0xFF);
            buffer[(s * 512) + i * 2 + 1] = (uint8_t)(w >> 8);
        }
        ata_delay400(base);
    }
    return 0;
}

int ata_write_sectors(uint8_t drive_idx, uint32_t lba, uint8_t count, const uint8_t *buffer) {
    if (drive_idx >= drive_count || !drives[drive_idx].present) return -1;
    ata_drive_t *drv = &drives[drive_idx];
    uint16_t base = drv->base;

    if (ata_wait_bsy(base) < 0) return -1;

    outb(base + 6, 0xE0 | (drv->drive << 4) | ((lba >> 24) & 0x0F));
    ata_delay400(base);

    outb(base + 1, 0x00);
    outb(base + 2, count);
    outb(base + 3, (uint8_t)(lba & 0xFF));
    outb(base + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(base + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(base + 7, ATA_CMD_WRITE_PIO);

    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq(base) < 0) return -1;
        for (int i = 0; i < 256; i++) {
            uint16_t w = (uint16_t)buffer[(s * 512) + i * 2] |
                         ((uint16_t)buffer[(s * 512) + i * 2 + 1] << 8);
            outw(base, w);
        }
        ata_delay400(base);
    }

    /* Flush cache */
    outb(base + 7, ATA_CMD_FLUSH);
    ata_wait_bsy(base);
    return 0;
}

uint32_t ata_drive_count(void) {
    return drive_count;
}

ata_drive_t *ata_get_drive(uint8_t index) {
    if (index >= drive_count) return NULL;
    return &drives[index];
}
