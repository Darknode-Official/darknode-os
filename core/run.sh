#!/usr/bin/env bash
# Boot Darknode OS in QEMU.
#   ./run.sh            graphical window (if a display is available)
#   ./run.sh -nographic serial console in this terminal (headless; Ctrl-A X to quit)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/out"

[ -f "$OUT/bzImage" ] || { echo "No kernel yet - run ./build.sh first."; exit 1; }

MODE="${1:-}"
COMMON=(
  -m 1024
  -smp 2
  -kernel "$OUT/bzImage"
  -initrd "$OUT/initramfs.cpio.gz"
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0
)

if [ "$MODE" = "-selftest" ]; then
  # Boot headless, run the self-test, power off, and print the captured log.
  LOG="$OUT/selftest.log"
  timeout 90 qemu-system-x86_64 "${COMMON[@]}" \
    -append "console=ttyS0 darknode.selftest quiet" \
    -display none -serial "file:$LOG" -no-reboot || true
  echo "===== Darknode OS self-test boot log ====="
  cat "$LOG"
  grep -q "SELF-TEST OK" "$LOG" && echo "=> PASS: Darknode OS reached userspace." \
    || { echo "=> FAIL: userspace not reached."; exit 1; }
elif [ "$MODE" = "-nographic" ]; then
  exec qemu-system-x86_64 "${COMMON[@]}" \
    -append "console=ttyS0 quiet" -nographic
else
  exec qemu-system-x86_64 "${COMMON[@]}" \
    -append "console=tty0 quiet" -vga std
fi
