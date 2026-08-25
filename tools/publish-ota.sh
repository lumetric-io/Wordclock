#!/usr/bin/env bash
#
# publish-ota.sh - OTA2 publisher for the frozen legacy ESP32 wordclock line.
#
# Ships an already-built firmware + LittleFS image to the OTA2 server
# (ota2.chronolett.com, served from /srv/ota on this VPS) and repoints a release
# channel at it, using the exact JSON contract the legacy firmware's OTA2 client
# parses in src/ota_updater.cpp (checkForFirmwareUpdateV2).
#
# The legacy line is FROZEN. Unlike nextgen there is no tagging, no GitHub
# release, and no CI. This script does the one thing that actually reaches a
# field clock: write the artifact tree and move the channel pointer. Build the
# bits first with tools/release.sh, then publish them here.
#
# /srv/ota is root-owned, so a real publish needs root: Ron runs it with sudo.
# --dry-run needs no privileges and writes nothing, so a build can be validated
# end to end (paths, versions, sizes, sha256, and the exact JSON) before shipping.
#
# Contract (keep in exact sync with src/ota_updater.cpp):
#   channels/<channel>.json
#     { "schema":1, "product":<id>, "channel":<name>,
#       "target": { "version":<fw>, "manifest_url":<...>, "fs_manifest_url":<...> } }
#   artifacts/<fw>/manifest.json                                     (firmware)
#     { "schema":1, "product":<id>, "version":<fw>, "chip":<chip>,
#       "filesize":<n>, "sha256":<hex>, "url":<.../firmware.bin> }
#   artifacts/<fw>/fs.json                                           (filesystem)
#     { "schema":1, "product":<id>, "type":"filesystem", "fs":"littlefs",
#       "version":<ui>, "filesize":<n>, "sha256":<hex>, "url":<.../fs.bin> }
#
# where <fw> is the full FIRMWARE_VERSION string, product prefix included
# (e.g. legacy-nl-v4-26.2.11-rc.10), and <ui> is UI_VERSION (ui-<...>). The
# artifact directory is named <fw> verbatim. NOTE: the legacy line does not
# apply -dev / -early channel suffixes; the version is whatever product_config.h
# (or --fw-version) says, and the same version can be numbered per channel by
# hand (develop has historically led early by one rc, e.g. rc.10 vs rc.9).
#
# Usage:
#   tools/publish-ota.sh -p <product> -c <channel> [options]           # publish a build
#   tools/publish-ota.sh -p <product> -c <channel> --repoint <fw>      # move a channel, no rebuild
#
# Options:
#   -p, --product <id>     OTA product id == build env == /srv/ota dir
#                          (e.g. wordclock-legacy-nl-v4)
#   -c, --channel <name>   stable | early | develop
#       --fw-version <v>   firmware version (default: FIRMWARE_VERSION from
#                          products/<build-env>/product_config.h)
#       --fs-version <v>   filesystem version (default: UI_VERSION, else ui-<fw>)
#       --build-env <env>  PlatformIO env / product dir (default: same as --product)
#       --build-dir <dir>  where firmware.bin/littlefs.bin live
#                          (default: .pio/build/<build-env>)
#       --chip <chip>      manifest chip field (default: esp32)
#       --repoint <fw>     point the channel at an already-published artifact <fw>;
#                          no copy, no build. The rollback / promote path.
#       --no-fs            firmware-only publish: the channel json carries an
#                          empty fs_manifest_url, so clocks skip the filesystem
#                          half entirely. Every field generation guards on this
#                          (26.2.8 up). Use to ship a firmware fix to clocks
#                          whose current firmware applies fs updates unsafely.
#                          Composes with --repoint.
#       --fs-from <fw>     pin the channel's fs_manifest_url to another already
#                          published artifact's fs.json instead of this one's.
#                          Clocks compare fs versions by equality, so pointing
#                          at the fs the fleet already runs means "firmware
#                          only, fs untouched" while keeping a valid fs
#                          manifest in place. Mutually exclusive with --no-fs.
#                          Composes with --repoint and with a normal publish.
#       --create-product   allow publishing to a product dir that does not exist yet
#       --dry-run          print everything that would be written; touch nothing
#   -y, --yes              do not prompt for confirmation
#   -h, --help
#
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OTA_ROOT="/srv/ota"
OTA_BASE_URL="http://ota2.chronolett.com"

