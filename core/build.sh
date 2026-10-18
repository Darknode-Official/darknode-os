#!/usr/bin/env bash
# Darknode OS - reproducible build.
# Produces a bootable OS from mainline Linux kernel source + a BusyBox userland.
#
# Output:  out/bzImage            (the compiled Linux kernel)
#          out/initramfs.cpio.gz  (the Darknode userland / init)
#
# Requires (Debian/Ubuntu): build-essential flex bison libelf-dev libssl-dev
#                           bc cpio xz-utils wget git qemu-system-x86
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
OUT="$HERE/out"
OVERLAY="$HERE/overlay/root"

KVER="${KVER:-v6.12}"
BBVER="${BBVER:-1.36.1}"
JOBS="${JOBS:-$(nproc)}"

mkdir -p "$SRC" "$OUT"

# git does not track empty directories: recreate the overlay's mount points and
# bin dirs so a fresh clone packs a complete root (init mounts proc/sys/dev/tmp/run).
mkdir -p "$OVERLAY"/{bin,sbin,etc,proc,sys,dev,tmp,root,run,usr/bin,usr/sbin}
chmod 1777 "$OVERLAY/tmp"

# Rootless host tools: if flex/bison/m4 or the libelf headers are missing and
# system packages can't be installed, fetch the distro .debs with
# `apt-get download` (no root needed) and extract them into ./hosttools.
use_host_tools() {
  local P="$HERE/hosttools" T; T="$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)"
  [ -d "$P/usr/bin" ] && export PATH="$P/usr/bin:$PATH"
  [ -x "$P/usr/bin/m4" ] && export M4="$P/usr/bin/m4"
  [ -d "$P/usr/share/bison" ] && export BISON_PKGDATADIR="$P/usr/share/bison"
  if [ -d "$P/usr/include" ]; then
    export C_INCLUDE_PATH="$P/usr/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
    export LIBRARY_PATH="$P/usr/lib/$T${LIBRARY_PATH:+:$LIBRARY_PATH}"
  fi
}
ensure_host_tools() {
  use_host_tools
  local need=""
  command -v flex  >/dev/null 2>&1 || need="$need flex"
  command -v bison >/dev/null 2>&1 || need="$need bison"
  command -v m4    >/dev/null 2>&1 || need="$need m4"
  echo '#include <gelf.h>' | gcc -E -x c - >/dev/null 2>&1 || need="$need libelf-dev"
  [ -z "$need" ] && return 0
  echo "==> Host tools missing:$need - fetching rootless into ./hosttools"
  local P="$HERE/hosttools" T; T="$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)"
  mkdir -p "$P/.debs"
  ( cd "$P/.debs" && apt-get download $need )
  for f in "$P"/.debs/*.deb; do dpkg-deb -x "$f" "$P"; done
  # the -dev package's libelf.so points at a runtime lib that lives in the system
  [ -L "$P/usr/lib/$T/libelf.so" ] && ln -sf "/usr/lib/$T/libelf.so.1" "$P/usr/lib/$T/libelf.so"
  use_host_tools
}
ensure_host_tools

echo "==> [1/5] Linux kernel source ($KVER)"
if [ ! -d "$SRC/linux" ]; then
  git clone --depth 1 --branch "$KVER" https://github.com/torvalds/linux.git "$SRC/linux"
fi

echo "==> [2/5] BusyBox source ($BBVER)"
if [ ! -d "$SRC/busybox-$BBVER" ]; then
  wget -q "https://busybox.net/downloads/busybox-$BBVER.tar.bz2" -O "$SRC/busybox.tar.bz2"
  tar -C "$SRC" -xf "$SRC/busybox.tar.bz2"
fi

echo "==> [3/5] Build BusyBox (static)"
pushd "$SRC/busybox-$BBVER" >/dev/null
  [ -f .config ] || make defconfig >/dev/null
  sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
  sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config || true
  make -j"$JOBS" >/dev/null
popd >/dev/null
cp "$SRC/busybox-$BBVER/busybox" "$OVERLAY/bin/busybox"

echo "==> [4/5] Pack Darknode initramfs"
( cd "$OVERLAY" && find . | cpio -o -H newc 2>/dev/null | gzip -9 ) > "$OUT/initramfs.cpio.gz"

echo "==> [5/5] Build Linux kernel"
pushd "$SRC/linux" >/dev/null
  make x86_64_defconfig >/dev/null
  # Guarantee the pieces a headless, virtio QEMU boot needs.
  scripts/config \
    --enable BLK_DEV_INITRD \
    --enable DEVTMPFS --enable DEVTMPFS_MOUNT \
    --enable SERIAL_8250 --enable SERIAL_8250_CONSOLE \
    --enable VIRTIO --enable VIRTIO_PCI --enable VIRTIO_BLK --enable VIRTIO_NET \
    --enable EXT4_FS --enable TMPFS --enable NET --enable INET --enable PACKET
  make olddefconfig >/dev/null
  make -j"$JOBS" bzImage
  cp arch/x86/boot/bzImage "$OUT/bzImage"
popd >/dev/null

echo
echo "Done. Boot it with:  ./run.sh"
echo "  kernel:    $OUT/bzImage        ($(du -h "$OUT/bzImage" | cut -f1))"
echo "  initramfs: $OUT/initramfs.cpio.gz ($(du -h "$OUT/initramfs.cpio.gz" | cut -f1))"
