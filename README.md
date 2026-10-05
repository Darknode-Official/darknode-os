# Darknode Obsidian — research kernel

> **This is NOT the shipping "Darknode OS".** Per the signed DO-001 decision
> (Track B), the product that end users install — **Darknode OS** — is a
> **Linux distribution** (custom compositor/shell/theme + an agentic security
> stack). This repository is the **separate from-scratch research kernel**,
> working name **"Darknode Obsidian"**. It is kept as a research / education
> project and is **not** the shipping product. The two must never be described
> as the same thing. (The research name is a working name the owner may change;
> the hard rule is only that it is not called "Darknode OS".)

**Status: pre-alpha research kernel, v0.1.0.** A from-scratch 32-bit x86
operating system kernel written in C and NASM assembly. It boots to a graphical
desktop under the QEMU emulator. It is **not** a Linux distribution, **not** the
shipping Darknode OS, **not** based on any existing OS, and **not** a
daily-driver or production system. It cannot run Linux, Windows, or any
third-party software, and it has no real web browser.

This README describes what the code in this repository actually does today,
reproduced from source and verified by building and booting the kernel. Where a
capability is only partially implemented or is a visual placeholder, that is
stated plainly. See [`docs/CLAIMS-AUDIT-DO-001.md`](docs/CLAIMS-AUDIT-DO-001.md)
for a line-by-line audit of the repository description against the source, and
[`docs/DECISION-MEMO-DO-001.md`](docs/DECISION-MEMO-DO-001.md) for the open
decision about what "Darknode OS" is as a product.

## What this is

- A standalone i686 (32-bit x86) kernel. Freestanding: built with
  `-ffreestanding -nostdlib`, no libc, no external dependencies.
- Boots via GRUB using the **multiboot2** protocol.
- ~9,250 lines of C, C headers, and NASM assembly (`make loc`).
- Author: Darknode-Official. Version 0.1.0.

## What is NOT here (explicit non-goals)

These are not implemented and are not near-term goals of the from-scratch
kernel. They are listed so that placeholders in the UI are not mistaken for
working features:

- **No real web browser.** The desktop "Web Browser" window is a static
  placeholder (see below). A from-scratch kernel at this stage cannot host
  Chromium, Firefox, or any production browser engine.
- **No third-party / Linux / POSIX software compatibility.**
- **No TCP.** The network stack implements UDP-based protocols only (see below).
- **No persistent / on-disk filesystem.** Storage is in-RAM only, despite the
  presence of ATA/AHCI block drivers.
- **No security / offensive / cryptographic tooling.** There is no scanner,
  exploit framework, or working hash implementation in this kernel. The
  "Firewall" and "Hash Tools" desktop windows display static placeholder data.
- **No tested real-hardware support.** Verified under QEMU only.

## Verified build and boot (this repository)

Reproduced on 2026-10-04 on Linux x86-64:

- **Builds:** `make all` compiles and links cleanly (warnings only) using the
  system `gcc -m32` fallback. Exit 0. Produces `darknode.elf`, a 32-bit ELF
  carrying the multiboot2 magic (`0xE85250D6`). `[C]`
- **ISO:** `make iso` produces a bootable GRUB rescue ISO via `grub-mkrescue`
  + `xorriso`. `[C]`
- **Boots:** `qemu-system-i386 -cdrom darknode-os.iso` boots through GRUB; the
  kernel receives a 1280x960x32 framebuffer from multiboot2 and launches its
  GUI (`gui_run`), confirmed by the serial log:
  ```
  [darknode] booting...
  [darknode] fb found: 1280x960 addr=fd000000 pitch=5120
  [darknode] fb_init done
  [darknode] gui_init done, calling gui_run
  ```
  `[C]`
- **Not verified:** boot on real hardware; boot under VirtualBox or other
  emulators (a VirtualBox guest driver exists in `drivers/vbox.c` but was not
  exercised in this run). `[V]`

## Build

Requirements: `nasm`, a 32-bit-capable C toolchain (`i686-elf-gcc` preferred;
falls back to system `gcc -m32`, which needs 32-bit multilib), `grub-mkrescue`,
`xorriso`, and `qemu-system-i386` to run.

