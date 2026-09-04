#!/usr/bin/env bash
# Flash a product build to ESP32-S3 boards attached to this machine.
#
# Tuned for the nextgen-bootstrap first-flash workflow: resolve the build once,
# then flash board after board in a loop. Each board is erased, written and
# verified; the verify step's hard reset leaves the board running the freshly
# flashed firmware (bootstrap comes up as a Wi-Fi AP ready to provision).
#
# The build is read straight out of .pio/build/<env>/ in this checkout. There is
# no fetch step: the boards hang off this machine's USB and the firmware is
# built here, so nothing needs to cross the network.
#
# Usage:
#   ./tools/flash.sh                          # nextgen-bootstrap, flash-many loop
#   ./tools/flash.sh --build                  # (re)build first, then flash
#   ./tools/flash.sh -p nextgen-50x50         # a different product
#   ./tools/flash.sh -P /dev/ttyACM0          # skip port detection
#   ./tools/flash.sh --once                   # flash a single board and exit
#   ./tools/flash.sh --menu                   # pick the product interactively
#
# Requires: python3, and esptool -- either a standalone one on PATH or the copy
# PlatformIO already ships, which is found automatically. Both the 4.x
# (write_flash) and 5.x (write-flash) command spellings are handled.
#
# Environment overrides:
#   ESPTOOL   esptool command      (default: PATH, else PlatformIO's bundled one)
#   PIO       platformio command   (default: PATH, else ~/.platformio/penv/bin/pio)
#   BAUD      write baud rate      (default: 460800)
#   PORT_WAIT_SECS  seconds to wait for a serial port (default: 30)

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="$PROJECT_ROOT/.pio/build"
BAUD="${BAUD:-460800}"
PORT_WAIT_SECS="${PORT_WAIT_SECS:-30}"
CHIP="esp32s3"
DEFAULT_PRODUCT="nextgen-bootstrap"

PRODUCT=""
PORT_OVERRIDE=""
DO_BUILD=0
ONCE=0
MENU=0

die() { echo "❌ $*" >&2; exit 1; }

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
# Derived from products/ so it cannot drift out of sync with platformio.ini
# the way a hardcoded list did.
PRODUCTS=()
for d in "$PROJECT_ROOT"/products/nextgen-*/; do
  [ -d "$d" ] || continue
  name="$(basename "$d")"
  [ "$name" = "$DEFAULT_PRODUCT" ] && continue
  PRODUCTS+=("$name")
done

# Bootstrap goes first: it is the common case. Guard the empty-array expansion --
# macOS ships bash 3.2, where "${arr[@]}" on an empty array trips `set -u`.
if [ -d "$PROJECT_ROOT/products/$DEFAULT_PRODUCT" ]; then
  if [ "${#PRODUCTS[@]}" -eq 0 ]; then
    PRODUCTS=("$DEFAULT_PRODUCT")
  else
    PRODUCTS=("$DEFAULT_PRODUCT" "${PRODUCTS[@]}")
  fi
fi

[ "${#PRODUCTS[@]}" -gt 0 ] || die "No products/nextgen-*/ found under $PROJECT_ROOT"

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

printf '%s\n' "${PRODUCTS[@]}" | grep -qx -- "$PRODUCT" || {
  echo "❌ Unknown product: $PRODUCT" >&2
  echo "   Available: ${PRODUCTS[*]}" >&2
  exit 1
}
ENV_NAME="$PRODUCT"
BUILD_DIR="$BUILD_ROOT/$ENV_NAME"
echo

# --------------------------------------------------------------- esptool ----
# PlatformIO already ships an esptool, so a standalone install is optional.
# 4.x spells the subcommands with underscores and 5.x with dashes; both are
# supported rather than demanding a particular version be installed.
ESPTOOL_CMD=()
if [ -n "${ESPTOOL:-}" ]; then
  read -r -a ESPTOOL_CMD <<< "$ESPTOOL"
elif command -v esptool > /dev/null 2>&1; then
  ESPTOOL_CMD=(esptool)
else
  for candidate in "$HOME"/.platformio/packages/tool-esptoolpy/esptool.py; do
    [ -f "$candidate" ] || continue
    if [ -x "$HOME/.platformio/penv/bin/python" ]; then
      ESPTOOL_CMD=("$HOME/.platformio/penv/bin/python" "$candidate")
    else
      ESPTOOL_CMD=(python3 "$candidate")
    fi
    break
  done
fi

[ "${#ESPTOOL_CMD[@]}" -gt 0 ] || die "No esptool found.
   Install one (pip install esptool) or set ESPTOOL=/path/to/esptool"

