#include "../include/types.h"
#include "../include/darknode.h"
#include "../include/multiboot2.h"
#include "console.h"
#include "string.h"
#include "gdt.h"
#include "idt.h"
#include "timer.h"
#include "keyboard.h"
#include "pmm.h"
#include "heap.h"
#include "serial.h"
#include "shell.h"
#include "process.h"
#include "syscall.h"
#include "../drivers/rtc.h"
#include "../drivers/pci.h"
#include "../drivers/ata.h"
#include "../drivers/ahci.h"
#include "../drivers/ne2000.h"
#include "../drivers/rtl8139.h"
#include "../drivers/acpi.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "../fs/devfs.h"
#include "../net/ethernet.h"
#include "../net/arp.h"
#include "../net/ipv4.h"
#include "../net/icmp.h"
#include "../net/udp.h"
#include "../net/dhcp.h"
#include "../net/dns.h"
#include "../drivers/mouse.h"
#include "../drivers/usb.h"
#include "../drivers/usb_hid.h"
#include "../drivers/vbox.h"
#include "framebuffer.h"
#include "theme.h"
#include "gui.h"

static void boot_banner(void) {
    console_set_color(DN_COLOR_ACCENT, DN_COLOR_BG);
    console_write("\n");
    console_write("  ____             _                     _       \n");
    console_write(" |  _ \\  __ _ _ __| | ___ __   ___   __| | ___  \n");
    console_write(" | | | |/ _` | '__| |/ / '_ \\ / _ \\ / _` |/ _ \\ \n");
    console_write(" | |_| | (_| | |  |   <| | | | (_) | (_| |  __/ \n");
    console_write(" |____/ \\__,_|_|  |_|\\_\\_| |_|\\___/ \\__,_|\\___| \n");
    console_write("\n");

    console_set_color(DN_COLOR_HEADER, DN_COLOR_BG);
    console_write("  " DARKNODE_NAME " ");
    console_set_color(DN_COLOR_DIM, DN_COLOR_BG);
    console_write("v" DARKNODE_VERSION " | ");
    console_write(DARKNODE_YEAR);
    console_write(" | ");
    console_write(DARKNODE_AUTHOR);
    console_write("\n\n");
    console_set_color(DN_COLOR_FG, DN_COLOR_BG);
}

static void boot_log(const char *component, const char *status) {
    console_write("  [");
    console_write_color("OK", DN_COLOR_OK);
    console_write("] ");
    console_write(component);
    if (status) {
        console_write_color(" — ", DN_COLOR_DIM);
        console_write_color(status, DN_COLOR_DIM);
    }
    console_write("\n");
}

