#!/usr/bin/env bash
# Build Darknode OS on the base OS of your choice. The Darknode identity — desktop,
# curated toolset, local-AI layer, branding — is applied on first boot by cloud-init,
# so any cloud-init + apt base can wear it.
#
#   ./build.sh                 # interactive OS picker  (the "settings" for the base OS)
#   ./build.sh ubuntu          # or name it directly
#   ./build.sh debian slim     # base + EDITION (netinstall | slim | full, default full)
#   DARKNODE_BASE=debian DARKNODE_EDITION=netinstall ./build.sh
#
# Editions (how much Darknode ships — all self-provision on first boot):
#   netinstall  terminal only, no desktop/UI — core CLI stack + Nexus + local AI (smallest, ~12G disk)
#   slim        full XFCE desktop + Nexus + tools, minus Metasploit/SecLists/Docker/cockpit app (~20G)
#   full        everything: desktop, cockpit app, Metasploit, SecLists, Exploit-DB, Docker (~30G)
#
# Supported bases live in the registry below. Debian & Ubuntu are fully wired
# (cloud-init + apt); adding another apt-family base is one registry line.
set -euo pipefail
cd "$(dirname "$0")"

CONF="darknode-os.conf"     # remembers your last choice — the persisted "setting"

# ── edition (how much ships) ─────────────────────────────────────────────────
EDITION="${2:-${DARKNODE_EDITION:-}}"
[ -z "$EDITION" ] && [ -f "$CONF" ] && EDITION="$(sed -n 's/^EDITION=//p' "$CONF" | head -1)"
EDITION="$(echo "${EDITION:-full}" | tr 'A-Z' 'a-z')"
case "$EDITION" in
  netinstall) DISK_SIZE="12G" ;;
  slim)       DISK_SIZE="20G" ;;
  full|"")    EDITION="full"; DISK_SIZE="30G" ;;
  *) echo "unknown edition '$EDITION' — use: netinstall | slim | full"; exit 2 ;;
esac
# DISK/SEED names also include the base OS (set once BASE is resolved below) so that
# building a different base with the same edition never silently reuses a stale disk.

# ── OS registry ───────────────────────────────────────────────────────────────
# key | pretty name | family | cloud-image directory | filename (or regex) | notes
declare -A OS_NAME OS_FAMILY OS_DIR OS_FILE OS_NOTE
reg(){ OS_NAME[$1]="$2"; OS_FAMILY[$1]="$3"; OS_DIR[$1]="$4"; OS_FILE[$1]="$5"; OS_NOTE[$1]="${6:-}"; }
#    key       pretty                family  cloud-image dir                                                           file / regex
reg debian   "Debian 12 (Bookworm)"  debian  "https://cloud.debian.org/images/cloud/bookworm/latest/"                  'debian-12-genericcloud-amd64(-[0-9.]+)?\.qcow2'  "default · rock-solid neutral base"
reg ubuntu   "Ubuntu 24.04 LTS"      ubuntu  "https://cloud-images.ubuntu.com/noble/current/"                         'noble-server-cloudimg-amd64\.img'                "popular · huge package universe"
reg ubuntu22 "Ubuntu 22.04 LTS"      ubuntu  "https://cloud-images.ubuntu.com/jammy/current/"                         'jammy-server-cloudimg-amd64\.img'                "older LTS"
reg kali     "Kali Linux (rolling)"  debian  "https://kali.download/cloud-images/current/"                            'kali-linux-[^"<> ]*cloud-genericcloud-amd64[^"<> ]*\.(qcow2|tar\.xz)' "experimental · already tool-heavy"
ORDER=(debian ubuntu ubuntu22 kali)

# OSes that CANNOT reuse this cloud-init/apt pipeline — shown so the picker is honest.
declare -A UNSUPPORTED=(
  [windows]="Windows needs an autounattend.xml + PowerShell/winget provisioner and a licensed ISO — a separate build, not this one."
  [fedora]="Fedora is dnf-based; the Darknode provisioner is apt-only for now (a dnf port is future work)."
  [arch]="Arch/BlackArch is pacman-based; needs a pacman port of the provisioner."
)

pick_menu(){
  echo "== Choose the base OS for Darknode ==" >&2
  local i=1; for k in "${ORDER[@]}"; do printf "  %d) %-22s %s\n" "$i" "${OS_NAME[$k]}" "${OS_NOTE[$k]}" >&2; i=$((i+1)); done
  printf "  --  not via this pipeline: %s\n" "${!UNSUPPORTED[*]}" >&2
  local ans; read -rp "OS [1-${#ORDER[@]}, default 1]: " ans </dev/tty || ans=1
  case "$ans" in ''|*[!0-9]*) ans=1;; esac        # non-numeric -> default (else ORDER[-1] picks the last)
  { [ "$ans" -ge 1 ] && [ "$ans" -le "${#ORDER[@]}" ]; } 2>/dev/null || ans=1
  echo "${ORDER[$((ans-1))]}"
}

