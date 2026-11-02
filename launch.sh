#!/usr/bin/env bash
# Launch Darknode OS in QEMU. Uses KVM if available (fast), else TCG (slow).
# The seed.iso stays attached; cloud-init runs it once (instance-id is pinned),
# so re-launches don't re-provision.
set -euo pipefail
cd "$(dirname "$0")"

# which build to launch (matches build.sh output names). Args: [edition] [base];
# both default to the last build recorded in darknode-os.conf.
EDITION="${1:-${DARKNODE_EDITION:-}}"; BASE="${2:-${DARKNODE_BASE:-}}"
[ -z "$EDITION" ] && [ -f darknode-os.conf ] && EDITION="$(sed -n 's/^EDITION=//p' darknode-os.conf | head -1)"
[ -z "$BASE" ]    && [ -f darknode-os.conf ] && BASE="$(sed -n 's/^BASE_OS=//p' darknode-os.conf | head -1)"
EDITION="$(echo "${EDITION:-full}" | tr 'A-Z' 'a-z')"; BASE="$(echo "${BASE:-debian}" | tr 'A-Z' 'a-z')"
DISK="darknode-os-${BASE}-${EDITION}.qcow2"
SEED="seed-${BASE}-${EDITION}.iso"
RAM="${DARKNODE_RAM:-4096}"
CPUS="${DARKNODE_CPUS:-2}"

[ -f "$DISK" ] || { echo "error: $DISK not found — run ./build.sh $BASE $EDITION first"; exit 1; }

ACCEL=()
if [ -w /dev/kvm ]; then
  ACCEL=(-enable-kvm -cpu host)
else
  echo "note: /dev/kvm not writable — running without KVM (slower)."
  echo "      add yourself to the 'kvm' group to enable acceleration."
fi

SEED_ARGS=(); [ -f "$SEED" ] && SEED_ARGS=(-drive file="$SEED",format=raw,if=virtio,readonly=on)

# Port-forward: 2222->22 (ssh), 8099->8099 (honeypot AI bridge), 8080->80.
exec qemu-system-x86_64 \
  "${ACCEL[@]}" \
  -m "$RAM" -smp "$CPUS" \
  -drive file="$DISK",format=qcow2,if=virtio \
  "${SEED_ARGS[@]}" \
  -device virtio-net,netdev=n0 \
  -netdev user,id=n0,hostfwd=tcp::2222-:22,hostfwd=tcp::8099-:8099,hostfwd=tcp::8080-:80 \
  -device virtio-vga-gl -display gtk,gl=on \
  -device virtio-tablet -device virtio-keyboard \
  -name "Darknode OS"
