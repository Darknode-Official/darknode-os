#include "ahci.h"
#include "pci.h"
#include "../kernel/io.h"
#include "../kernel/heap.h"
#include "../kernel/string.h"
#include "../kernel/console.h"

static volatile uint32_t *abar = 0;
static ahci_port_t ports[AHCI_MAX_PORTS];
static int num_drives = 0;

static inline uint32_t ahci_reg(uint32_t off) {
    return abar[off / 4];
}
static inline void ahci_reg_set(uint32_t off, uint32_t val) {
    abar[off / 4] = val;
}
static inline volatile uint32_t *port_regs(int p) {
    return (volatile uint32_t *)((uint8_t *)abar + 0x100 + p * 0x80);
}
static inline uint32_t port_reg(int p, uint32_t off) {
    return port_regs(p)[off / 4];
}
static inline void port_reg_set(int p, uint32_t off, uint32_t val) {
    port_regs(p)[off / 4] = val;
}

static void port_stop(int p) {
    uint32_t cmd = port_reg(p, PORT_CMD);
    cmd &= ~PORT_CMD_ST;
    cmd &= ~PORT_CMD_FRE;
    port_reg_set(p, PORT_CMD, cmd);
    /* wait for CR and FR to clear */
    for (int i = 0; i < 500000; i++) {
        uint32_t c = port_reg(p, PORT_CMD);
        if (!(c & PORT_CMD_CR) && !(c & PORT_CMD_FR)) return;
    }
}

static void port_start(int p) {
    /* wait until not busy */
    for (int i = 0; i < 500000; i++) {
        if (!(port_reg(p, PORT_CMD) & PORT_CMD_CR)) break;
    }
    uint32_t cmd = port_reg(p, PORT_CMD);
    cmd |= PORT_CMD_FRE;
    cmd |= PORT_CMD_ST;
    port_reg_set(p, PORT_CMD, cmd);
}

/* Allocate aligned memory (power-of-2 alignment, simple bump from kmalloc) */
static void *alloc_aligned(size_t size, size_t align) {
    /* over-allocate and align manually */
    uint8_t *raw = (uint8_t *)kmalloc(size + align);
    if (!raw) return 0;
    memset(raw, 0, size + align);
    uint32_t addr = (uint32_t)raw;
    uint32_t aligned = (addr + align - 1) & ~(align - 1);
    return (void *)aligned;
}

static int port_init(int p) {
    port_stop(p);

    /* allocate command list: 32 entries * 32 bytes = 1024 bytes, 1K aligned */
    ahci_cmd_hdr_t *cl = (ahci_cmd_hdr_t *)alloc_aligned(1024, 1024);
    if (!cl) return -1;

    /* allocate FIS receive area: 256 bytes, 256-byte aligned */
    uint8_t *fb = (uint8_t *)alloc_aligned(256, 256);
    if (!fb) return -1;

    /* allocate command table: 128-byte aligned */
    ahci_cmd_tbl_t *ct = (ahci_cmd_tbl_t *)alloc_aligned(sizeof(ahci_cmd_tbl_t), 128);
    if (!ct) return -1;

    /* set port registers */
    port_reg_set(p, PORT_CLB, (uint32_t)cl);
    port_reg_set(p, PORT_CLBU, 0);
    port_reg_set(p, PORT_FB, (uint32_t)fb);
    port_reg_set(p, PORT_FBU, 0);

    /* clear pending interrupts and errors */
    port_reg_set(p, PORT_IS, 0xFFFFFFFF);
    port_reg_set(p, PORT_SERR, 0xFFFFFFFF);

    ports[p].cmd_list = cl;
    ports[p].fis_recv = fb;
    ports[p].cmd_tbl = ct;

    /* point first command header at the command table */
    cl[0].ctba = (uint32_t)ct;
    cl[0].ctbau = 0;

    port_start(p);
    return 0;
}

