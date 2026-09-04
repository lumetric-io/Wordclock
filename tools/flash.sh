#!/usr/bin/env bash
# Fetch a product build from the VPS and flash it to connected ESP32-S3 boards.
#
# Tuned for the nextgen-bootstrap first-flash workflow: fetch the build once,
# then flash board after board in a loop. Each board is erased, written and
# verified; the verify step's hard reset leaves the board running the freshly
# flashed firmware (bootstrap comes up as a Wi-Fi AP ready to provision).
#
# Usage:
#   ./tools/flash.sh                          # nextgen-bootstrap, flash-many loop
#   ./tools/flash.sh --build                  # build on the VPS first, then flash
#   ./tools/flash.sh -p nextgen-50x50         # a different product
#   ./tools/flash.sh -P /dev/cu.usbmodem101   # skip port detection
#   ./tools/flash.sh --once                   # flash a single board and exit
#   ./tools/flash.sh --menu                   # pick the product interactively
#
# Requires (locally): ssh access to $VPS_HOST, rsync, esptool >= 5.0, python3
#
# Environment overrides:
#   VPS_HOST          ssh target             (default: ron@vps-vpn)
#   VPS_PROJECT_PATH  repo path on the VPS   (default: /home/ron/repos/wordclock)
#   ESPTOOL           esptool command        (default: esptool)
#   BAUD              write baud rate        (default: 460800)

set -euo pipefail

VPS_HOST="${VPS_HOST:-ron@vps-vpn}"
VPS_PROJECT_PATH="${VPS_PROJECT_PATH:-/home/ron/repos/wordclock}"
LOCAL_PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_CACHE="$LOCAL_PROJECT_ROOT/build_cache"
BAUD="${BAUD:-460800}"
CHIP="esp32s3"
DEFAULT_PRODUCT="nextgen-bootstrap"

read -r -a ESPTOOL_CMD <<< "${ESPTOOL:-esptool}"

PRODUCT=""
PORT_OVERRIDE=""
DO_BUILD=0
ONCE=0
MENU=0

usage() {
  awk 'NR==1 {next} /^#/ {sub(/^# ?/, ""); print; next} {exit}' "${BASH_SOURCE[0]}"
}

while [ $# -gt 0 ]; do
  case "$1" in
    -p|--product) PRODUCT="${2:?--product needs a value}"; shift 2 ;;
    -P|--port)    PORT_OVERRIDE="${2:?--port needs a value}"; shift 2 ;;
    --build)      DO_BUILD=1; shift ;;
    --once)       ONCE=1; shift ;;
    --menu)       MENU=1; shift ;;
    -h|--help)    usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

echo "== Wordclock S3 Flash Helper =="
echo

# ---------------------------------------------------------------- product ---
# Product list is derived from products/ so it cannot drift out of sync with
# platformio.ini the way a hardcoded list did.
PRODUCTS=()
for d in "$LOCAL_PROJECT_ROOT"/products/nextgen-*/; do
  [ -d "$d" ] || continue
  name="$(basename "$d")"
  [ "$name" = "$DEFAULT_PRODUCT" ] && continue
  PRODUCTS+=("$name")
done
# Bootstrap goes first: it is the common case. Guard the empty-array expansion —
# macOS ships bash 3.2, where "${arr[@]}" on an empty array trips `set -u`.
if [ -d "$LOCAL_PROJECT_ROOT/products/$DEFAULT_PRODUCT" ]; then
  if [ "${#PRODUCTS[@]}" -eq 0 ]; then
    PRODUCTS=("$DEFAULT_PRODUCT")
  else
    PRODUCTS=("$DEFAULT_PRODUCT" "${PRODUCTS[@]}")
  fi
fi

if [ "${#PRODUCTS[@]}" -eq 0 ]; then
  echo "❌ No products/nextgen-*/ found under $LOCAL_PROJECT_ROOT" >&2
  exit 1
fi

if [ -z "$PRODUCT" ]; then
  if [ "$MENU" -eq 1 ]; then
    echo "Select product:"
    select PRODUCT in "${PRODUCTS[@]}"; do
      [ -n "${PRODUCT:-}" ] && break
    done
  else
    PRODUCT="$DEFAULT_PRODUCT"
    echo "Product: $PRODUCT (default — use --menu or -p NAME to change)"
  fi