PRODUCT=""
CHANNEL=""
FW_VERSION=""
FS_VERSION=""
BUILD_ENV=""
BUILD_DIR=""
CHIP="esp32"
REPOINT_VERSION=""
NO_FS=false
FS_FROM_VERSION=""
CREATE_PRODUCT=false
DRY_RUN=false
ASSUME_YES=false

die() { echo "ERROR: $*" >&2; exit 1; }

check_fs_from() {
  # The --fs-from target must already be published; dry-run only warns so a
  # plan can be validated before the artifact lands.
  local f="$PRODUCT_DIR/artifacts/$FS_FROM_VERSION/fs.json"
  if [[ -f "$f" ]]; then return 0; fi
  if [[ "$DRY_RUN" == true ]]; then
    echo "WARNING: fs manifest not found: $f (would fail on a real run)"
  else
    die "fs manifest not found: $f
     nothing published under that version. Check the name."
  fi
}

read_define() {
  # read_define <file> <macro> -> the string literal, or empty
  local file="$1" key="$2"
  [[ -f "$file" ]] || return 0
  grep -E "^#define[[:space:]]+$key[[:space:]]+\"" "$file" \
    | sed 's/.*"\(.*\)".*/\1/' | head -n 1 || true
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--product)        PRODUCT="$2"; shift 2 ;;
    -c|--channel)        CHANNEL="$2"; shift 2 ;;
    --fw-version)        FW_VERSION="$2"; shift 2 ;;
    --fs-version)        FS_VERSION="$2"; shift 2 ;;
    --build-env)         BUILD_ENV="$2"; shift 2 ;;
    --build-dir)         BUILD_DIR="$2"; shift 2 ;;
    --chip)              CHIP="$2"; shift 2 ;;
    --repoint)           REPOINT_VERSION="$2"; shift 2 ;;
    --no-fs)             NO_FS=true; shift ;;
    --fs-from)           FS_FROM_VERSION="$2"; shift 2 ;;
    --create-product)    CREATE_PRODUCT=true; shift ;;
    --dry-run)           DRY_RUN=true; shift ;;
    -y|--yes)            ASSUME_YES=true; shift ;;
    -h|--help)           sed -n '2,71p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *)                   die "unknown argument: $1 (try --help)" ;;
  esac
done

[[ -n "$PRODUCT" ]] || die "missing --product (e.g. wordclock-legacy-nl-v4)"
[[ -n "$CHANNEL" ]] || die "missing --channel (stable|early|develop)"
case "$CHANNEL" in
  stable|early|develop) ;;
  *) die "invalid --channel '$CHANNEL' (stable|early|develop)" ;;
esac

if [[ "$NO_FS" == true && -n "$FS_FROM_VERSION" ]]; then
  die "--no-fs and --fs-from are mutually exclusive"
fi

PRODUCT_DIR="$OTA_ROOT/$PRODUCT"
CHANNEL_DIR="$PRODUCT_DIR/channels"

if [[ ! -d "$PRODUCT_DIR" && "$CREATE_PRODUCT" != true ]]; then
  die "product dir does not exist: $PRODUCT_DIR
     refusing to create it (typo guard). Pass --create-product to seed a new one."
fi

# Real writes to /srv/ota need root; dry-run never writes.
SUDO=""
if [[ "$DRY_RUN" != true && "$(id -u)" -ne 0 ]]; then
  SUDO="sudo"
fi

emit_json() {
  # emit_json <path> <content>
  local path="$1" content="$2"
  if [[ "$DRY_RUN" == true ]]; then
    echo "---- would write $path ----"
    echo "$content"
    echo
    return
  fi
  printf '%s\n' "$content" | $SUDO tee "$path" >/dev/null
  $SUDO chown root:www-data "$path"
  $SUDO chmod 644 "$path"
}

write_channel() {
  # write_channel <fw_version> <manifest_url> <fs_manifest_url>
  local ver="$1" murl="$2" fsurl="$3"
  $DRY_RUN || $SUDO mkdir -p "$CHANNEL_DIR"
  emit_json "$CHANNEL_DIR/$CHANNEL.json" "$(cat <<EOF
{
  "schema": 1,
  "product": "$PRODUCT",
  "channel": "$CHANNEL",
  "target": {
    "version": "$ver",
    "manifest_url": "$murl",
    "fs_manifest_url": "$fsurl"
  }
}
EOF
)"
}

