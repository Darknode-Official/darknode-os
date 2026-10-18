# Darknode OS core

The from-source core of the Darknode OS distribution — this is `core/` of
[`darknode-os-distro`](../README.md). A real, bootable operating system built
on the **mainline Linux kernel**.

This is the shipping Darknode OS per the signed Track B decision: an actual
Linux-based OS, not an emulation and not the from-scratch research kernel
("Darknode Obsidian") kept in the separate kernel repo. Everything here is real
code that really runs — the genuine Linux kernel compiled from
[`torvalds/linux`](https://github.com/torvalds/linux) source, plus a BusyBox
userland and a Darknode `init`/shell environment.

## What you get

- The **real Linux kernel** (v6.12 LTS), compiled from upstream source — real
  process scheduler, real memory management, real TCP/IP stack, real VFS,
  real device drivers (virtio, serial, ext4, networking).
- A **BusyBox** userland (static, ~400 real applets: `sh`, `ip`, `ps`, `vi`,
  `wget`, `nc`, `mount`, `top`, ...).
- A Darknode `init` (PID 1) that brings up `/proc`, `/sys`, `/dev`, networking
  (DHCP on the first NIC), branding, and an interactive login shell.

It boots in QEMU to a working Darknode shell. This is the minimal core; the
userland is grown from here (see Roadmap).

## Build

Needs a Debian/Ubuntu host with:

```
sudo apt-get install -y build-essential flex bison libelf-dev libssl-dev \
                        bc cpio xz-utils wget git qemu-system-x86
```

Then:

```
./build.sh      # fetches kernel + busybox source, compiles both, packs initramfs
./run.sh -nographic   # boots it headless in this terminal (Ctrl-A X to quit)
./run.sh              # boots it in a graphical QEMU window
```

Build outputs land in `out/`:

- `out/bzImage` — the compiled Linux kernel
- `out/initramfs.cpio.gz` — the Darknode userland

## Layout

```
build.sh            reproducible build (kernel + userland + initramfs)
run.sh              QEMU launcher (graphical or -nographic serial)
overlay/root/       the Darknode userland that becomes the initramfs
  init              PID 1: mounts, networking, branding, shell
  etc/os-release    Darknode OS identity
  etc/banner        boot banner
  etc/profile       login shell environment (branded prompt, aliases)
  usr/bin/darknode  system summary / tour
  usr/bin/help      command list
src/                fetched sources (gitignored; recreated by build.sh)
out/                build artifacts (gitignored)
```

## What is honest about this

- It runs the **real** Linux kernel and real userland programs. `uname -a`,
  `ps`, `ip addr`, real networking all work because they are the real thing.
- The core userland is intentionally minimal (BusyBox). It is a working base,
  not yet a full desktop distribution.
- It is **not** the from-scratch "Darknode Obsidian" kernel. That is a separate
  research/education project; this is the product that actually boots and runs
  real software.

## Roadmap (toward a full workstation)

1. **Persistent root filesystem** — ext4 image built with `debootstrap` (Debian
   base) so the OS has a full package set and survives reboots, replacing the
   RAM-only initramfs.
2. **Darknode security toolkit** — wire in the curated offensive/defensive tools
   and the local-AI layer (the provisioning already prototyped in the Track B
   cloud-init repo).
3. **Graphical shell** — a Darknode-themed desktop (compositor + Darknode UI)
   matching the website design system.
4. **Installer + signed images** — an ISO that installs to disk, with
   reproducible, signed release images.