ESPTOOL_VERSION="$("${ESPTOOL_CMD[@]}" version 2>/dev/null | head -1 || true)"
[ -n "$ESPTOOL_VERSION" ] || die "Cannot run '${ESPTOOL_CMD[*]}'"

ESPTOOL_MAJOR="$(printf '%s' "$ESPTOOL_VERSION" | grep -oE '[0-9]+\.[0-9]+' | head -1 | cut -d. -f1 || true)"
: "${ESPTOOL_MAJOR:=4}"

if [ "$ESPTOOL_MAJOR" -ge 5 ]; then
  OP_ERASE="erase-flash"; OP_WRITE="write-flash"; OP_VERIFY="verify-flash"
  AFTER_NONE="no-reset";  AFTER_HARD="hard-reset"
else
  OP_ERASE="erase_flash"; OP_WRITE="write_flash"; OP_VERIFY="verify_flash"
  AFTER_NONE="no_reset";  AFTER_HARD="hard_reset"
fi

echo "esptool: $ESPTOOL_VERSION  (${OP_WRITE} spelling)"

# ------------------------------------------------------------------ build ---
resolve_pio() {
  if [ -n "${PIO:-}" ]; then printf '%s\n' "$PIO"; return 0; fi
  if command -v pio > /dev/null 2>&1; then printf '%s\n' "pio"; return 0; fi
  if [ -x "$HOME/.platformio/penv/bin/pio" ]; then
    printf '%s\n' "$HOME/.platformio/penv/bin/pio"; return 0
  fi
  return 1
}

if [ "$DO_BUILD" -eq 1 ]; then
  PIO_BIN="$(resolve_pio)" || die "pio not found. Install PlatformIO or set PIO=/path/to/pio"
  echo
  echo "Building $ENV_NAME (firmware + filesystem)…"
  ( cd "$PROJECT_ROOT" && "$PIO_BIN" run -e "$ENV_NAME" && "$PIO_BIN" run -e "$ENV_NAME" -t buildfs )
  echo "Build finished."
fi

if [ ! -d "$BUILD_DIR" ]; then
  die "$ENV_NAME has never been built.
   Re-run with --build, or:
     pio run -e $ENV_NAME && pio run -e $ENV_NAME -t buildfs"
fi

BOOTLOADER="$BUILD_DIR/bootloader.bin"
PARTITIONS="$BUILD_DIR/partitions.bin"
FIRMWARE="$BUILD_DIR/firmware.bin"
LITTLEFS="$BUILD_DIR/littlefs.bin"

# ------------------------------------------------------------- provenance ---
# What is about to be flashed, so a stale build is visible before it reaches a
# board rather than after.
echo
echo "Build: .pio/build/$ENV_NAME/"
if git -C "$PROJECT_ROOT" rev-parse --git-dir > /dev/null 2>&1; then
  rev="$(git -C "$PROJECT_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  branch="$(git -C "$PROJECT_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
  dirty="$(git -C "$PROJECT_ROOT" status --porcelain 2>/dev/null | grep -c . || true)"
  printf '  commit   %s (%s)' "$rev" "$branch"
  [ "$dirty" != "0" ] && printf ' — ⚠️  %s uncommitted file(s)' "$dirty"
  printf '\n'
fi
for f in bootloader.bin partitions.bin firmware.bin littlefs.bin; do
  if [ -f "$BUILD_DIR/$f" ]; then
    printf '  %-16s %s  %s bytes\n' "$f" \
      "$(date -r "$BUILD_DIR/$f" '+%Y-%m-%d %H:%M' 2>/dev/null || echo '?')" \
      "$(wc -c < "$BUILD_DIR/$f" | tr -d ' ')"
  else
    printf '  %-16s (not built)\n' "$f"
  fi
done

for f in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE"; do
  [ -f "$f" ] || die "Missing build artifact: $f
   Re-run with --build."
done

if [ "$DO_BUILD" -eq 0 ]; then
  echo
  echo "Flashed as-is — re-run with --build to rebuild first."
fi

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

[ -n "${APP_OFFSET:-}" ] || die "Could not read the app offset from $PARTITIONS"
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
  [ -f "$LITTLEFS" ] || die "Partition table has a filesystem partition but littlefs.bin is missing.
   Re-run with --build, or: pio run -e $ENV_NAME -t buildfs"
  FLASH_ARGS+=("$FS_OFFSET" "$LITTLEFS")
fi

# ------------------------------------------------------------------- ports ---
list_ports() {
  ls /dev/ttyACM* /dev/ttyUSB* /dev/cu.usb* /dev/cu.wchusbserial* 2>/dev/null || true
}

