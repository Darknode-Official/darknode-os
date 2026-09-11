#ifndef AHCI_H
#define AHCI_H
#include "../include/types.h"

/* AHCI Generic Host Control registers (offsets from ABAR) */
#define AHCI_CAP        0x00
#define AHCI_GHC        0x04
#define AHCI_IS         0x08
#define AHCI_PI         0x0C
#define AHCI_VS         0x10

/* GHC bits */
#define AHCI_GHC_AE     (1 << 31)   /* AHCI enable */
#define AHCI_GHC_IE     (1 << 1)    /* interrupt enable */
#define AHCI_GHC_HR     (1 << 0)    /* HBA reset */

/* Port register offsets (from ABAR + 0x100 + port*0x80) */
#define PORT_CLB        0x00  /* command list base (lo) */
#define PORT_CLBU       0x04  /* command list base (hi) */
#define PORT_FB         0x08  /* FIS base (lo) */
#define PORT_FBU        0x0C  /* FIS base (hi) */
#define PORT_IS         0x10  /* interrupt status */
#define PORT_IE         0x14  /* interrupt enable */
#define PORT_CMD        0x18  /* command and status */
#define PORT_TFD        0x20  /* task file data */
#define PORT_SIG        0x24  /* signature */
#define PORT_SSTS       0x28  /* SATA status */
#define PORT_SCTL       0x2C  /* SATA control */
#define PORT_SERR       0x30  /* SATA error */
#define PORT_SACT       0x34  /* SATA active */
#define PORT_CI         0x38  /* command issue */

/* PORT_CMD bits */
#define PORT_CMD_ST     (1 << 0)   /* start */
#define PORT_CMD_FRE    (1 << 4)   /* FIS receive enable */
#define PORT_CMD_FR     (1 << 14)  /* FIS receive running */
#define PORT_CMD_CR     (1 << 15)  /* command list running */

/* PORT_TFD bits */
#define PORT_TFD_BSY    (1 << 7)
#define PORT_TFD_DRQ    (1 << 3)
#define PORT_TFD_ERR    (1 << 0)

/* Device signatures */
#define SATA_SIG_ATA    0x00000101
#define SATA_SIG_ATAPI  0xEB140101
#define SATA_SIG_SEMB   0xC33C0101
#define SATA_SIG_PM     0x96690101

/* SATA status: device detection */
#define SSTS_DET_MASK   0x0F
#define SSTS_DET_PRESENT 0x03

/* FIS types */
#define FIS_TYPE_REG_H2D   0x27
#define FIS_TYPE_REG_D2H   0x34
#define FIS_TYPE_DMA_SETUP 0x41
#define FIS_TYPE_PIO_SETUP 0x5F
#define FIS_TYPE_DATA      0x46

/* ATA commands */
#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_READ_DMA_EX  0x25
#define ATA_CMD_WRITE_DMA_EX 0x35

#define AHCI_MAX_PORTS 32

/* FIS: Register Host to Device */
typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport_c;   /* bit 7: C (command/control), bits 3:0: PM port */
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2;
    uint8_t  device;
    uint8_t  lba3, lba4, lba5;
    uint8_t  featureh;
    uint16_t count;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  reserved[4];
} __attribute__((packed)) fis_reg_h2d_t;

/* Command Header (one of 32 in the command list) */
typedef struct {
    uint16_t flags;      /* bits 4:0 = CFL (command FIS length in DWORDs), bit 6=W, bit 5=A */
    uint16_t prdtl;      /* PRDT length (entries) */
    uint32_t prdbc;      /* PRD byte count (filled by HBA) */
    uint32_t ctba;       /* command table base address (lo) */
    uint32_t ctbau;      /* command table base address (hi) */
    uint32_t reserved[4];
} __attribute__((packed)) ahci_cmd_hdr_t;

/* Physical Region Descriptor Table entry */
typedef struct {
    uint32_t dba;        /* data base address (lo) */
    uint32_t dbau;       /* data base address (hi) */
    uint32_t reserved;
    uint32_t dbc_i;      /* byte count (bit 31 = interrupt on completion), count is 0-based */
} __attribute__((packed)) ahci_prdt_entry_t;

/* Command Table: CFIS (64 bytes) + ACMD (16 bytes) + reserved (48 bytes) + PRDT */
typedef struct {
    uint8_t            cfis[64];
    uint8_t            acmd[16];
    uint8_t            reserved[48];
    ahci_prdt_entry_t  prdt[8];  /* up to 8 PRD entries per command */
} __attribute__((packed)) ahci_cmd_tbl_t;

/* Per-port state */
typedef struct {
    int       active;
    int       is_atapi;
    char      model[41];
    uint64_t  sectors;
    uint32_t  size_mb;
    ahci_cmd_hdr_t *cmd_list;  /* 32 headers, 1K aligned */
    uint8_t        *fis_recv;  /* 256 bytes, 256-byte aligned */
    ahci_cmd_tbl_t *cmd_tbl;   /* command table, 128-byte aligned */
} ahci_port_t;

void     ahci_init(void);
int      ahci_read(int port, uint64_t lba, uint32_t count, uint8_t *buf);
int      ahci_write(int port, uint64_t lba, uint32_t count, const uint8_t *buf);
int      ahci_drive_count(void);
int      ahci_identify(int port, uint16_t *buf);

#endif