fi

if ! printf '%s\n' "${PRODUCTS[@]}" | grep -qx -- "$PRODUCT"; then
  echo "❌ Unknown product: $PRODUCT" >&2
  echo "   Available: ${PRODUCTS[*]}" >&2
  exit 1
fi
ENV_NAME="$PRODUCT"
echo

# --------------------------------------------------------------- esptool ----
ESPTOOL_VERSION="$("${ESPTOOL_CMD[@]}" version 2>/dev/null | head -1 || true)"
if [ -z "$ESPTOOL_VERSION" ]; then
  echo "❌ Cannot run '${ESPTOOL_CMD[*]}'. Install esptool >= 5.0 (pip install esptool)." >&2
  exit 1
fi
ESPTOOL_MAJOR="$(printf '%s' "$ESPTOOL_VERSION" | grep -oE '[0-9]+\.[0-9]+' | head -1 | cut -d. -f1 || true)"
if [ -n "$ESPTOOL_MAJOR" ] && [ "$ESPTOOL_MAJOR" -lt 5 ] 2>/dev/null; then
  echo "❌ esptool $ESPTOOL_VERSION is too old; this script uses 5.x dash-form" >&2
  echo "   subcommands (erase-flash / write-flash / verify-flash)." >&2
  echo "   Upgrade with: pip install -U esptool" >&2
  exit 1
fi

# ------------------------------------------------------------ ssh preflight --
echo "Checking ssh to $VPS_HOST…"
if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "$VPS_HOST" true 2>/dev/null; then
  echo "❌ Cannot reach $VPS_HOST over ssh." >&2
  echo "   • Is the VPN up?" >&2
  echo "   • Is your key loaded (ssh-add -l)?" >&2
  echo "   • Override the host with: VPS_HOST=user@host $0" >&2
  exit 1
fi

# Run a script on the VPS with arguments. The script is base64'd so the remote
# command line stays free of quoting hazards and works under any login shell.
remote_sh() {
  local script="$1"; shift
  local b64 cmd a
  b64="$(printf '%s' "$script" | base64 | tr -d '\n')"
  cmd="printf %s $b64 | base64 -d | sh -s --"
  for a in "$@"; do cmd+=" $(printf '%q' "$a")"; done
  ssh "$VPS_HOST" "$cmd"
}

# ------------------------------------------------------------ remote build ---
REMOTE_BUILD='set -e
cd "$1"
PIO="$(command -v pio || true)"
[ -n "$PIO" ] || PIO="$HOME/.platformio/penv/bin/pio"
[ -x "$PIO" ] || { echo "pio not found on the VPS" >&2; exit 1; }
"$PIO" run -e "$2"
"$PIO" run -e "$2" -t buildfs'

if [ "$DO_BUILD" -eq 1 ]; then
  echo
  echo "Building $ENV_NAME on $VPS_HOST (firmware + filesystem)…"
  remote_sh "$REMOTE_BUILD" "$VPS_PROJECT_PATH" "$ENV_NAME"
  echo "Build finished."
fi

# --------------------------------------------------- remote build provenance -
REMOTE_PROBE='cd "$1" 2>/dev/null || { echo "MISSING_PROJECT"; exit 0; }
rev=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
dirty=$(git status --porcelain 2>/dev/null | wc -l | tr -d " ")
branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)
d=".pio/build/$2"
[ -d "$d" ] || { echo "MISSING_BUILD"; exit 0; }
echo "REV $rev $branch $dirty"
for f in bootloader.bin partitions.bin firmware.bin littlefs.bin; do
  if [ -f "$d/$f" ]; then
    echo "FILE $f $(date -r "$d/$f" "+%Y-%m-%d %H:%M") $(wc -c < "$d/$f" | tr -d " ")"
  else
    echo "FILE $f MISSING"
  fi
done'

echo
echo "Inspecting $VPS_HOST:$VPS_PROJECT_PATH/.pio/build/$ENV_NAME…"
PROBE="$(remote_sh "$REMOTE_PROBE" "$VPS_PROJECT_PATH" "$ENV_NAME")"

