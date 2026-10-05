# DO-001 — Repo-Description Claims Audit

Every claim in the GitHub repository description, checked against the source in
this repository. `[C]` = confirmed from source files; evidence cited.
Reproduced 2026-10-04 by reading the source and by building + booting the kernel
under QEMU.

**Repository description as of audit:**
> "Darknode OS kernel -- custom x86 operating system with GUI desktop, shell,
> filesystem, networking stack, and security tools. Built from scratch in C and
> Assembly."

| # | Claim | Verdict | Evidence |
|---|-------|---------|----------|
| 1 | "custom x86 operating system" | **Supported** `[C]` | i686 kernel; `linker.ld` loads at 1 MiB; multiboot2 boot (`boot/multiboot.asm`, `kernel/kernel.c`); builds and boots under QEMU to `gui_run`. |
| 2 | "GUI desktop" | **Supported, with qualification** `[C]` | `kernel/gui.c` (~71 KB): framebuffer windowing, start menu, desktop icons, draggable/resizable windows. Qualification: several app windows are static placeholders (see #6). |
| 3 | "shell" | **Supported** `[C]` | `kernel/shell.c`: 28 commands on real kernel state. GUI `Terminal` has its own ~15-command keyboard-driven interpreter (`kernel/gui.c`, `term_exec`). |
| 4 | "filesystem" | **Supported, with qualification** `[C]` | `fs/vfs.c`, `fs/ramfs.c`, `fs/devfs.c`: VFS + in-RAM ramfs mounted at `/` + devfs. Qualification: **in-memory only** — no persistent/on-disk filesystem (no FAT/ext), despite ATA/AHCI drivers; nothing survives reboot. |
| 5 | "networking stack" | **Partially supported** `[C]` | `net/`: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS implemented; NE2000 + RTL8139 drivers. **No TCP** — `IP_PROTO_TCP` is defined in `net/ipv4.h` but there is no `tcp.c` and no TCP layer. The desktop "About" panel claim "Full TCP/IP networking stack" (`kernel/gui.c:380`) is therefore **not supported**. |
| 6 | "security tools" | **NOT supported** `[C]` | No security/offensive/crypto tooling exists in the kernel source (no scanner, no exploit code, no working hash). The desktop "Firewall" app is a static hardcoded rule table painted to screen with no packet-filter engine (`kernel/gui.c`, `draw_firewall`); the "Hash Tools" app prints hardcoded example digests and does not hash its input (`kernel/gui.c`, `draw_hashtools` — the displayed SHA-256 is in fact the well-known empty-string hash). This is the primary false claim. |
| 7 | "Built from scratch in C and Assembly" | **Supported** `[C]` | ~9,250 lines of C/headers + NASM; freestanding build (`-ffreestanding -nostdlib`, `Makefile`); no external libraries. |

## Additional misleading surfaces (not in the description but in-product)

| Surface | Issue | Evidence |
|---|---|---|
| Desktop "Web Browser" | Presented as a browser (Chrome-style tab + address bar) but renders only a static painted "darknode.ai" marketing page; no HTML/CSS/JS engine, fetches nothing. It is a placeholder for an impossible dependency, not a browser. | `kernel/gui.c`, `draw_browser` |
| Fake browser hero stats | The painted page shows "449K+ lines / 120+ tools / 6 engines / 34 modules" and "Security Operating System / the platform for ethical hacking" — these are the **Linux platform's / web platform's** numbers, displayed inside the from-scratch kernel, conflating the two incompatible products (the core DO-001 problem). | `kernel/gui.c:929-961` |

## Recommended description (pending DO-001 sign-off)

Neutral wording that the current source fully supports:

> "Darknode OS — a from-scratch 32-bit x86 research kernel written in C and
> NASM assembly. Boots via GRUB (multiboot2) to a framebuffer GUI desktop under
> QEMU. Includes a PS/2/USB input stack, PCI/ATA/AHCI/NIC drivers, an in-RAM
> VFS, a cooperative scheduler, INT 0x80 syscalls, and a UDP/ICMP IPv4 network
> stack (DHCP/DNS; no TCP). Pre-alpha; not a Linux distribution; no third-party
> software or real web browser. Security tooling is **not** part of this kernel."

The final description depends on the Track chosen in
`DECISION-MEMO-DO-001.md`. Under Track B the security-OS branding moves to the
Linux-based shipping product and this repo is renamed to a research name; under
Track A/C this repo keeps a research identity with a description like the above.
Correcting the live GitHub description requires an owner action (the agent is
restricted to local work and does not change the remote).
