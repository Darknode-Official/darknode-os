#include "acpi.h"
#include "../kernel/io.h"
#include "../kernel/string.h"
#include "../kernel/console.h"

static acpi_fadt_t *fadt = 0;
static uint16_t slp_typa = 0;
static uint16_t slp_typb = 0;
static uint32_t pm1a_cnt = 0;
static uint32_t pm1b_cnt = 0;
static uint32_t pm_tmr = 0;
static char oem[7] = {0};
static int acpi_ready = 0;

#define SLP_EN  (1 << 13)

static int checksum(void *ptr, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++)
        sum += ((uint8_t *)ptr)[i];
    return sum == 0;
}

static acpi_rsdp_t *find_rsdp(void) {
    /* search EBDA */
    uint16_t ebda_seg = *(uint16_t *)0x040E;
    uint32_t ebda = (uint32_t)ebda_seg << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000) {
        for (uint32_t addr = ebda; addr < ebda + 1024; addr += 16) {
            if (memcmp((void *)addr, "RSD PTR ", 8) == 0) {
                acpi_rsdp_t *r = (acpi_rsdp_t *)addr;
                if (checksum(r, 20)) return r;
            }
        }
    }
    /* search BIOS area */
    for (uint32_t addr = 0x000E0000; addr < 0x00100000; addr += 16) {
        if (memcmp((void *)addr, "RSD PTR ", 8) == 0) {
            acpi_rsdp_t *r = (acpi_rsdp_t *)addr;
            if (checksum(r, 20)) return r;
        }
    }
    return 0;
}

static acpi_sdt_header_t *find_table(acpi_rsdt_t *rsdt, const char *sig) {
    uint32_t entries = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
    for (uint32_t i = 0; i < entries; i++) {
        acpi_sdt_header_t *h = (acpi_sdt_header_t *)(rsdt->entries[i]);
        if (memcmp(h->signature, sig, 4) == 0 && checksum(h, h->length))
            return h;
    }
    return 0;
}

static void parse_s5(acpi_fadt_t *f) {
    /* Parse \_S5 from DSDT for sleep type values */
    uint8_t *dsdt = (uint8_t *)(f->dsdt);
    uint32_t dsdt_len = ((acpi_sdt_header_t *)dsdt)->length;
    /* search for "_S5_" or "\\_S5" */
    for (uint32_t i = 36; i < dsdt_len - 8; i++) {
        if (dsdt[i] == '_' && dsdt[i+1] == 'S' && dsdt[i+2] == '5' && dsdt[i+3] == '_') {
            /* skip to the package and extract SLP_TYPa/b */
            uint32_t j = i + 4;
            if (dsdt[j] == 0x12) { /* PackageOp */
                j++;
                j++; /* skip PkgLength */
                j++; /* skip NumElements */
                if (dsdt[j] == 0x0A) j++; /* BytePrefix */
                slp_typa = dsdt[j] << 10;
                j++;
                if (dsdt[j] == 0x0A) j++;
                slp_typb = dsdt[j] << 10;
                return;
            }
        }
    }
    /* fallback: common QEMU/VBox values */
    slp_typa = 5 << 10;
    slp_typb = 0;
}

int acpi_init(void) {
    acpi_rsdp_t *rsdp = find_rsdp();
    if (!rsdp) return -1;

    memcpy(oem, rsdp->oem_id, 6);
    oem[6] = '\0';

    acpi_rsdt_t *rsdt = (acpi_rsdt_t *)(rsdp->rsdt_addr);
    if (!rsdt || !checksum(rsdt, rsdt->header.length)) return -2;

    acpi_sdt_header_t *facp = find_table(rsdt, "FACP");
    if (!facp) return -3;

    fadt = (acpi_fadt_t *)facp;
    pm1a_cnt = fadt->pm1a_event_block + fadt->pm1_event_length;
    pm1b_cnt = fadt->pm1b_event_block ? fadt->pm1b_event_block + fadt->pm1_event_length : 0;
    pm_tmr   = fadt->pm_timer_block;

    /* Use PM1a/PM1b control block addresses directly if available */
    /* FADT offset 64 = PM1a_CNT_BLK, offset 68 = PM1b_CNT_BLK */
    uint32_t *fadt_raw = (uint32_t *)fadt;
    if (fadt->header.length >= 72) {
        pm1a_cnt = fadt_raw[16]; /* offset 64 / 4 */
        pm1b_cnt = fadt_raw[17]; /* offset 68 / 4 */
    }

    /* Enable ACPI if not already enabled */
    if (fadt->smi_command_port && fadt->acpi_enable) {
        uint16_t pm1a_val = inw(pm1a_cnt);
        if (!(pm1a_val & 1)) {
            outb(fadt->smi_command_port, fadt->acpi_enable);
            /* wait for ACPI to become enabled */
            for (int i = 0; i < 1000; i++) {
                if (inw(pm1a_cnt) & 1) break;
                io_wait();
            }
        }
    }

    parse_s5(fadt);
    acpi_ready = 1;
    return 0;
}

void acpi_shutdown(void) {
    if (!acpi_ready || !pm1a_cnt) {
        /* fallback: QEMU/Bochs debug exit */
        outw(0xB004, 0x2000); /* Bochs/old QEMU */
        outw(0x0604, 0x2000); /* new QEMU */
        return;
    }
    outw(pm1a_cnt, slp_typa | SLP_EN);
    if (pm1b_cnt)
        outw(pm1b_cnt, slp_typb | SLP_EN);
    /* if still running, try QEMU fallbacks */
    outw(0xB004, 0x2000);
    outw(0x0604, 0x2000);
}

void acpi_reboot(void) {
    if (acpi_ready && fadt && fadt->header.length >= 129 &&
        fadt->reset_reg_space == 1 && fadt->reset_reg_address) {
        outb((uint16_t)fadt->reset_reg_address, fadt->reset_value);
    }
    /* fallback: keyboard controller reset */
    outb(0x64, 0xFE);
    /* fallback: triple fault */
    __asm__ volatile("lidt (%%eax)" : : "a"(0));
    __asm__ volatile("int $3");
}

uint32_t acpi_get_pm_timer(void) {
    if (!pm_tmr) return 0;
    return inl(pm_tmr);
}

const char *acpi_oem_id(void) {
    return oem;
}
