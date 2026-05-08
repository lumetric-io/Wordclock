#!/usr/bin/env bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OTA_ROOT="/srv/ota"
OTA_BASE_URL="http://ota2.chronolett.com"
PRODUCT=""
CHANNEL=""
FW_VERSION=""
FS_VERSION=""
ASSUME_YES=false
FS_ONLY=false

read_version_from_config() {
  local file="$1"
  local key="$2"
  if [[ ! -f "$file" ]]; then
    return
  fi
  grep -E "^#define[[:space:]]+$key[[:space:]]+\"" "$file" | sed 's/.*"\(.*\)".*/\1/' | head -n 1
}

bump_last_number() {
  local v="$1"
  if [[ -z "$v" ]]; then
    echo ""
    return
  fi
  if [[ "$v" =~ ^(.*[^0-9])([0-9]+)([^0-9]*)$ ]]; then
    local prefix="${BASH_REMATCH[1]}"
    local num="${BASH_REMATCH[2]}"
    local suffix="${BASH_REMATCH[3]}"
    local next=$((10#$num + 1))
    echo "${prefix}${next}${suffix}"
    return
  fi
  echo "$v"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --product)
      PRODUCT="$2"
      shift 2
      ;;
    --channel)
      CHANNEL="$2"
      shift 2
      ;;
    --fw-version)
      FW_VERSION="$2"
      shift 2
      ;;
    --fs-version)
      FS_VERSION="$2"
      shift 2
      ;;
    --fs-only)
      FS_ONLY=true
      FW_VERSION=""
      shift
      ;;
    --yes)
      ASSUME_YES=true
      shift
      ;;
    --help|-h)
      echo "Usage:"
      echo "  ./publish-ota.sh [--product <product>] [--channel <channel>]"
      echo "                   [--fw-version <version>] [--fs-version <version>]"
      echo "                   [--yes]"
      exit 0
      ;;
    *)
      echo "❌ Unknown argument: $1"
      exit 1
      ;;
  esac
done

echo "=== OTA Publish Script ==="
echo

if [[ -z "$PRODUCT" ]]; then
  echo "Select product:"
  echo "  1) nextgen-30x30"
  echo "  2) nextgen-50x50"
  echo "  3) nextgen-logo-55x50"
  echo "  4) nextgen-logo-100x100"
  echo "  5) nextgen-mini"
  read -rp "Product number (1-5): " PRODUCT_SELECTION
  case "$PRODUCT_SELECTION" in
    1) PRODUCT="nextgen-30x30" ;;
    2) PRODUCT="nextgen-50x50" ;;
    3) PRODUCT="nextgen-logo-55x50" ;;
    4) PRODUCT="nextgen-logo-100x100" ;;
    5) PRODUCT="nextgen-mini" ;;
    *) PRODUCT="" ;;
  esac
fi

detect_chip() {
  local board
  board=$(grep -E "^board[[:space:]]*=" "$PROJECT_ROOT/platformio.ini" | head -1 | sed -E 's/^board[[:space:]]*=[[:space:]]*//; s/[[:space:]]*$//')
  case "$board" in
    esp32-s3-*) echo "esp32-s3" ;;
    esp32-c3-*) echo "esp32-c3" ;;
    *) echo "esp32" ;;
  esac
}
CHIP="$(detect_chip)"

if [[ -z "$CHANNEL" ]]; then
  read -rp "Target channel (develop / early / stable): " CHANNEL
fi

# Validate product by checking if its directory exists
if [[ ! -d "$PROJECT_ROOT/products/$PRODUCT" ]]; then
  echo "❌ Invalid product: $PRODUCT"
  exit 1
fi

PRODUCT_CONFIG="$PROJECT_ROOT/products/$PRODUCT/product_config.h"
CURRENT_FW_VERSION="$(read_version_from_config "$PRODUCT_CONFIG" "FIRMWARE_VERSION")"
CURRENT_UI_VERSION="$(read_version_from_config "$PRODUCT_CONFIG" "UI_VERSION")"
SUGGESTED_FW_VERSION="$(bump_last_number "$CURRENT_FW_VERSION")"
SUGGESTED_UI_VERSION="$(bump_last_number "$CURRENT_UI_VERSION")"

if [[ -n "$CURRENT_FW_VERSION" ]]; then
  echo "Current firmware version: $CURRENT_FW_VERSION"
  if [[ -n "$SUGGESTED_FW_VERSION" ]]; then
    echo "Suggested firmware version: $SUGGESTED_FW_VERSION"
  fi
fi
if [[ -z "$FW_VERSION" && "$FS_ONLY" != true ]]; then
  read -rp "Firmware version (leave empty for FS-only): " FW_VERSION
