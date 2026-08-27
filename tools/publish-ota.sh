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
FORCE_FS_VERSION=false
NO_FS=false
FS_FROM_VERSION=""
FS_UNCHANGED=""
REPOINT_VERSION=""
DRY_RUN=false

# nextgen-logo-105x105 was previously shipped as nextgen-logo-100x100. Any
# channel write for the new id is mirrored to the old one (see the mirror
# sections in both repoint and publish mode) so units still polling the old
# PRODUCT_ID keep receiving updates.
declare -A LEGACY_PRODUCT_ALIAS=(
  ["nextgen-logo-105x105"]="nextgen-logo-100x100"
)

# Reads one string field out of a JSON file, walking nested keys.
# Prints an empty line on anything unexpected (missing file, missing key,
# non-string value), because every caller treats "unknown" as "do nothing".
read_json_field() {
  local file="$1"
  shift
  python3 - "$file" "$@" <<'PY'
import json, sys
path = sys.argv[1]
keys = sys.argv[2:]
try:
    with open(path, "r", encoding="utf-8") as f:
        doc = json.load(f)
    for key in keys:
        if not isinstance(doc, dict):
            print("")
            sys.exit(0)
        doc = doc.get(key)
        if doc is None:
            print("")
            sys.exit(0)
    print(doc if isinstance(doc, str) else "")
except Exception:
    print("")
PY
}

# Locates mklittlefs, which PlatformIO installs as a package rather than on
# PATH. Prints an empty line when it cannot be found: the content comparison is
# an optimisation, and losing it must never stop a release.
find_mklittlefs() {
  if command -v mklittlefs > /dev/null 2>&1; then
    command -v mklittlefs
    return
  fi
  local candidate
  for candidate in "$HOME"/.platformio/packages/tool-mklittlefs*/mklittlefs; do
    if [[ -x "$candidate" ]]; then
      echo "$candidate"
      return
    fi
  done
  echo ""
}

# Hashes what a LittleFS image CONTAINS rather than the bytes it is made of.
#
# mklittlefs stamps the build time into the filesystem metadata, so packing the
# same directory twice a second apart produces two different images. That was
# measured, not assumed: two builds of an unchanged data/ differed in 17 bytes
# out of 3 MB, at offsets 0xC44 and 0x1FDE, both decoding to the build time,
# and unpacking the two gave identical trees. So a byte comparison between
# releases can only ever say "different", which is why the guard below needs
# this instead.
#
# The hash covers every file's relative path and contents, in a fixed order.
# Empty output means the image could not be read, and every caller treats that
# as "cannot tell", never as "unchanged".
fs_content_hash() {
  local image="$1"
  local mk
  mk="$(find_mklittlefs)"
  if [[ -z "$mk" || ! -f "$image" ]]; then
    echo ""
    return
  fi
  local tmp
  tmp="$(mktemp -d)"
  # A corrupt image makes mklittlefs abort on a signal, and the shell that waits
  # for it prints "Aborted (core dumped)" into the middle of a release. The
  # trailing 'exit' is what stops that: without a second command bash execs
  # mklittlefs in place of the subshell, so the parent does the waiting and the
  # redirect below never gets near the message.
  if ! ( "$mk" -u "$tmp" -b 4096 -p 256 -s "$(stat -c%s "$image")" "$image"; exit $? ) > /dev/null 2>&1; then
    rm -rf "$tmp"
    echo ""
    return
  fi
  local hash
  hash="$( (cd "$tmp" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) \
           | sha256sum | awk '{print $1}')"
  rm -rf "$tmp"
  echo "$hash"
}

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
    -p|--product)
      PRODUCT="$2"
      shift 2
      ;;
    -c|--channel)
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
    --no-fs)
      NO_FS=true
      shift
      ;;
    --fs-from)
      FS_FROM_VERSION="$2"
      shift 2
      ;;
    --force-fs-version)
      FORCE_FS_VERSION=true
      shift
      ;;
    -y|--yes)
      ASSUME_YES=true
      shift
      ;;
    --repoint)
      REPOINT_VERSION="$2"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=true
      shift
      ;;
    --help|-h)
      echo "Usage:"
      echo "  ./publish-ota.sh [-p <product>] [-c <channel>]"
      echo "                   [--fw-version <version>] [--fs-version <version>]"
      echo "                   [--no-fs] [--fs-from <fw>]"
      echo "                   [--force-fs-version] [-y]"
      echo "  ./publish-ota.sh -p <product> -c <channel> --repoint <fw>"
      echo "                   [--no-fs | --fs-from <fw>] [--dry-run] [-y]"
      echo
      echo "  -p/--product, -c/--channel and -y/--yes work long or short."
      echo
      echo "  --force-fs-version  Publish under the given FS version even when the"
      echo "                      image contains exactly what the published one"
      echo "                      does. Without it, an image whose contents are"
      echo "                      unchanged keeps its old version, so devices skip"
      echo "                      the filesystem write and keep their logs."
      echo
      echo "  --no-fs             Firmware-only publish: the channel json carries an"
      echo "                      empty fs_manifest_url, so devices skip the"
      echo "                      filesystem half entirely (ota_updater guards on"
      echo "                      the empty string). Use to ship a firmware fix to"
      echo "                      devices whose CURRENT firmware applies fs updates"
      echo "                      unsafely: the fs half of an update runs under the"
      echo "                      old firmware, so it must not be offered until the"
      echo "                      fleet reboots into the fix."
      echo "  --fs-from <fw>      Pin the channel's fs_manifest_url to an already"
      echo "                      published artifact's fs.json instead of this"
      echo "                      one's. Devices compare fs versions by equality,"
      echo "                      so pointing at the fs the fleet already runs"
      echo "                      means firmware only, fs untouched, while a valid"
      echo "                      fs manifest stays in place. Mutually exclusive"
      echo "                      with --no-fs."
      echo "  --repoint <fw>      Point the channel at the already published"
      echo "                      artifact <fw>. No build, no copy, no version"
      echo "                      prompts: the rollback / promote path. Only"
      echo "                      reads the OTA tree, never products/<env>, so"
      echo "                      it works for every dir under /srv/ota, nextgen"
      echo "                      and legacy alike. Composes with --no-fs and"
      echo "                      --fs-from."
      echo "  --dry-run           With --repoint: print the channel json that"
      echo "                      would be written and touch nothing. Needs no"
      echo "                      sudo. Not supported for a normal publish."
      exit 0
      ;;
    *)
      echo "❌ Unknown argument: $1"
      exit 1
      ;;
  esac