case "$PROBE" in
  *MISSING_PROJECT*)
    echo "❌ $VPS_PROJECT_PATH does not exist on $VPS_HOST." >&2
    echo "   Override with: VPS_PROJECT_PATH=/path/to/repo $0" >&2
    exit 1 ;;
  *MISSING_BUILD*)
    echo "❌ $ENV_NAME has never been built on $VPS_HOST." >&2
    echo "   Re-run with --build, or on the VPS:" >&2
    echo "     pio run -e $ENV_NAME && pio run -e $ENV_NAME -t buildfs" >&2
    exit 1 ;;
esac

while IFS= read -r line; do
  case "$line" in
    REV\ *)
      set -- $line
      printf '  commit   %s (%s)' "$2" "$3"
      [ "$4" != "0" ] && printf ' — ⚠️  %s uncommitted file(s)' "$4"
      printf '\n' ;;
    FILE\ *)
      set -- $line
      if [ "$3" = "MISSING" ]; then
        printf '  %-16s (not built)\n' "$2"
      else
        printf '  %-16s %s %s  %s bytes\n' "$2" "$3" "$4" "$5"
      fi ;;
  esac
done <<< "$PROBE"

if [ "$DO_BUILD" -eq 0 ]; then
  echo
  echo "These artifacts are used as-is — re-run with --build to rebuild them first."
fi

# ------------------------------------------------------------------ fetch ----
# Only the four images that actually get flashed. The full build dir is ~60 MB
# of .elf/.map/.o that we have no use for locally.
echo
echo "Fetching build artifacts…"
mkdir -p "$BUILD_CACHE/$ENV_NAME"
rsync -az --delete \
  --include='bootloader.bin' \
  --include='partitions.bin' \
  --include='firmware.bin' \
  --include='littlefs.bin' \
  --exclude='*' \
  "$VPS_HOST:$VPS_PROJECT_PATH/.pio/build/$ENV_NAME/" \
  "$BUILD_CACHE/$ENV_NAME/"
echo "Fetched $(du -ch "$BUILD_CACHE/$ENV_NAME"/*.bin 2>/dev/null | tail -1 | cut -f1) into build_cache/$ENV_NAME/"

BOOTLOADER="$BUILD_CACHE/$ENV_NAME/bootloader.bin"
PARTITIONS="$BUILD_CACHE/$ENV_NAME/partitions.bin"
FIRMWARE="$BUILD_CACHE/$ENV_NAME/firmware.bin"
LITTLEFS="$BUILD_CACHE/$ENV_NAME/littlefs.bin"

for f in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE"; do
  if [ ! -f "$f" ]; then
    echo "❌ Missing build artifact: $f" >&2
    exit 1
  fi
done

# ------------------------------------------------------- partition offsets ---
# Partition entry layout (32 bytes):
#   magic u16 | type u8 | subtype u8 | offset u32 | size u32 | label[16] | flags u32
# Label therefore starts at +12, not +8.
{ read -r APP_OFFSET; read -r FS_OFFSET; } < <(python3 - "$PARTITIONS" <<'PY'
import struct, sys

with open(sys.argv[1], "rb") as f:
    data = f.read()

app_offset = None
fs_offset = None

for i in range(0, len(data) - 31, 32):
    if struct.unpack_from("<H", data, i)[0] != 0x50AA:
        continue
    ptype, subtype, offset, _size = struct.unpack_from("<BBII", data, i + 2)
    label = data[i+12:i+28].split(b"\x00", 1)[0].decode("ascii", "ignore").lower()
    if ptype == 0x00 and app_offset is None and subtype in (0x10, 0x00):
        app_offset = offset          # ota_0 or factory, whichever comes first
    if ptype == 0x01 and fs_offset is None:
        if subtype in (0x82, 0x83) or label in ("spiffs", "littlefs", "fs", "storage"):
            fs_offset = offset

if app_offset is None:
    print("ERROR: app partition not found in partition table", file=sys.stderr)
    sys.exit(1)

print(hex(app_offset))
print(hex(fs_offset) if fs_offset is not None else "")
PY
)

if [ -z "${APP_OFFSET:-}" ]; then
  echo "❌ Could not read the app offset from $PARTITIONS" >&2
  exit 1
fi
FS_OFFSET="${FS_OFFSET:-}"

echo
echo "App offset:        $APP_OFFSET"
[ -n "$FS_OFFSET" ] && echo "Filesystem offset: $FS_OFFSET"

