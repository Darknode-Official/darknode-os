# Darknode OS — Kernel Build System
# Requires: nasm, i686-elf-gcc (or gcc -m32), grub-mkrescue, xorriso

CC      = i686-elf-gcc
AS      = nasm
LD      = i686-elf-ld
CFLAGS  = -m32 -std=gnu99 -ffreestanding -fno-pic -fno-pie -O2 -Wall -Wextra -Iinclude -Ikernel -Idrivers -Ifs -Inet
ASFLAGS = -f elf32
LDFLAGS = -T linker.ld -nostdlib -melf_i386

# Fallback to system gcc if cross-compiler not found
ifeq ($(shell which $(CC) 2>/dev/null),)
CC = gcc
LD = ld
endif

KERNEL = darknode.elf
ISO    = darknode-os.iso

ASM_SRCS = boot/multiboot.asm boot/boot.asm boot/gdt.asm boot/idt.asm
C_SRCS   = kernel/kernel.c kernel/console.c kernel/string.c kernel/gdt.c \
           kernel/idt.c kernel/timer.c kernel/keyboard.c kernel/pmm.c \
           kernel/heap.c kernel/serial.c kernel/shell.c \
           kernel/process.c kernel/syscall.c \
           kernel/framebuffer.c kernel/gui.c kernel/theme.c \
           drivers/rtc.c drivers/pci.c drivers/ata.c drivers/ne2000.c drivers/mouse.c \
           drivers/usb.c drivers/usb_hid.c drivers/ahci.c \
           drivers/acpi.c drivers/rtl8139.c drivers/vbox.c \
           fs/vfs.c fs/ramfs.c fs/devfs.c \
           net/ethernet.c net/arp.c net/ipv4.c net/icmp.c \
           net/udp.c net/dhcp.c net/dns.c

ASM_OBJS = $(ASM_SRCS:.asm=.o)
C_OBJS   = $(C_SRCS:.c=.o)
OBJS     = $(ASM_OBJS) $(C_OBJS)

.PHONY: all clean iso run debug

all: $(KERNEL)

$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/darknode.elf
	echo 'set timeout=0'                    >  isodir/boot/grub/grub.cfg
	echo 'set default=0'                    >> isodir/boot/grub/grub.cfg
	echo 'menuentry "Darknode OS" {'        >> isodir/boot/grub/grub.cfg
	echo '    multiboot2 /boot/darknode.elf' >> isodir/boot/grub/grub.cfg
	echo '    boot'                          >> isodir/boot/grub/grub.cfg
	echo '}'                                 >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir
	@echo "\n  $(ISO) ready — boot with: qemu-system-i386 -cdrom $(ISO)\n"

run: iso
	qemu-system-i386 -cdrom $(ISO) -serial stdio -m 128M \
		-netdev user,id=net0 -device ne2k_pci,netdev=net0

debug: iso
	qemu-system-i386 -cdrom $(ISO) -serial stdio -m 128M -s -S \
		-netdev user,id=net0 -device ne2k_pci,netdev=net0

clean:
	rm -f $(OBJS) $(KERNEL) $(ISO)
	rm -rf isodir

loc:
	@echo "Lines of code:"
	@find . -name '*.c' -o -name '*.h' -o -name '*.asm' | xargs wc -l | tail -1