```sh
make            # build darknode.elf
make iso        # build darknode-os.iso (bootable GRUB rescue image)
make run        # build ISO and boot it in QEMU (serial on stdio)
make debug      # same as run, with a GDB stub (-s -S)
make clean      # remove build artifacts
make loc        # count lines of source
```

Prebuilt `.elf`/`.iso` images are **not** committed to this repository; they are
reproducible build outputs. They should be downloaded from the repository's
Releases (to be published by the maintainer). See
[`docs/DECISION-MEMO-DO-001.md`](docs/DECISION-MEMO-DO-001.md).

## Implemented subsystems (from source)

### Boot and CPU
- Multiboot2 header and entry (`boot/`, `kernel/kernel.c`).
- GDT with 5 segments (null, kernel code/data, user code/data).
- IDT with 256 gates; the 8259 PIC is remapped.
- PIT programmable timer at 1000 Hz; serial (COM1) logging.

### Memory
- Physical memory manager seeded from the multiboot2 memory map.
- A 4 MiB kernel heap at `0x400000`.

### Drivers
PS/2 keyboard (scancode set 1), PS/2 mouse (IRQ12), RTC, PCI bus enumeration,
ACPI (RSDP/FADT discovery), ATA and AHCI block devices, NE2000 and RTL8139
network cards, USB UHCI controller + HID, and a VirtualBox guest device for
absolute-pointer support.

### Filesystem
A VFS layer with an in-memory **ramfs** mounted at `/` and a **devfs** providing
`/dev/null`, `/dev/zero`, `/dev/random`, `/dev/console`, `/dev/serial`, and
`/dev/rtc`. **In-RAM only — nothing is written to disk and nothing survives a
reboot.**

### Processes and syscalls
A process table with a cooperative scheduler (idle PID 0) and a system-call
interface on `INT 0x80` (11 handlers).

### Networking
Ethernet, ARP, IPv4, ICMP (ping), UDP, DHCP, and DNS, driven by the NE2000 or
RTL8139 driver. **There is no TCP implementation** (`IP_PROTO_TCP` is defined in
a header but no TCP layer exists), so there is no HTTP, TLS, or web traffic.

### Graphics / desktop
A 32-bpp framebuffer GUI with a theme system, a start menu, desktop icons, and
draggable/resizable windows with right-click menus. When no framebuffer is
available, the kernel falls back to a text-mode shell.

## Shell and desktop applications

### Text-mode shell (`kernel/shell.c`)
28 commands backed by real kernel state: `help clear version time uptime mem
free cpuinfo reboot shutdown echo ls cat mkdir touch write rm cd pwd ps mount
disk ifconfig ping arp dhcp dns netstat`. Filesystem commands operate on the
live ramfs; `ps` reads the real process table; the network commands use the
real stack.

### GUI desktop (`kernel/gui.c`) — 12 menu entries, honest status
- **Interactive, backed by real state:** **Terminal** — a keyboard-driven
  window with its own ~15-command interpreter (`help`, `whoami`, `hostname`,
  `uname`, `uptime`, `date`, `ls`, `pwd`, `free`, `ps`, `ifconfig`, `echo`,
  `neofetch`, `clear`, `cat`). Calculator provides arithmetic.
- **Display panels (representative data):** Task Manager, Disk Manager, Network,
  File Manager, Text Editor, Settings — windows that present system/representative
  information.
- **Static placeholders (NOT functional tools):**
  - **Web Browser** — paints a Chrome-like tab/address bar and a static
    "darknode.ai" marketing page using framebuffer text primitives. It fetches
    nothing and has no HTML/CSS/JS engine. It is a placeholder standing in for a
    dependency the kernel cannot provide.
  - **Firewall** — a static, hardcoded rule table painted to the screen. There
    is no packet-filtering engine behind it.
  - **Hash Tools** — displays hardcoded example digest strings. It does not hash
    its input.

## License

See [`LICENSE`](LICENSE).