static int send_command(int p, int write, uint64_t lba, uint32_t count, uint8_t *buf) {
    ahci_cmd_hdr_t *hdr = &ports[p].cmd_list[0];
    ahci_cmd_tbl_t *tbl = ports[p].cmd_tbl;

    memset(tbl, 0, sizeof(ahci_cmd_tbl_t));

    /* build FIS */
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80; /* C bit = 1: this is a command */
    fis->command  = write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
    fis->device   = (1 << 6); /* LBA mode */
    fis->lba0 = (uint8_t)(lba);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);
    fis->count = (uint16_t)count;

    /* set up PRDT: single entry pointing at the caller's buffer */
    uint32_t bytes = count * 512;
    tbl->prdt[0].dba  = (uint32_t)buf;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc_i = (bytes - 1) | 0; /* byte count 0-based, no interrupt */

    /* command header */
    hdr->flags = (sizeof(fis_reg_h2d_t) / 4) & 0x1F; /* CFL in DWORDs */
    if (write) hdr->flags |= (1 << 6); /* W bit */
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    /* wait for port not busy */
    for (int i = 0; i < 1000000; i++) {
        uint32_t tfd = port_reg(p, PORT_TFD);
        if (!(tfd & (PORT_TFD_BSY | PORT_TFD_DRQ))) break;
    }

    /* issue command (slot 0) */
    port_reg_set(p, PORT_CI, 1);

    /* wait for completion */
    for (int i = 0; i < 5000000; i++) {
        uint32_t ci = port_reg(p, PORT_CI);
        if (!(ci & 1)) return 0; /* done */
        uint32_t is = port_reg(p, PORT_IS);
        if (is & (1 << 30)) { /* task file error */
            port_reg_set(p, PORT_IS, 0xFFFFFFFF);
            return -1;
        }
    }
    return -1; /* timeout */
}

static void identify_drive(int p) {
    uint16_t *id_buf = (uint16_t *)alloc_aligned(512, 4);
    if (!id_buf) return;

    ahci_cmd_hdr_t *hdr = &ports[p].cmd_list[0];
    ahci_cmd_tbl_t *tbl = ports[p].cmd_tbl;
    memset(tbl, 0, sizeof(ahci_cmd_tbl_t));

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;
    fis->command  = ATA_CMD_IDENTIFY;
    fis->device   = 0;

    tbl->prdt[0].dba  = (uint32_t)id_buf;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc_i = 511; /* 512 bytes, 0-based */

    hdr->flags = (sizeof(fis_reg_h2d_t) / 4) & 0x1F;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    for (int i = 0; i < 1000000; i++) {
        uint32_t tfd = port_reg(p, PORT_TFD);
        if (!(tfd & (PORT_TFD_BSY | PORT_TFD_DRQ))) break;
    }

    port_reg_set(p, PORT_CI, 1);

    for (int i = 0; i < 5000000; i++) {
        if (!(port_reg(p, PORT_CI) & 1)) break;
    }

    if (port_reg(p, PORT_CI) & 1) {
        kfree(id_buf);
        return; /* identify failed */
    }

    /* extract model string (words 27-46, byte-swapped) */
    char *m = ports[p].model;
    for (int i = 0; i < 20; i++) {
        uint16_t w = id_buf[27 + i];
        m[i * 2]     = (char)(w >> 8);
        m[i * 2 + 1] = (char)(w & 0xFF);
    }
    m[40] = '\0';
    /* trim trailing spaces */
    for (int i = 39; i >= 0 && m[i] == ' '; i--) m[i] = '\0';

    /* extract sector count (words 100-103 for 48-bit LBA) */
    uint64_t sectors = (uint64_t)id_buf[100]
                     | ((uint64_t)id_buf[101] << 16)
                     | ((uint64_t)id_buf[102] << 32)
                     | ((uint64_t)id_buf[103] << 48);
    if (sectors == 0) {
        /* fallback: 28-bit LBA (words 60-61) */
        sectors = (uint32_t)id_buf[60] | ((uint32_t)id_buf[61] << 16);
    }
    ports[p].sectors = sectors;
    ports[p].size_mb = (uint32_t)(sectors / 2048);

    kfree(id_buf);
}