done

if [[ "$NO_FS" == true && -n "$FS_FROM_VERSION" ]]; then
  echo "❌ --no-fs and --fs-from are mutually exclusive"
  exit 1
fi
if [[ "$FS_ONLY" == true ]] && [[ "$NO_FS" == true || -n "$FS_FROM_VERSION" ]]; then
  echo "❌ --fs-only cannot combine with --no-fs or --fs-from"
  exit 1
fi
if [[ -n "$REPOINT_VERSION" ]] && [[ "$FS_ONLY" == true || -n "$FW_VERSION" || -n "$FS_VERSION" || "$FORCE_FS_VERSION" == true ]]; then
  echo "❌ --repoint takes the target version as its argument and only moves"
  echo "   the channel pointer; it cannot combine with --fs-only, --fw-version,"
  echo "   --fs-version or --force-fs-version"
  exit 1
fi
if [[ "$DRY_RUN" == true && -z "$REPOINT_VERSION" ]]; then
  echo "❌ --dry-run is only supported together with --repoint"
  exit 1
fi

# ---------------------------------------------------------------------------
# Repoint mode: move a channel onto an already-published artifact. No build,
# no copy, no version prompts. The rollback / promote path: point a channel
# back at a known-good release, or promote a tested artifact to stable.
# Ported from the legacy-main publisher. Unlike the publish flow below it
# never reads products/<env> or .pio/build, only the OTA tree, so it works
# for every product dir under /srv/ota, nextgen and legacy alike.
# ---------------------------------------------------------------------------
if [[ -n "$REPOINT_VERSION" ]]; then
  if [[ -z "$PRODUCT" || -z "$CHANNEL" ]]; then
    echo "❌ --repoint needs -p <product> (the /srv/ota dir name) and -c <channel>"
    exit 1
  fi
  if [[ "$CHANNEL" != "develop" && "$CHANNEL" != "early" && "$CHANNEL" != "stable" ]]; then
    echo "❌ Invalid channel: $CHANNEL (stable|early|develop)"
    exit 1
  fi
  PRODUCT_DIR="$OTA_ROOT/$PRODUCT"
  if [[ ! -d "$PRODUCT_DIR" ]]; then
    echo "❌ No such OTA product dir: $PRODUCT_DIR"
    echo "   The product id must match the /srv/ota dir name exactly."
    exit 1
  fi

  ART_DIR="$PRODUCT_DIR/artifacts/$REPOINT_VERSION"
  MANIFEST_URL="$OTA_BASE_URL/$PRODUCT/artifacts/$REPOINT_VERSION/manifest.json"
  FS_MANIFEST_URL="$OTA_BASE_URL/$PRODUCT/artifacts/$REPOINT_VERSION/fs.json"

  if [[ "$NO_FS" == true ]]; then
    # Empty on purpose: ota_updater treats an empty fs_manifest_url as "no
    # filesystem half", so devices update firmware only.
    FS_MANIFEST_URL=""
  elif [[ -n "$FS_FROM_VERSION" ]]; then
    FS_MANIFEST_URL="$OTA_BASE_URL/$PRODUCT/artifacts/$FS_FROM_VERSION/fs.json"
    FS_FROM_MANIFEST="$PRODUCT_DIR/artifacts/$FS_FROM_VERSION/fs.json"
    if [[ ! -f "$FS_FROM_MANIFEST" ]]; then
      if [[ "$DRY_RUN" == true ]]; then
        echo "⚠️  fs manifest not found: $FS_FROM_MANIFEST (would fail on a real run)"
      else
        echo "❌ fs manifest not found: $FS_FROM_MANIFEST"
        echo "   Nothing published under that version. Check the name."
        exit 1
      fi
    fi
  elif [[ ! -f "$ART_DIR/fs.json" ]]; then
    echo "⚠️  $ART_DIR/fs.json does not exist (artifact published with --no-fs?)."
    echo "   Devices will fail the fs manifest fetch and skip the filesystem half."
    echo "   Consider --no-fs or --fs-from <fw> to be explicit about it."
  fi

  if [[ ! -f "$ART_DIR/manifest.json" ]]; then
    if [[ "$DRY_RUN" == true ]]; then
      echo "⚠️  artifact not found: $ART_DIR/manifest.json (would fail on a real run)"
    else
      echo "❌ artifact not found: $ART_DIR/manifest.json"
      echo "   Nothing published under that version. Publish it first, or check the name."
      exit 1
    fi
  fi

  LEGACY_PRODUCT="${LEGACY_PRODUCT_ALIAS[$PRODUCT]:-}"

  echo "=== OTA repoint ==="
  echo "  Product : $PRODUCT"
  echo "  Channel : $CHANNEL   ->   $REPOINT_VERSION"
  echo "  Manifest: $MANIFEST_URL"
  if [[ "$NO_FS" == true ]]; then
    echo "  FS half : SKIPPED (--no-fs, channel carries no fs_manifest_url)"
  elif [[ -n "$FS_FROM_VERSION" ]]; then
    echo "  FS half : pinned to $FS_FROM_VERSION"
  else
    echo "  FS half : $FS_MANIFEST_URL"
  fi
  if [[ -n "$LEGACY_PRODUCT" ]]; then
    echo "  Mirror  : $LEGACY_PRODUCT/channels/$CHANNEL.json (renamed-product alias)"
  fi
  echo "  Dry run : $DRY_RUN"
  echo

  if [[ "$DRY_RUN" != true && "$ASSUME_YES" != true ]]; then
    read -rp "Repoint $CHANNEL to $REPOINT_VERSION? [y/N]: " CONFIRM
    [[ "$CONFIRM" == "y" || "$CONFIRM" == "Y" ]] || exit 0
  fi

  # Real writes to /srv/ota need root; dry-run never writes.
  SUDO="sudo"
  if [[ "$(id -u)" -eq 0 ]]; then SUDO=""; fi

  write_repoint_channel() {
    # write_repoint_channel <product>: write that product's <channel>.json.
    # The manifest URLs stay absolute (they point at $PRODUCT's artifacts),
    # which is exactly what the publish-mode mirror ships too.
    local product="$1"
    local dir="$OTA_ROOT/$product/channels"
    local body
    body=$(cat <<EOF
{
  "schema": 1,
  "product": "$product",
  "channel": "$CHANNEL",
  "target": {
    "version": "$REPOINT_VERSION",
    "manifest_url": "$MANIFEST_URL",
    "fs_manifest_url": "$FS_MANIFEST_URL"
  }
}
EOF
)
    if [[ "$DRY_RUN" == true ]]; then
      echo "---- would write $dir/$CHANNEL.json ----"
      echo "$body"
      echo
      return
    fi
    $SUDO mkdir -p "$dir"
    printf '%s\n' "$body" | $SUDO tee "$dir/$CHANNEL.json" > /dev/null
    $SUDO chown root:www-data "$dir/$CHANNEL.json"
    $SUDO chmod 644 "$dir/$CHANNEL.json"
  }

  write_repoint_channel "$PRODUCT"
  if [[ -n "$LEGACY_PRODUCT" ]]; then
    echo "→ Mirroring channel to legacy product path: $LEGACY_PRODUCT/$CHANNEL.json"
    write_repoint_channel "$LEGACY_PRODUCT"
  fi

  echo
  if [[ "$DRY_RUN" == true ]]; then
    echo "Dry run: nothing written."
  else
    echo "✅ Repoint complete"
  fi
  echo "Verify: curl -s $OTA_BASE_URL/$PRODUCT/channels/$CHANNEL.json"
  exit 0