fi

if [[ -n "$CURRENT_UI_VERSION" ]]; then
  echo "Current UI version: $CURRENT_UI_VERSION"
  if [[ -n "$SUGGESTED_UI_VERSION" ]]; then
    echo "Suggested UI version: $SUGGESTED_UI_VERSION"
  fi
fi
if [[ -z "$FS_VERSION" ]]; then
  read -rp "UI version (leave empty to use suggested): " FS_VERSION
fi

if [[ -z "$FS_VERSION" ]]; then
  FS_VERSION="$SUGGESTED_UI_VERSION"
fi
if [[ -z "$FS_VERSION" ]]; then
  echo "❌ UI version is required"
  exit 1
fi

if [[ "$CHANNEL" != "develop" && "$CHANNEL" != "early" && "$CHANNEL" != "stable" ]]; then
  echo "❌ Invalid channel"
  exit 1
fi

channel_suffix_for() {
  case "$1" in
    develop) echo "-dev" ;;
    early) echo "-early" ;;
    *) echo "" ;;
  esac
}

apply_channel_suffix() {
  local version="$1"
  local suffix="$2"
  if [[ -z "$version" || -z "$suffix" ]]; then
    echo "$version"
    return
  fi
  local product_prefix="${PRODUCT#wordclock-}"
  local check_version="${version#ui-}"
  if [[ -n "$product_prefix" ]]; then
    if [[ "$check_version" == "$product_prefix"-* ]]; then
      check_version="${check_version#${product_prefix}-}"
    elif [[ "$check_version" == *"-$product_prefix" ]]; then
      check_version="${check_version%-${product_prefix}}"
    fi
  fi
  if [[ "$check_version" == *"-"* ]]; then
    echo "$version"
    return
  fi
  if [[ "$version" == *"$suffix" ]]; then
    echo "$version"
    return
  fi
  echo "${version}${suffix}"
}

BUILD_DIR="$PROJECT_ROOT/.pio/build/$PRODUCT"
ARTIFACT_DIR="$OTA_ROOT/$PRODUCT/artifacts/${FW_VERSION:-current}"
CHANNEL_DIR="$OTA_ROOT/$PRODUCT/channels"

CHANNEL_SUFFIX="$(channel_suffix_for "$CHANNEL")"
FW_VERSION="$(apply_channel_suffix "$FW_VERSION" "$CHANNEL_SUFFIX")"
FS_VERSION="$(apply_channel_suffix "$FS_VERSION" "$CHANNEL_SUFFIX")"

echo
echo "Publishing to:"
echo "  Product : $PRODUCT"
echo "  Channel : $CHANNEL"
echo "  FW ver  : ${FW_VERSION:-<unchanged>}"
echo "  FS ver  : $FS_VERSION"
echo "  Artifacts dir: $ARTIFACT_DIR"
echo

if [[ "$ASSUME_YES" != true ]]; then
  read -rp "Continue? [y/N]: " CONFIRM
  [[ "$CONFIRM" == "y" ]] || exit 0
fi

sudo mkdir -p "$ARTIFACT_DIR"
sudo mkdir -p "$CHANNEL_DIR"

# Ensure a firmware manifest reference exists when publishing FS-only updates
if [[ -z "$FW_VERSION" ]]; then
  EXISTING_CHANNEL="$CHANNEL_DIR/$CHANNEL.json"
  if [[ -f "$EXISTING_CHANNEL" ]]; then
    EXISTING_MANIFEST_URL="$(python3 - <<'PY' "$EXISTING_CHANNEL"
import json, sys
path = sys.argv[1]
try:
    with open(path, "r", encoding="utf-8") as f:
        doc = json.load(f)
    target = doc.get("target") or {}
    print(target.get("manifest_url") or "")
except Exception:
    print("")
PY
)"
  fi
  if [[ -z "$EXISTING_MANIFEST_URL" ]]; then
    CURRENT_MANIFEST="$OTA_ROOT/$PRODUCT/artifacts/current/manifest.json"
    if [[ ! -f "$CURRENT_MANIFEST" ]]; then
      echo "❌ Missing $CURRENT_MANIFEST (required for FS-only channel update)"
      exit 1
    fi
  fi
fi