# ── resolve the chosen base ──────────────────────────────────────────────────
BASE="${1:-${DARKNODE_BASE:-}}"
[ -z "$BASE" ] && [ -f "$CONF" ] && BASE="$(sed -n 's/^BASE_OS=//p' "$CONF" | head -1)"
if [ -z "$BASE" ]; then BASE="$(pick_menu)"; fi
BASE="$(echo "$BASE" | tr 'A-Z' 'a-z')"

if [ -n "${UNSUPPORTED[$BASE]:-}" ]; then
  echo "!! '$BASE' is not buildable by this pipeline." >&2
  echo "   ${UNSUPPORTED[$BASE]}" >&2
  exit 2
fi
[ -n "${OS_NAME[$BASE]:-}" ] || { echo "unknown base '$BASE' — options: ${ORDER[*]}"; exit 2; }

FAMILY="${OS_FAMILY[$BASE]}"; DIR="${OS_DIR[$BASE]}"; FILE_RE="${OS_FILE[$BASE]}"
BASE_IMG="base-${BASE}.qcow2"
DISK="darknode-os-${BASE}-${EDITION}.qcow2"      # per base+edition — no stale-disk reuse
SEED="seed-${BASE}-${EDITION}.iso"
{ echo "BASE_OS=$BASE"; echo "EDITION=$EDITION"; } > "$CONF"   # persist the settings
echo "== Darknode OS build  ·  base: ${OS_NAME[$BASE]}  ($FAMILY family)  ·  edition: $EDITION =="

need(){ command -v "$1" >/dev/null 2>&1; }
for t in qemu-img xorriso curl openssl; do
  need "$t" || { echo "installing build deps ..."; sudo apt-get update -qq && sudo apt-get install -y qemu-utils xorriso curl openssl; break; }
done

# 1) fetch the base cloud image (qcow2, or a .img which is already qcow2)
if [ ! -f "$BASE_IMG" ]; then
  FNAME="$(curl -fsSL "$DIR" | grep -oE "$FILE_RE" | sort -u | head -1 || true)"
  [ -n "$FNAME" ] || { echo "!! couldn't find a cloud image at $DIR (regex: $FILE_RE)"; echo "   If '$BASE' moved its images, update the registry in build.sh."; exit 1; }
  echo "-- downloading $FNAME ..."
  curl -fL "${DIR}${FNAME}" -o "$BASE_IMG.part"
  case "$FNAME" in
    *.tar.xz) tar -xJf "$BASE_IMG.part" && mv "$(tar -tJf "$BASE_IMG.part" | grep -m1 '\.qcow2$')" "$BASE_IMG"; rm -f "$BASE_IMG.part";;
    *)        mv "$BASE_IMG.part" "$BASE_IMG";;
  esac
fi

# 2) working disk (grown copy of the base)
if [ ! -f "$DISK" ]; then
  echo "-- creating $DISK ($DISK_SIZE) ..."
  qemu-img convert -O qcow2 "$BASE_IMG" "$DISK"
  qemu-img resize "$DISK" "$DISK_SIZE"
fi

# 3) cloud-init seed. Stamp the chosen family into user-data so the provisioner
#    adapts (kernel package, repo components) per distro.
echo "-- building cloud-init seed (family: $FAMILY) ..."
HASH="$(openssl passwd -6 darknode)"
rm -rf .seed && mkdir -p .seed
cp cloud-init/meta-data .seed/meta-data
sed -e "s#^    passwd: .*#    passwd: \"${HASH//#/\\#}\"#" \
    -e "s#@@DARKNODE_FAMILY@@#${FAMILY}#g" \
    -e "s#@@DARKNODE_EDITION@@#${EDITION}#g" \
    cloud-init/user-data > .seed/user-data
( cd .seed && xorriso -as mkisofs -quiet -output "../$SEED" -volid CIDATA -joliet -rock user-data meta-data )
rm -rf .seed

echo
echo "== done  ·  ${OS_NAME[$BASE]}  ·  $EDITION edition =="
echo "  disk:   $(pwd)/$DISK"
echo "  seed:   $(pwd)/$SEED"
echo "  launch: ./launch.sh          boot this build in QEMU/KVM"
echo "          ./export-vbox.sh     export the same disk to VirtualBox"
echo "  login:  darknode / darknode"
if [ "$EDITION" = netinstall ]; then
  echo "          terminal only — boots to a console"
else
  echo "          first boot installs the Darknode desktop + tools"
fi
echo "  rebuild other editions:  ./build.sh $BASE {netinstall|slim|full}"