fi

echo "=== OTA Publish Script ==="
echo

if [[ -z "$PRODUCT" ]]; then
  echo "Select product:"
  echo "  1) nextgen-30x30"
  echo "  2) nextgen-50x50"
  echo "  3) nextgen-logo-55x50"
  echo "  4) nextgen-logo-105x105"
  echo "  5) nextgen-mini"
  read -rp "Product number (1-5): " PRODUCT_SELECTION
  case "$PRODUCT_SELECTION" in
    1) PRODUCT="nextgen-30x30" ;;
    2) PRODUCT="nextgen-50x50" ;;
    3) PRODUCT="nextgen-logo-55x50" ;;
    4) PRODUCT="nextgen-logo-105x105" ;;
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
if [[ -z "$FW_VERSION" ]] && [[ "$NO_FS" == true || -n "$FS_FROM_VERSION" ]]; then
  echo "❌ --no-fs and --fs-from publish firmware, so a firmware version is required"
  exit 1
fi

# No fs image goes out under --no-fs / --fs-from, so no UI version is needed.
if [[ "$NO_FS" != true && -z "$FS_FROM_VERSION" ]]; then
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

# Under --no-fs / --fs-from no fs image is read or published, so nothing here
# applies. The --fs-from target must already exist, or every device on the
# channel would start failing its fs check.
if [[ -n "$FS_FROM_VERSION" ]]; then
  FS_FROM_MANIFEST="$OTA_ROOT/$PRODUCT/artifacts/$FS_FROM_VERSION/fs.json"
  if [[ ! -f "$FS_FROM_MANIFEST" ]]; then
    echo "❌ fs manifest not found: $FS_FROM_MANIFEST"
    echo "   Nothing published under that version. Check the name."
    exit 1
  fi