# -------------------------
# Firmware (optional)
# -------------------------
if [[ -n "$FW_VERSION" ]]; then
  echo "→ Copying firmware.bin"
  sudo cp "$BUILD_DIR/firmware.bin" "$ARTIFACT_DIR/firmware.bin"
  sudo chown root:www-data "$ARTIFACT_DIR/firmware.bin"
  sudo chmod 644 "$ARTIFACT_DIR/firmware.bin"

  FW_SIZE=$(stat -c%s "$ARTIFACT_DIR/firmware.bin")
  FW_HASH=$(sha256sum "$ARTIFACT_DIR/firmware.bin" | awk '{print $1}')

  sudo tee "$ARTIFACT_DIR/manifest.json" > /dev/null <<EOF
{
  "schema": 1,
  "product": "$PRODUCT",
  "version": "$FW_VERSION",
  "chip": "$CHIP",
  "filesize": $FW_SIZE,
  "sha256": "$FW_HASH",
  "url": "$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/firmware.bin"
}
EOF
fi

# -------------------------
# Filesystem (required)
# -------------------------
echo "→ Copying filesystem image"

FS_TYPE="littlefs"
if [[ -f "$BUILD_DIR/littlefs.bin" ]]; then
  FS_SRC="$BUILD_DIR/littlefs.bin"
else
  echo "❌ No LittleFS image found"
  exit 1
fi

sudo cp "$FS_SRC" "$ARTIFACT_DIR/fs.bin"
sudo chown root:www-data "$ARTIFACT_DIR/fs.bin"
sudo chmod 644 "$ARTIFACT_DIR/fs.bin"

FS_SIZE=$(stat -c%s "$ARTIFACT_DIR/fs.bin")
FS_HASH=$(sha256sum "$ARTIFACT_DIR/fs.bin" | awk '{print $1}')

sudo tee "$ARTIFACT_DIR/fs.json" > /dev/null <<EOF
{
  "schema": 1,
  "product": "$PRODUCT",
  "type": "filesystem",
  "fs": "$FS_TYPE",
  "version": "$FS_VERSION",
  "filesize": $FS_SIZE,
  "sha256": "$FS_HASH",
  "url": "$OTA_BASE_URL/$PRODUCT/artifacts/${FW_VERSION:-current}/fs.bin"
}
EOF

# -------------------------
# Channel update
# -------------------------
echo "→ Updating channel: $CHANNEL"

TARGET_JSON="null"

if [[ -n "$FW_VERSION" ]]; then
  TARGET_JSON=$(cat <<EOF
{
  "version": "$FW_VERSION",
  "manifest_url": "$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/manifest.json",
  "fs_manifest_url": "$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/fs.json"
}
EOF
)
else
  EXISTING_CHANNEL="$CHANNEL_DIR/$CHANNEL.json"
  EXISTING_MANIFEST_URL=""
  EXISTING_VERSION=""
  if [[ -f "$EXISTING_CHANNEL" ]]; then
    read -r EXISTING_VERSION EXISTING_MANIFEST_URL < <(python3 - <<'PY' "$EXISTING_CHANNEL"
import json, sys
path = sys.argv[1]
try:
    with open(path, "r", encoding="utf-8") as f:
        doc = json.load(f)
    target = doc.get("target") or {}
    print((target.get("version") or "") + " " + (target.get("manifest_url") or ""))
except Exception:
    print("")
PY
)
  fi
  if [[ -z "$EXISTING_MANIFEST_URL" ]]; then
    CURRENT_MANIFEST="$OTA_ROOT/$PRODUCT/artifacts/current/manifest.json"
    if [[ ! -f "$CURRENT_MANIFEST" ]]; then
      echo "❌ Missing $CURRENT_MANIFEST (required for FS-only channel update)"
      exit 1
    fi
    EXISTING_MANIFEST_URL="$OTA_BASE_URL/$PRODUCT/artifacts/current/manifest.json"
  fi
  if [[ -n "$EXISTING_VERSION" ]]; then
    TARGET_JSON=$(cat <<EOF
{
  "version": "$EXISTING_VERSION",
  "manifest_url": "$EXISTING_MANIFEST_URL",
  "fs_manifest_url": "$OTA_BASE_URL/$PRODUCT/artifacts/current/fs.json"
}
EOF
)
  else
    TARGET_JSON=$(cat <<EOF
{
  "manifest_url": "$EXISTING_MANIFEST_URL",
  "fs_manifest_url": "$OTA_BASE_URL/$PRODUCT/artifacts/current/fs.json"
}
EOF
)
  fi
fi

sudo tee "$CHANNEL_DIR/$CHANNEL.json" > /dev/null <<EOF
{
  "schema": 1,
  "product": "$PRODUCT",
  "channel": "$CHANNEL",
  "target": $TARGET_JSON
}
EOF

sudo chown root:www-data "$CHANNEL_DIR/$CHANNEL.json"
sudo chmod 644 "$CHANNEL_DIR/$CHANNEL.json"

echo
echo "✅ OTA publish complete"