FLASH_ARGS=(
  0x0     "$BOOTLOADER"     # ESP32-S3 bootloader lives at 0x0, not 0x1000
  0x8000  "$PARTITIONS"
  "$APP_OFFSET" "$FIRMWARE"
)

if [ -n "$FS_OFFSET" ]; then
  if [ ! -f "$LITTLEFS" ]; then
    echo "❌ Partition table has a filesystem partition but littlefs.bin is missing." >&2
    echo "   Re-run with --build, or on the VPS: pio run -e $ENV_NAME -t buildfs" >&2
    exit 1
  fi
  FLASH_ARGS+=("$FS_OFFSET" "$LITTLEFS")
fi

# ------------------------------------------------------------------- ports ---
list_ports() {
  ls /dev/cu.usb* /dev/cu.wchusbserial* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true
}

# Echoes the chosen port on stdout; everything else goes to stderr.
pick_port() {
  local ports=() line
  while IFS= read -r line; do
    [ -n "$line" ] && ports+=("$line")
  done < <(list_ports)

  if [ "${#ports[@]}" -eq 0 ]; then
    echo "❌ No USB serial device found (/dev/cu.usb*, /dev/ttyUSB*, /dev/ttyACM*)" >&2
    return 1
  fi

  if [ "${#ports[@]}" -eq 1 ]; then
    echo "Port: ${ports[0]}" >&2
    printf '%s\n' "${ports[0]}"
    return 0
  fi

  local choice
  echo "Multiple serial devices found — select one:" >&2
  select choice in "${ports[@]}"; do
    [ -n "${choice:-}" ] && break
  done
  printf '%s\n' "$choice"
}

# ------------------------------------------------------------------- flash ---
# erase-flash is deliberate: a first flash must clear NVS *and* otadata, so the
# bootloader falls through to ota_0 instead of booting whatever a previous
# bootstrap OTA'd into ota_1. Do not add a no-erase fast path without also
# writing boot_app0.bin at the otadata offset.
flash_board() {
  local port="$1"

  echo "  Erasing…"
  "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$port" erase-flash || return 1

  echo "  Writing…"
  "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$port" --baud "$BAUD" \
    --after no-reset write-flash -z "${FLASH_ARGS[@]}" || return 1

  # The hard reset here is the last thing that touches the board: it boots
  # straight into the firmware just written. No separate `run` step — that
  # would have to re-enter the bootloader on an already-running board, which
  # fails on the S3's native USB once the CDC port re-enumerates.
  echo "  Verifying…"
  "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$port" \
    --after hard-reset verify-flash "${FLASH_ARGS[@]}" || return 1
}

OK_COUNT=0
FAIL_COUNT=0
FIRST_PASS=1

summary() {
  echo
  echo "── Session summary ──"
  echo "  Product: $ENV_NAME"
  echo "  Flashed OK: $OK_COUNT"
  [ "$FAIL_COUNT" -gt 0 ] && echo "  Failed:     $FAIL_COUNT"
  return 0
}
trap summary EXIT

echo
while true; do
  PORT=""
  if [ "$FIRST_PASS" -eq 1 ] && [ -n "$PORT_OVERRIDE" ]; then
    PORT="$PORT_OVERRIDE"
    echo "Port: $PORT (from --port)"
  else
    PORT="$(pick_port)" || { FAIL_COUNT=$((FAIL_COUNT + 1)); }
  fi
  FIRST_PASS=0

  if [ -n "$PORT" ]; then
    echo "Flashing board #$((OK_COUNT + FAIL_COUNT + 1)) on $PORT…"
    if flash_board "$PORT"; then
      OK_COUNT=$((OK_COUNT + 1))
      echo "✅ Board #$OK_COUNT flashed and running $ENV_NAME."
    else
      FAIL_COUNT=$((FAIL_COUNT + 1))
      echo "❌ Flash failed on $PORT — leave it aside and carry on." >&2
    fi
  fi

  [ "$ONCE" -eq 1 ] && break

  echo
  printf 'Connect the next board, then press Enter (q to quit): '
  if ! read -r ans; then echo; break; fi
  case "${ans:-}" in [Qq]*) break ;; esac
  echo
done

# Non-zero exit if any board failed, so this is usable from a wrapper script.
[ "$FAIL_COUNT" -eq 0 ]