fi
if [[ "$NO_FS" != true && -z "$FS_FROM_VERSION" ]]; then
FS_TYPE="littlefs"
if [[ -f "$BUILD_DIR/littlefs.bin" ]]; then
  FS_SRC="$BUILD_DIR/littlefs.bin"
else
  echo "❌ No LittleFS image found"
  exit 1
fi
FS_SIZE=$(stat -c%s "$FS_SRC")
FS_HASH=$(sha256sum "$FS_SRC" | awk '{print $1}')
# Recorded in fs.json so the next release can compare against it without
# unpacking this image again. Empty when mklittlefs is not installed, which
# every reader treats as "unknown", never as "unchanged".
FS_CONTENT_HASH="$(fs_content_hash "$FS_SRC")"

# Keep the published FS version when the image did not actually change.
#
# A filesystem update rewrites the whole 3 MB LittleFS partition, which takes
# every log file with it. The device already guards against that: it skips the
# write when the manifest's fs version equals what /.fs_image_version records
# (src/ota_updater.cpp). That guard has never fired in practice, because
# release.sh bumps UI_VERSION in lockstep with FIRMWARE_VERSION — so a release
# touching only src/ still hands the fleet a "new" filesystem and wipes its
# logs for a byte-identical image.
#
# Rather than ask everyone to remember not to bump it, make the pipeline
# decide from the image: if what it contains is the same as what this channel
# currently points at, publish it under the old version. Devices already on it
# skip the write, devices on an older one still download (from the new URL,
# same contents) and land on the same version string.
#
# The comparison is on CONTENTS, not on bytes. This guard originally compared
# sha256 of the image and could therefore never fire: mklittlefs writes the
# build time into the filesystem metadata, so two packs of an unchanged data/
# are always different images. The 26.08.21-rc.3 release proved it, wiping the
# fleet's logs for a data/ that had not been touched. See fs_content_hash().
#
# The byte hash is still tried first, because it is free and a match is
# conclusive. Only when it differs does the expensive unpack happen.
#
# Per channel on purpose: stable and develop carry different images, and a
# device on stable must be compared against what stable last shipped.
#
# Manifests published before this change carry no content_sha256, so the
# published image is unpacked to derive one. That keeps the guard working on
# the first release after this lands instead of one release later.
#
# If mklittlefs is not installed both content hashes come back empty and the
# script behaves exactly as it did before: a new version every time.
if [[ "$FORCE_FS_VERSION" != true ]]; then
  PUBLISHED_FS_MANIFEST=""
  PUBLISHED_FS_URL=""
  if [[ -f "$CHANNEL_DIR/$CHANNEL.json" ]]; then
    PUBLISHED_FS_URL="$(read_json_field "$CHANNEL_DIR/$CHANNEL.json" target fs_manifest_url)"
  fi
  if [[ -n "$PUBLISHED_FS_URL" && "$PUBLISHED_FS_URL" == "$OTA_BASE_URL"/* ]]; then
    PUBLISHED_FS_MANIFEST="$OTA_ROOT/${PUBLISHED_FS_URL#"$OTA_BASE_URL"/}"
  fi
  if [[ -z "$PUBLISHED_FS_MANIFEST" ]]; then
    PUBLISHED_FS_MANIFEST="$OTA_ROOT/$PRODUCT/artifacts/current/fs.json"
  fi

  if [[ -f "$PUBLISHED_FS_MANIFEST" ]]; then
    PUBLISHED_FS_HASH="$(read_json_field "$PUBLISHED_FS_MANIFEST" sha256)"
    PUBLISHED_FS_VERSION="$(read_json_field "$PUBLISHED_FS_MANIFEST" version)"

    FS_SAME=false
    if [[ -n "$PUBLISHED_FS_HASH" && "$PUBLISHED_FS_HASH" == "$FS_HASH" ]]; then
      FS_SAME=true
      FS_SAME_HOW="byte-identical"
    else
      PUBLISHED_FS_CONTENT_HASH="$(read_json_field "$PUBLISHED_FS_MANIFEST" content_sha256)"
      if [[ -z "$PUBLISHED_FS_CONTENT_HASH" ]]; then
        # Pre-content_sha256 manifest. fs.bin sits next to it.
        PUBLISHED_FS_CONTENT_HASH="$(fs_content_hash "$(dirname "$PUBLISHED_FS_MANIFEST")/fs.bin")"
      fi
      if [[ -n "$FS_CONTENT_HASH" && "$FS_CONTENT_HASH" == "$PUBLISHED_FS_CONTENT_HASH" ]]; then
        FS_SAME=true
        FS_SAME_HOW="different bytes, identical contents"
      fi
    fi

    if [[ "$FS_SAME" == true && -n "$PUBLISHED_FS_VERSION" ]]; then
      if [[ "$PUBLISHED_FS_VERSION" != "$FS_VERSION" ]]; then
        echo "→ Filesystem contents are unchanged from $CHANNEL ($FS_SAME_HOW)"
        echo "  Keeping FS version $PUBLISHED_FS_VERSION (not publishing $FS_VERSION)"
        echo "  Devices will skip the filesystem write and keep their logs."
        echo "  Override with --force-fs-version."
        FS_VERSION="$PUBLISHED_FS_VERSION"
      fi
      FS_UNCHANGED=true
    fi
  fi
fi
fi

echo
echo "Publishing to:"
echo "  Product : $PRODUCT"
echo "  Channel : $CHANNEL"
echo "  FW ver  : ${FW_VERSION:-<unchanged>}"
if [[ "$NO_FS" == true ]]; then
  echo "  FS ver  : SKIPPED (--no-fs, channel carries no fs_manifest_url)"
elif [[ -n "$FS_FROM_VERSION" ]]; then
  echo "  FS ver  : pinned to $FS_FROM_VERSION (no fs image published)"
else
  echo "  FS ver  : $FS_VERSION${FS_UNCHANGED:+ (unchanged image)}"
fi
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
# Filesystem (skipped under --no-fs / --fs-from)
# -------------------------
if [[ "$NO_FS" != true && -z "$FS_FROM_VERSION" ]]; then
echo "→ Copying filesystem image"

# FS_SRC / FS_SIZE / FS_HASH were resolved before the confirmation prompt, so
# the operator got to see the version that is really going out.
sudo cp "$FS_SRC" "$ARTIFACT_DIR/fs.bin"
sudo chown root:www-data "$ARTIFACT_DIR/fs.bin"
sudo chmod 644 "$ARTIFACT_DIR/fs.bin"

sudo tee "$ARTIFACT_DIR/fs.json" > /dev/null <<EOF
{
  "schema": 1,
  "product": "$PRODUCT",
  "type": "filesystem",
  "fs": "$FS_TYPE",
  "version": "$FS_VERSION",
  "filesize": $FS_SIZE,
  "sha256": "$FS_HASH",
  "content_sha256": "$FS_CONTENT_HASH",
  "url": "$OTA_BASE_URL/$PRODUCT/artifacts/${FW_VERSION:-current}/fs.bin"
}
EOF
fi

# -------------------------
# Channel update
# -------------------------
echo "→ Updating channel: $CHANNEL"

TARGET_JSON="null"

if [[ -n "$FW_VERSION" ]]; then
  FS_MANIFEST_URL="$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/fs.json"
  if [[ "$NO_FS" == true ]]; then
    # Empty on purpose: ota_updater treats an empty fs_manifest_url as "no
    # filesystem half", so devices update firmware only.
    FS_MANIFEST_URL=""
  elif [[ -n "$FS_FROM_VERSION" ]]; then
    FS_MANIFEST_URL="$OTA_BASE_URL/$PRODUCT/artifacts/$FS_FROM_VERSION/fs.json"
  fi
  TARGET_JSON=$(cat <<EOF
{
  "version": "$FW_VERSION",
  "manifest_url": "$OTA_BASE_URL/$PRODUCT/artifacts/$FW_VERSION/manifest.json",
  "fs_manifest_url": "$FS_MANIFEST_URL"
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

# -------------------------
# Legacy channel mirror (OTA continuity for renamed products)
# -------------------------
# nextgen-logo-105x105 was previously shipped as nextgen-logo-100x100. Units
# flashed under the old PRODUCT_ID keep polling the old channel path. Mirror the
# channel JSON there so they still receive updates. The target's manifest URLs
# are absolute (they point at the 105x105 artifacts), the firmware is identical
# hardware, and the device never validates the manifest 'product' field against
# its own PRODUCT_ID — so the unit installs this build and, on reboot, runs as
# 105x105 and polls the new channel directly from then on.
LEGACY_PRODUCT="${LEGACY_PRODUCT_ALIAS[$PRODUCT]:-}"
if [[ -n "$LEGACY_PRODUCT" ]]; then
  LEGACY_CHANNEL_DIR="$OTA_ROOT/$LEGACY_PRODUCT/channels"
  echo "→ Mirroring channel to legacy product path: $LEGACY_PRODUCT/$CHANNEL.json"
  sudo mkdir -p "$LEGACY_CHANNEL_DIR"
  sudo tee "$LEGACY_CHANNEL_DIR/$CHANNEL.json" > /dev/null <<EOF
{
  "schema": 1,
  "product": "$LEGACY_PRODUCT",
  "channel": "$CHANNEL",
  "target": $TARGET_JSON
}
EOF
  sudo chown root:www-data "$LEGACY_CHANNEL_DIR/$CHANNEL.json"
  sudo chmod 644 "$LEGACY_CHANNEL_DIR/$CHANNEL.json"
fi

echo
echo "✅ OTA publish complete"
