#!/usr/bin/env bash
# Fetch nextgen-* product build artifacts from VPS and flash to a connected ESP32-S3.
#
# Usage:
#   ./tools/flash.sh
#
# Requires (locally):
#   - ssh access to $VPS_HOST (default: ron@vps-vpn — override via env var)
#   - rsync, esptool (>=5.0), python3
#
# Override the VPS host without editing this file:
#   VPS_HOST=ron@my-other-vps ./tools/flash.sh

set -e

VPS_HOST="${VPS_HOST:-ron@vps-vpn}"
VPS_PROJECT_PATH="${VPS_PROJECT_PATH:-/home/ron/repos/wordclock}"
LOCAL_PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_CACHE="$LOCAL_PROJECT_ROOT/build_cache"
ESPTOOL="${ESPTOOL:-esptool}"
CHIP="esp32s3"

mkdir -p "$BUILD_CACHE"

echo "== Wordclock S3 Flash Helper =="
echo

echo "Select product:"
select PRODUCT in \
  nextgen-30x30 \
  nextgen-50x50 \
  nextgen-logo-55x50 \
  nextgen-logo-100x100 \
  nextgen-mini; do
  [[ -n "$PRODUCT" ]] && break
done

ENV_NAME="$PRODUCT"

echo
echo "Fetching build from $VPS_HOST…"
rsync -avz --delete \
  "$VPS_HOST:$VPS_PROJECT_PATH/.pio/build/$ENV_NAME/" \
  "$BUILD_CACHE/$ENV_NAME/"
echo

echo "Available USB serial devices:"
PORTS=()
while IFS= read -r line; do
  PORTS+=("$line")
done < <(ls /dev/cu.usb* /dev/cu.wchusbserial* 2>/dev/null)

if [ "${#PORTS[@]}" -eq 0 ]; then
  echo "❌ No USB serial devices found (/dev/cu.usb* or /dev/cu.wchusbserial*)"
  exit 1
fi

select PORT in "${PORTS[@]}"; do
  [[ -n "$PORT" ]] && break
done
echo "Selected port: $PORT"
echo

BOOTLOADER="$BUILD_CACHE/$ENV_NAME/bootloader.bin"
PARTITIONS="$BUILD_CACHE/$ENV_NAME/partitions.bin"
FIRMWARE="$BUILD_CACHE/$ENV_NAME/firmware.bin"
LITTLEFS="$BUILD_CACHE/$ENV_NAME/littlefs.bin"

for f in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE"; do
  if [ ! -f "$f" ]; then
    echo "❌ Missing build artifact: $f"
    exit 1
  fi
done

echo "Reading offsets from partitions.bin…"
PARTITION_INFO=$(python3 - "$PARTITIONS" <<'PY'
import struct, sys

with open(sys.argv[1], "rb") as f:
    data = f.read()

app_offset = None
fs_offset = None

for i in range(0, len(data), 32):
    if i + 32 > len(data):
        break
    magic = struct.unpack_from("<H", data, i)[0]
    if magic != 0x50AA:
        continue
    ptype, subtype, offset, _size = struct.unpack_from("<BBII", data, i + 2)
    label = data[i+8:i+24].split(b"\x00", 1)[0].decode("ascii", "ignore").lower()
    if ptype == 0x00 and subtype == 0x10 and app_offset is None:
        app_offset = offset
    elif ptype == 0x00 and subtype == 0x00 and app_offset is None:
        app_offset = offset
    if ptype == 0x01 and fs_offset is None:
        if subtype in (0x82, 0x83) or label in ("spiffs", "littlefs", "fs", "storage"):
            fs_offset = offset

if app_offset is None:
    print("ERROR: app partition not found", file=sys.stderr)
    sys.exit(1)

print(f"APP_OFFSET={hex(app_offset)}")
if fs_offset is not None:
    print(f"FS_OFFSET={hex(fs_offset)}")
PY
)
eval "$PARTITION_INFO"

echo "Detected app offset: $APP_OFFSET"
[ -n "$FS_OFFSET" ] && echo "Detected filesystem offset: $FS_OFFSET"
echo

FLASH_FS=0
if [ -n "$FS_OFFSET" ] && [ -f "$LITTLEFS" ]; then
  FLASH_FS=1
fi

echo "Erasing flash on $PORT (esp32s3)…"
$ESPTOOL --chip "$CHIP" --port "$PORT" erase-flash
echo

# ESP32-S3 bootloader is at offset 0x0 (vs 0x1000 on classic ESP32).
FLASH_ARGS=(
  0x0 "$BOOTLOADER"
  0x8000 "$PARTITIONS"
  "$APP_OFFSET" "$FIRMWARE"
)
if [ "$FLASH_FS" -eq 1 ]; then
  FLASH_ARGS+=("$FS_OFFSET" "$LITTLEFS")
fi

echo "Flashing…"
$ESPTOOL --chip "$CHIP" --port "$PORT" --baud 460800 --after no-reset write-flash -z "${FLASH_ARGS[@]}"
echo

$ESPTOOL --chip "$CHIP" --port "$PORT" verify-flash "${FLASH_ARGS[@]}"
echo

$ESPTOOL --chip "$CHIP" --port "$PORT" --after hard-reset run

echo
echo "✅ Flash completed."