void kmain(uint32_t magic, multiboot2_info_t *mbi) {
    console_init();
    serial_init();
    serial_write("[darknode] booting...\n");

    boot_banner();

    /* CPU core setup */
    gdt_init();
    boot_log("GDT", "5 segments (null, kcode, kdata, ucode, udata)");

    idt_init();
    boot_log("IDT", "256 gates, PIC remapped");

    timer_init(1000);
    boot_log("PIT", "1000 Hz system timer");

    keyboard_init();
    boot_log("PS/2 Keyboard", "scancode set 1");

    rtc_init();
    boot_log("RTC", "real-time clock");

    /* Memory management */
    if (magic == MULTIBOOT2_MAGIC) {
        pmm_init(mbi);
        char buf[64];
        utoa(pmm_total_memory() / 1024, buf, 10);
        strcat(buf, " MiB, ");
        char fp[16]; utoa(pmm_free_pages(), fp, 10);
        strcat(buf, fp);
        strcat(buf, " free pages");
        boot_log("PMM", buf);
    } else {
        console_write_color("  [!!] ", DN_COLOR_ERR);
        console_write("multiboot2 magic mismatch — PMM skipped\n");
    }

    heap_init();
    boot_log("Heap", "4 MiB kernel heap at 0x400000");

    /* Hardware detection */
    pci_init();
    char pcibuf[32];
    itoa(pci_device_count(), pcibuf, 10);
    strcat(pcibuf, " devices found");
    boot_log("PCI", pcibuf);

    /* ACPI */
    if (acpi_init() == 0) {
        boot_log("ACPI", acpi_oem_id());
    } else {
        boot_log("ACPI", "not found (fallback power control)");
    }

    ata_init();
    ahci_init();
    {
        uint32_t dc = ata_drive_count();
        if (dc > 0) {
            char atabuf[64];
            itoa(dc, atabuf, 10);
            strcat(atabuf, " drive(s): ");
            for (uint32_t i = 0; i < dc; i++) {
                ata_drive_t *d = ata_get_drive(i);
                if (d) {
                    strcat(atabuf, d->model);
                    if (i < dc - 1) strcat(atabuf, ", ");
                }
            }
            boot_log("ATA", atabuf);
        } else {
            boot_log("ATA", "no drives detected");
        }
    }

    /* Virtual filesystem */
    vfs_init();
    vfs_node_t *ramfs_root = ramfs_init();
    vfs_mount("/", ramfs_root);
    boot_log("VFS", "ramfs mounted at /");

    /* Device filesystem */
    vfs_node_t *dev_dir = vfs_resolve("/dev");
    if (dev_dir) {
        devfs_init((ramfs_entry_t *)dev_dir);
        boot_log("devfs", "/dev/null, zero, random, console, serial, rtc");
    }

    /* Process management */
    process_init();
    boot_log("Processes", "scheduler initialized, idle PID 0");

    /* System calls */
    syscall_init();
    boot_log("Syscalls", "INT 0x80, 11 handlers");

    /* Enable interrupts */
    __asm__ volatile("sti");
    boot_log("Interrupts", "enabled");

    /* Network stack */
    if (ne2000_init() == 0) {
        eth_init();
        arp_init();
        ipv4_init();
        icmp_init();
        udp_init();
        dhcp_init();
        dns_init();
        uint8_t mac[6];
        ne2000_get_mac(mac);
        char macbuf[24];
        macbuf[0] = '\0';
        for (int i = 0; i < 6; i++) {
            char hx[4]; utoa(mac[i], hx, 16);
            if (mac[i] < 16) strcat(macbuf, "0");
            strcat(macbuf, hx);
            if (i < 5) strcat(macbuf, ":");
        }
        boot_log("NE2000", macbuf);

        if (dhcp_discover() != 0) {
            console_write_color("  [", DN_COLOR_FG);
            console_write_color("!!", DN_COLOR_WARN);
            console_write("] DHCP — no response, use 'dhcp' to retry\n");
        }
        dns_set_server(dhcp_get_dns());
        boot_log("Network", "stack ready");
    } else if (rtl8139_init() == 0) {
        eth_init();
        arp_init();
        ipv4_init();
        icmp_init();
        udp_init();
        dhcp_init();
        dns_init();
        boot_log("RTL8139", "Realtek NIC detected");
        if (dhcp_discover() != 0) {
            console_write_color("  [", DN_COLOR_FG);
            console_write_color("!!", DN_COLOR_WARN);
            console_write("] DHCP — no response, use 'dhcp' to retry\n");
        }
        dns_set_server(dhcp_get_dns());
        boot_log("Network", "stack ready (RTL8139)");
    } else {
        boot_log("Network", "no NIC detected — networking disabled");
    }

    /* Mouse */
    mouse_init();
    boot_log("PS/2 Mouse", "IRQ12 active");

    /* USB */
    usb_init();
    usb_hid_init();
    boot_log("USB", "UHCI controller + HID keyboard/mouse");

    /* VirtualBox Guest Device — absolute mouse without capture */
    vbox_init();

    /* Check for framebuffer from multiboot2 */
    uint32_t *fb_addr = 0;
    int fb_w = 0, fb_h = 0, fb_pitch = 0, fb_bpp = 0;
    if (magic == MULTIBOOT2_MAGIC) {
        multiboot2_tag_t *tag = (multiboot2_tag_t *)((uint8_t *)mbi + 8);
        while (tag->type != MULTIBOOT2_TAG_END) {
            if (tag->type == MULTIBOOT2_TAG_FRAMEBUF) {
                multiboot2_tag_framebuffer_t *fb = (multiboot2_tag_framebuffer_t *)tag;
                fb_addr = (uint32_t *)(uint32_t)fb->addr;
                fb_w = fb->width;
                fb_h = fb->height;
                fb_pitch = fb->pitch;
                fb_bpp = fb->bpp;
            }
            tag = (multiboot2_tag_t *)((uint8_t *)tag + ((tag->size + 7) & ~7));
        }
    }

    if (fb_addr && fb_bpp == 32 && fb_w >= 640 && fb_h >= 480) {
        serial_write("[darknode] fb found: ");
        char fbbuf[64];
        itoa(fb_w, fbbuf, 10);
        serial_write(fbbuf);
        serial_write("x");
        char hbuf[16]; itoa(fb_h, hbuf, 10);
        serial_write(hbuf);
        serial_write(" addr=");
        char abuf[16]; utoa((uint32_t)fb_addr, abuf, 16);
        serial_write(abuf);
        serial_write(" pitch=");
        char pbuf[16]; itoa(fb_pitch, pbuf, 10);
        serial_write(pbuf);
        serial_write("\n");

        strcat(fbbuf, "x");
        strcat(fbbuf, hbuf);
        strcat(fbbuf, "x32 framebuffer");
        boot_log("Graphics", fbbuf);

        mouse_set_bounds(fb_w, fb_h);
        theme_init();
        serial_write("[darknode] calling fb_init\n");
        fb_init(fb_addr, fb_w, fb_h, fb_pitch);
        serial_write("[darknode] fb_init done\n");
        boot_log("Framebuffer", "initialized");

        serial_write("[darknode] calling gui_init\n");
        gui_init();
        serial_write("[darknode] gui_init done, calling gui_run\n");
        gui_run();
    } else {
        console_write("\n");
        console_write_color("  Type 'help' for available commands.\n\n", DN_COLOR_DIM);
        serial_write("[darknode] boot complete, entering shell\n");
        shell_init();
        shell_run();
    }
}