# A device node can outlive the device. After the S3 hard-resets it re-enumerates
# on the USB bus, and in a container with a passed-through node the old /dev entry
# lingers while every open returns ENXIO. `test -e` and `test -w` both pass on such
# a node, so the only honest liveness test is to open it.
#
# Probing does open the port, which on the S3's native USB can pulse DTR/RTS and
# reset the board. Harmless here: we only probe immediately before flashing, and
# esptool opens the same port moments later anyway.
port_is_live() {
  python3 - "$1" <<'PROBE' 2>/dev/null
import os, sys
try:
    fd = os.open(sys.argv[1], os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
except OSError:
    sys.exit(1)
os.close(fd)
PROBE
}

# Echoes a usable port on stdout; everything else goes to stderr. With an
# argument, waits for that specific port; without one, for any live port.
#
# The wait is the point: between boards the port is legitimately absent for a
# moment, and after each flash's closing hard reset it re-enumerates. Failing on
# the first look would break the flash-many loop on the second board.
pick_port() {
  local want="${1:-}"
  local deadline=$(( $(date +%s) + PORT_WAIT_SECS ))
  local announced=0
  local ports=() line chosen

  while :; do
    ports=()
    if [ -n "$want" ]; then
      port_is_live "$want" && ports=("$want")
    else
      while IFS= read -r line; do
        [ -n "$line" ] || continue
        port_is_live "$line" && ports+=("$line")
      done < <(list_ports)
    fi

    [ "${#ports[@]}" -gt 0 ] && break

    if [ "$(date +%s)" -ge "$deadline" ]; then
      echo "❌ No usable serial port after ${PORT_WAIT_SECS}s${want:+ ($want)}." >&2
      # Tell the stale-node case apart from a genuinely absent board: a stale
      # node still looks present in /dev, so "not found" would be misleading.
      # Diagnose only what was actually asked for -- with --port, another port
      # being stale is not the caller's problem.
      local stale=0
      local candidates
      if [ -n "$want" ]; then
        if [ ! -e "$want" ]; then
          echo "   $want does not exist." >&2
          return 1
        fi
        candidates="$want"
      else
        candidates="$(list_ports)"
      fi
      for line in $candidates; do
        port_is_live "$line" || { echo "   $line exists but will not open." >&2; stale=1; }
      done
      if [ "$stale" -eq 1 ]; then
        echo "   The device re-enumerated and this node is stale." >&2
        echo "   Replug the board, or re-attach it on the host." >&2
      else
        echo "   Looked for /dev/ttyACM*, /dev/ttyUSB*, /dev/cu.usb*" >&2
      fi
      return 1
    fi

    if [ "$announced" -eq 0 ]; then
      echo "Waiting up to ${PORT_WAIT_SECS}s for a serial port..." >&2
      announced=1
    fi
    sleep 0.5
  done

  if [ "${#ports[@]}" -eq 1 ]; then
    chosen="${ports[0]}"
    echo "Port: $chosen" >&2
  else
    echo "Multiple serial devices found -- select one:" >&2
    select chosen in "${ports[@]}"; do
      [ -n "${chosen:-}" ] && break
    done
  fi

  if [ ! -w "$chosen" ]; then
    echo "❌ $chosen is not writable by $(id -un)." >&2
    echo "   $(ls -l "$chosen" 2>/dev/null)" >&2
    echo "   Add yourself to the owning group, or run as the owning user." >&2
    return 1
  fi

  printf '%s\n' "$chosen"
}

# ------------------------------------------------------------------- flash ---
# erase is deliberate: a first flash must clear NVS *and* otadata, so the
# bootloader falls through to ota_0 instead of booting whatever a previous
# bootstrap OTA'd into ota_1. Do not add a no-erase fast path without also
# writing boot_app0.bin at the otadata offset.
flash_board() {
  local port="$1"

  echo "  Erasing…"
  "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$port" "$OP_ERASE" || return 1

  echo "  Writing…"
  "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$port" --baud "$BAUD" \
    --after "$AFTER_NONE" "$OP_WRITE" -z "${FLASH_ARGS[@]}" || return 1

  # The hard reset here is the last thing that touches the board: it boots
  # straight into the firmware just written. No separate `run` step — that
  # would have to re-enter the bootloader on an already-running board, which
  # fails on the S3's native USB once the CDC port re-enumerates.
  echo "  Verifying…"
  "${ESPTOOL_CMD[@]}" --chip "$CHIP" --port "$port" \
    --after "$AFTER_HARD" "$OP_VERIFY" "${FLASH_ARGS[@]}" || return 1
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
    PORT="$(pick_port "$PORT_OVERRIDE")" || FAIL_COUNT=$((FAIL_COUNT + 1))
  else
    PORT="$(pick_port)" || FAIL_COUNT=$((FAIL_COUNT + 1))
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