void ahci_init(void) {
    memset(ports, 0, sizeof(ports));
    num_drives = 0;

    /* scan PCI for AHCI controller: class 0x01 (storage), subclass 0x06 (SATA) */
    int n = pci_device_count();
    pci_device_t *ahci_dev = 0;
    for (int i = 0; i < n; i++) {
        pci_device_t *d = pci_get_device(i);
        if (d && d->class_code == 0x01 && d->subclass == 0x06) {
            ahci_dev = d;
            break;
        }
    }
    if (!ahci_dev) return; /* no AHCI controller found */

    /* read ABAR from PCI BAR5 (offset 0x24) */
    uint32_t bar5 = pci_read(ahci_dev->bus, ahci_dev->slot, ahci_dev->func, 0x24);
    bar5 &= 0xFFFFFFF0; /* mask type bits */
    if (bar5 == 0) return;
    abar = (volatile uint32_t *)bar5;

    /* enable bus mastering */
    uint32_t cmd = pci_read(ahci_dev->bus, ahci_dev->slot, ahci_dev->func, 0x04);
    cmd |= (1 << 2) | (1 << 1); /* bus master + memory space enable */
    pci_write(ahci_dev->bus, ahci_dev->slot, ahci_dev->func, 0x04, cmd);

    /* enable AHCI mode */
    uint32_t ghc = ahci_reg(AHCI_GHC);
    ghc |= AHCI_GHC_AE;
    ahci_reg_set(AHCI_GHC, ghc);

    /* read version */
    uint32_t vs = ahci_reg(AHCI_VS);
    uint32_t pi = ahci_reg(AHCI_PI);

    console_write("  [");
    console_write_color("OK", 0x02);
    console_write("] AHCI ");
    char vbuf[16];
    itoa((vs >> 16) & 0xFFFF, vbuf, 10);
    console_write(vbuf);
    console_write(".");
    itoa(vs & 0xFFFF, vbuf, 10);
    console_write(vbuf);
    console_write("\n");

    /* probe each implemented port */
    for (int p = 0; p < AHCI_MAX_PORTS; p++) {
        if (!(pi & (1 << p))) continue;

        uint32_t ssts = port_reg(p, PORT_SSTS);
        uint32_t det = ssts & SSTS_DET_MASK;
        if (det != SSTS_DET_PRESENT) continue;

        uint32_t sig = port_reg(p, PORT_SIG);
        if (sig != SATA_SIG_ATA && sig != SATA_SIG_ATAPI) continue;

        ports[p].active = 1;
        ports[p].is_atapi = (sig == SATA_SIG_ATAPI);

        if (port_init(p) != 0) {
            ports[p].active = 0;
            continue;
        }

        if (!ports[p].is_atapi) {
            identify_drive(p);
        }

        num_drives++;

        console_write("  [");
        console_write_color("OK", 0x02);
        console_write("] SATA port ");
        char pbuf[4]; itoa(p, pbuf, 10);
        console_write(pbuf);
        console_write(": ");
        if (ports[p].model[0]) {
            console_write(ports[p].model);
            console_write(" (");
            char sbuf[16]; itoa(ports[p].size_mb, sbuf, 10);
            console_write(sbuf);
            console_write(" MB)\n");
        } else {
            console_write(ports[p].is_atapi ? "ATAPI device\n" : "unknown\n");
        }
    }
}

int ahci_read(int p, uint64_t lba, uint32_t count, uint8_t *buf) {
    if (p < 0 || p >= AHCI_MAX_PORTS || !ports[p].active) return -1;
    return send_command(p, 0, lba, count, buf);
}

int ahci_write(int p, uint64_t lba, uint32_t count, const uint8_t *buf) {
    if (p < 0 || p >= AHCI_MAX_PORTS || !ports[p].active) return -1;
    return send_command(p, 1, lba, count, (uint8_t *)buf);
}

int ahci_drive_count(void) { return num_drives; }

int ahci_identify(int p, uint16_t *buf) {
    if (p < 0 || p >= AHCI_MAX_PORTS || !ports[p].active) return -1;

    ahci_cmd_hdr_t *hdr = &ports[p].cmd_list[0];
    ahci_cmd_tbl_t *tbl = ports[p].cmd_tbl;
    memset(tbl, 0, sizeof(ahci_cmd_tbl_t));

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;
    fis->command  = ATA_CMD_IDENTIFY;

    tbl->prdt[0].dba  = (uint32_t)buf;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc_i = 511;

    hdr->flags = (sizeof(fis_reg_h2d_t) / 4) & 0x1F;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    for (int i = 0; i < 1000000; i++) {
        if (!(port_reg(p, PORT_TFD) & (PORT_TFD_BSY | PORT_TFD_DRQ))) break;
    }
    port_reg_set(p, PORT_CI, 1);
    for (int i = 0; i < 5000000; i++) {
        if (!(port_reg(p, PORT_CI) & 1)) return 0;
    }
    return -1;
}