# ---------------------------------------------------------------------------
# Repoint mode: move a channel onto an already-published artifact. No build,
# no copy. This is the rollback (or promote) path, e.g. point develop back at a
# known-good rc, or promote a tested artifact to stable.
# ---------------------------------------------------------------------------
if [[ -n "$REPOINT_VERSION" ]]; then
  ART_DIR="$PRODUCT_DIR/artifacts/$REPOINT_VERSION"
  MURL="$OTA_BASE_URL/$PRODUCT/artifacts/$REPOINT_VERSION/manifest.json"
  FSURL="$OTA_BASE_URL/$PRODUCT/artifacts/$REPOINT_VERSION/fs.json"
  if [[ "$NO_FS" == true ]]; then FSURL=""; fi
  if [[ -n "$FS_FROM_VERSION" ]]; then
    FSURL="$OTA_BASE_URL/$PRODUCT/artifacts/$FS_FROM_VERSION/fs.json"
    check_fs_from
  fi

  if [[ ! -f "$ART_DIR/manifest.json" ]]; then
    if [[ "$DRY_RUN" == true ]]; then
      echo "WARNING: artifact not found: $ART_DIR/manifest.json (would fail on a real run)"
    else
      die "artifact not found: $ART_DIR/manifest.json
     nothing published under that version. Publish it first, or check the name."
    fi
  fi

  echo "=== OTA repoint (legacy) ==="
  echo "  product : $PRODUCT"
  echo "  channel : $CHANNEL   ->   $REPOINT_VERSION"
  echo "  manifest: $MURL"
  if [[ "$NO_FS" == true ]]; then echo "  fs half : SKIPPED (--no-fs)"; fi
  if [[ -n "$FS_FROM_VERSION" ]]; then echo "  fs half : pinned to $FS_FROM_VERSION"; fi
  echo "  dry-run : $DRY_RUN"
  echo
  if [[ "$DRY_RUN" != true && "$ASSUME_YES" != true ]]; then
    read -rp "Repoint $CHANNEL to $REPOINT_VERSION? [y/N]: " ok
    [[ "$ok" == "y" || "$ok" == "Y" ]] || { echo "aborted."; exit 0; }
  fi
  write_channel "$REPOINT_VERSION" "$MURL" "$FSURL"
  echo "Done. Verify: curl -s $OTA_BASE_URL/$PRODUCT/channels/$CHANNEL.json"
  exit 0
fi

# ---------------------------------------------------------------------------
# Publish mode: copy a fresh build into a new artifact dir and point the channel.
# ---------------------------------------------------------------------------
[[ -n "$BUILD_ENV" ]] || BUILD_ENV="$PRODUCT"
[[ -n "$BUILD_DIR" ]] || BUILD_DIR="$PROJECT_ROOT/.pio/build/$BUILD_ENV"
PRODUCT_CONFIG="$PROJECT_ROOT/products/$BUILD_ENV/product_config.h"

# Version defaults come from product_config.h, so a plain build publishes the
# version the firmware itself reports (FIRMWARE_VERSION), and the FS image
# carries UI_VERSION, exactly as the running clock compares them.
if [[ -z "$FW_VERSION" ]]; then
  FW_VERSION="$(read_define "$PRODUCT_CONFIG" "FIRMWARE_VERSION")"
  [[ -n "$FW_VERSION" ]] || die "could not read FIRMWARE_VERSION from $PRODUCT_CONFIG; pass --fw-version"
fi
if [[ -z "$FS_VERSION" ]]; then
  FS_VERSION="$(read_define "$PRODUCT_CONFIG" "UI_VERSION")"
  [[ -n "$FS_VERSION" ]] || FS_VERSION="ui-$FW_VERSION"
fi

FW_SRC="$BUILD_DIR/firmware.bin"
FS_SRC="$BUILD_DIR/littlefs.bin"
[[ -f "$FW_SRC" ]] || die "firmware image not found: $FW_SRC (build it: tools/release.sh -p $BUILD_ENV)"
if [[ "$NO_FS" != true && -z "$FS_FROM_VERSION" ]]; then
  [[ -f "$FS_SRC" ]] || die "littlefs image not found: $FS_SRC (build it: tools/release.sh -p $BUILD_ENV --fs)"
fi

ART_DIR="$PRODUCT_DIR/artifacts/$FW_VERSION"
FW_URL="$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/firmware.bin"
FS_URL="$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/fs.bin"
MURL="$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/manifest.json"
FSURL="$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/fs.json"
if [[ -n "$FS_FROM_VERSION" ]]; then
  FSURL="$OTA_BASE_URL/$PRODUCT/artifacts/$FS_FROM_VERSION/fs.json"
  check_fs_from
