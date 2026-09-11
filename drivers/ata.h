#ifndef ATA_H
#define ATA_H
#include "../include/types.h"

/* ATA I/O ports (primary controller) */
#define ATA_PRIMARY_DATA       0x1F0
#define ATA_PRIMARY_ERROR      0x1F1
#define ATA_PRIMARY_SECTORS    0x1F2
#define ATA_PRIMARY_LBA_LO     0x1F3
#define ATA_PRIMARY_LBA_MID    0x1F4
#define ATA_PRIMARY_LBA_HI     0x1F5
#define ATA_PRIMARY_DRIVE      0x1F6
#define ATA_PRIMARY_STATUS     0x1F7
#define ATA_PRIMARY_COMMAND    0x1F7
#define ATA_PRIMARY_CTRL       0x3F6

/* ATA I/O ports (secondary controller) */
#define ATA_SECONDARY_DATA     0x170
#define ATA_SECONDARY_ERROR    0x171
#define ATA_SECONDARY_SECTORS  0x172
#define ATA_SECONDARY_LBA_LO   0x173
#define ATA_SECONDARY_LBA_MID  0x174
#define ATA_SECONDARY_LBA_HI   0x175
#define ATA_SECONDARY_DRIVE    0x176
#define ATA_SECONDARY_STATUS   0x177
#define ATA_SECONDARY_COMMAND  0x177
#define ATA_SECONDARY_CTRL     0x376

/* Status register bits */
#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DF    0x20
#define ATA_SR_DSC   0x10
#define ATA_SR_DRQ   0x08
#define ATA_SR_CORR  0x04
#define ATA_SR_IDX   0x02
#define ATA_SR_ERR   0x01

/* Commands */
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_FLUSH       0xE7

/* Drive selectors */
#define ATA_MASTER  0x00
#define ATA_SLAVE   0x01

typedef struct {
    bool     present;
    uint16_t base;
    uint8_t  drive;
    char     model[41];
    uint32_t sectors;
    uint32_t size_mb;
} ata_drive_t;

void     ata_init(void);
int      ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t *buffer);
int      ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t *buffer);
uint32_t ata_drive_count(void);
ata_drive_t *ata_get_drive(uint8_t index);

#endif