fi

# Sizes and hashes are computed from the source images, so --dry-run reports the
# real numbers that would be published without touching /srv/ota.
FW_SIZE="$(stat -c%s "$FW_SRC")"
FW_HASH="$(sha256sum "$FW_SRC" | awk '{print $1}')"
FS_SIZE=0
FS_HASH=""
if [[ "$NO_FS" != true && -z "$FS_FROM_VERSION" ]]; then
  FS_SIZE="$(stat -c%s "$FS_SRC")"
  FS_HASH="$(sha256sum "$FS_SRC" | awk '{print $1}')"
fi

echo "=== OTA publish (legacy) ==="
echo "  product   : $PRODUCT"
echo "  channel   : $CHANNEL"
echo "  fw version: $FW_VERSION"
echo "  fs version: $FS_VERSION"
echo "  chip      : $CHIP"
echo "  build dir : $BUILD_DIR"
echo "  firmware  : $FW_SIZE bytes  sha256 $FW_HASH"
if [[ "$NO_FS" == true ]]; then
  echo "  filesystem: SKIPPED (--no-fs, channel carries no fs_manifest_url)"
elif [[ -n "$FS_FROM_VERSION" ]]; then
  echo "  filesystem: pinned to $FS_FROM_VERSION (no fs image published)"
else
  echo "  filesystem: $FS_SIZE bytes  sha256 $FS_HASH"
fi
echo "  artifacts : $ART_DIR"
echo "  dry-run   : $DRY_RUN"
echo

if [[ -f "$ART_DIR/manifest.json" && "$DRY_RUN" != true ]]; then
  echo "NOTE: $ART_DIR already exists and will be overwritten."
fi
if [[ "$DRY_RUN" != true && "$ASSUME_YES" != true ]]; then
  read -rp "Publish to $PRODUCT/$CHANNEL? [y/N]: " ok
  [[ "$ok" == "y" || "$ok" == "Y" ]] || { echo "aborted."; exit 0; }
fi

$DRY_RUN || $SUDO mkdir -p "$ART_DIR"

# firmware.bin
if [[ "$DRY_RUN" == true ]]; then
  echo "---- would copy $FW_SRC -> $ART_DIR/firmware.bin ----"
else
  $SUDO cp "$FW_SRC" "$ART_DIR/firmware.bin"
  $SUDO chown root:www-data "$ART_DIR/firmware.bin"
  $SUDO chmod 644 "$ART_DIR/firmware.bin"
fi
emit_json "$ART_DIR/manifest.json" "$(cat <<EOF
{
  "schema": 1,
  "product": "$PRODUCT",
  "version": "$FW_VERSION",
  "chip": "$CHIP",
  "filesize": $FW_SIZE,
  "sha256": "$FW_HASH",
  "url": "$FW_URL"
}
EOF
)"

# fs.bin
if [[ "$NO_FS" == true ]]; then
  FSURL=""
elif [[ -n "$FS_FROM_VERSION" ]]; then
  : # fs half pinned to another artifact; nothing to copy
elif [[ "$DRY_RUN" == true ]]; then
  echo "---- would copy $FS_SRC -> $ART_DIR/fs.bin ----"
else
  $SUDO cp "$FS_SRC" "$ART_DIR/fs.bin"
  $SUDO chown root:www-data "$ART_DIR/fs.bin"
  $SUDO chmod 644 "$ART_DIR/fs.bin"
fi
if [[ "$NO_FS" != true && -z "$FS_FROM_VERSION" ]]; then
emit_json "$ART_DIR/fs.json" "$(cat <<EOF
{
  "schema": 1,
  "product": "$PRODUCT",
  "type": "filesystem",
  "fs": "littlefs",
  "version": "$FS_VERSION",
  "filesize": $FS_SIZE,
  "sha256": "$FS_HASH",
  "url": "$FS_URL"
}
EOF
)"
fi

# channel pointer (written last, so a clock never sees a target before its files)
write_channel "$FW_VERSION" "$MURL" "$FSURL"

echo "Done."
echo "Verify channel : curl -s $OTA_BASE_URL/$PRODUCT/channels/$CHANNEL.json"
echo "Verify firmware: curl -sI $FW_URL | head -n1"
