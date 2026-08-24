#!/bin/bash

# Legacy Wordclock Build Script
#
# Builds firmware (and optionally the littlefs filesystem image) for the frozen
# legacy ESP32 product line. Each product is a PlatformIO env declared in
# products/<name>/platformio.env.ini and pulled in via platformio.ini's
# extra_configs; the env name equals the product directory name, and
# tools/set_grid_filter.py narrows the compiled grid variants from that
# product's product.json.
#
# Scope: this is a *builder*. It compiles, names, and collects binaries into
# dist/. It deliberately does NOT tag, push, cut a GitHub release, or publish
# OTA manifests. Those stages on the nextgen line lean on infrastructure that
# does not exist on the frozen legacy line (tools/publish-ota.sh, per-channel
# OTA2 servers) and are Ron's call to wire up if the legacy line ever ships new
# builds again. Version numbers live in each product's product_config.h and are
# left untouched here.
#
# Usage:
#   tools/release.sh --list
#   tools/release.sh --product wordclock-legacy-nl-v4
#   tools/release.sh --prefix wordclock-legacy
#   tools/release.sh --all
#   tools/release.sh --all --channel early --fs
#
# See --help for all flags.

# Require bash (arrays, [[ ]]) even if invoked via sh
if [ -z "${BASH_VERSINFO}" ]; then
    exec bash "$0" "$@"
fi

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRODUCTS_DIR="$PROJECT_ROOT/products"
DIST_DIR="$PROJECT_ROOT/dist"

# Products excluded from --all. wordclock-nextgen is the ESP32-S3 migration
# seed that still lives in the legacy tree; it is not a legacy SKU and does not
# belong in a legacy mass build. It can still be built explicitly with
# --product wordclock-nextgen if ever needed.
EXCLUDE_FROM_ALL=("wordclock-nextgen")

# Defaults (overridable via flags)
CHANNEL="stable"
BUILD_FS=false
FS_ONLY=false
DO_CLEAN=true
SELECTED_PRODUCTS=()

print_header()  { echo -e "${BLUE}========================================${NC}"; echo -e "${BLUE}$1${NC}"; echo -e "${BLUE}========================================${NC}"; }
print_success() { echo -e "${GREEN}\xE2\x9C\x93 $1${NC}"; }
print_error()   { echo -e "${RED}\xE2\x9C\x97 $1${NC}"; }
print_warning() { echo -e "${YELLOW}\xE2\x9A\xA0 $1${NC}"; }
print_info()    { echo -e "${BLUE}\xE2\x84\xB9 $1${NC}"; }

usage() {
    cat <<'EOF'
Legacy Wordclock Build Script

Selects one or more legacy products and builds firmware (and optionally the
littlefs filesystem image) into dist/.

Selection (choose one):
  -p, --product <name>    Build a single product (e.g. wordclock-legacy-nl-v4)
      --prefix <prefix>   Build every product whose name is <prefix> or starts
                          with "<prefix>-" (e.g. wordclock-legacy, wordclock-logo)
  -a, --all               Build every legacy product (excludes wordclock-nextgen)
  -l, --list              List buildable products with version and grids, then exit

Build options:
  -c, --channel <name>    Release channel: stable | early | develop (default: stable).
                          Passed as RELEASE_CHANNEL; on stable/early the console.*
                          logs are stripped from the UI, on develop they are kept.
      --fs                Also build the littlefs filesystem image and copy to dist
      --fs-only           Build only the filesystem image (skip firmware)
      --no-clean          Skip `pio run --target clean` (faster incremental builds)
  -o, --output <dir>      Output directory (default: dist/)
  -h, --help              Show this help

Notes:
  * Version numbers are read from each product's product_config.h and are not
    modified by this script.
  * Tagging, GitHub releases, and OTA publishing are intentionally out of scope
    for the frozen legacy line.
EOF
}

# All buildable legacy products = every products/*/ dir that has a product_config.h.
discover_products() {
    local d name
    for d in "$PRODUCTS_DIR"/*/; do
        [[ -f "${d}product_config.h" ]] || continue
        name="$(basename "$d")"
        echo "$name"
    done
}

is_excluded_from_all() {
    local candidate="$1" x
    for x in "${EXCLUDE_FROM_ALL[@]}"; do
        [[ "$candidate" == "$x" ]] && return 0
    done
    return 1
}

products_by_prefix() {
    local prefix="$1" p
    while IFS= read -r p; do
        if [[ "$p" == "$prefix" || "$p" == "$prefix"-* ]]; then
            echo "$p"
        fi
    done < <(discover_products)
}

# Read FIRMWARE_VERSION out of a product's product_config.h
product_version() {
    local product="$1"
    grep -E '^#define FIRMWARE_VERSION' "$PRODUCTS_DIR/$product/product_config.h" 2>/dev/null \
        | sed 's/.*"\(.*\)".*/\1/'
}

# Read the grids array out of a product's product.json (for --list display)
product_grids() {
    local product="$1"
    local json="$PRODUCTS_DIR/$product/product.json"
    [[ -f "$json" ]] || { echo "(no product.json)"; return; }
    tr -d '\n ' < "$json" | sed -n 's/.*"grids":\[\(.*\)\].*/\1/p' | tr -d '"'
}

# Strip the per-product prefix (product name minus "wordclock-") off a version.
# e.g. product=wordclock-legacy-nl-v4 version=legacy-nl-v4-26.4.0 -> 26.4.0
version_suffix() {
    local version="$1" product="$2"
    local subtype="${product#wordclock-}"
    if [[ -n "$subtype" && "$version" == "$subtype"-* ]]; then
        echo "${version#${subtype}-}"
    else
        echo "$version"
    fi
}

list_products() {
    print_header "Buildable legacy products"
    local p ver grids tag
    while IFS= read -r p; do
        ver="$(product_version "$p")"
        grids="$(product_grids "$p")"
        tag=""
        is_excluded_from_all "$p" && tag="  [excluded from --all]"
        printf '  %-30s %-28s grids: %s%s\n' "$p" "${ver:-<none>}" "${grids:-<none>}" "$tag"
    done < <(discover_products)
}

# Build one product. Honors BUILD_FS / FS_ONLY / DO_CLEAN / CHANNEL.
# Appends produced files to the global RESULTS array.
RESULTS=()
build_one() {
    local product="$1"
    local env_name="$product"
    local cfg="$PRODUCTS_DIR/$product/product_config.h"

    if [[ ! -f "$cfg" ]]; then
        print_error "Unknown product '$product' (no products/$product/product_config.h)"
        return 1
    fi

    local version suffix
    version="$(product_version "$product")"
    if [[ -z "$version" ]]; then
        print_error "Could not read FIRMWARE_VERSION from $cfg"
        return 1
    fi
    suffix="$(version_suffix "$version" "$product")"

    print_header "Build $product ($version, channel=$CHANNEL)"

    cd "$PROJECT_ROOT"

    if [[ "$DO_CLEAN" == true ]]; then
        print_info "Cleaning $env_name ..."
        pio run --target clean --environment "$env_name"
    fi

    # Filesystem image (littlefs). Built when requested; also the stage that
    # runs remove_console_logs.py, so RELEASE_CHANNEL matters here.
    if [[ "$BUILD_FS" == true || "$FS_ONLY" == true ]]; then
        print_info "Building filesystem image for $product ..."
        if ! RELEASE_CHANNEL="$CHANNEL" pio run --target buildfs --environment "$env_name"; then
            print_error "Filesystem build failed for $product"
            return 1
        fi
        local fs_src="$PROJECT_ROOT/.pio/build/$env_name/littlefs.bin"
        if [[ -f "$fs_src" ]]; then
            local fs_dst="$DIST_DIR/${product}-${suffix}-littlefs.bin"
            cp "$fs_src" "$fs_dst"
            RESULTS+=("$fs_dst")
            print_success "Filesystem: $fs_dst ($(du -h "$fs_dst" | cut -f1))"
        else
            print_warning "Expected filesystem image not found at $fs_src"
        fi
    fi

    if [[ "$FS_ONLY" == true ]]; then
        return 0
    fi

    print_info "Building firmware for $product ..."
    if ! RELEASE_CHANNEL="$CHANNEL" pio run --environment "$env_name"; then
        print_error "Firmware build failed for $product"
        return 1
    fi
    local fw_src="$PROJECT_ROOT/.pio/build/$env_name/firmware.bin"
    if [[ ! -f "$fw_src" ]]; then
        print_error "Expected firmware not found at $fw_src"
        return 1
    fi
    local fw_dst="$DIST_DIR/${product}-${suffix}.bin"
    cp "$fw_src" "$fw_dst"
    RESULTS+=("$fw_dst")
    print_success "Firmware: $fw_dst ($(du -h "$fw_dst" | cut -f1))"
}

# ---- argument parsing -------------------------------------------------------

MODE=""   # product | prefix | all | list
ARG=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--product) MODE="product"; ARG="${2:-}"; shift 2 ;;
        --prefix)     MODE="prefix";  ARG="${2:-}"; shift 2 ;;
        -a|--all)     MODE="all";     shift ;;
        -l|--list)    MODE="list";    shift ;;
        -c|--channel) CHANNEL="${2:-}"; shift 2 ;;
        --fs)         BUILD_FS=true;  shift ;;
        --fs-only)    FS_ONLY=true;   shift ;;
        --no-clean)   DO_CLEAN=false; shift ;;
        -o|--output)  DIST_DIR="${2:-}"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) print_error "Unknown argument: $1"; echo; usage; exit 1 ;;
    esac
done

if [[ -z "$MODE" ]]; then
    print_error "Nothing to do: pass one of --product, --prefix, --all, or --list"
    echo
    usage
    exit 1
fi

case "$CHANNEL" in
    stable|early|develop) ;;
    *) print_error "Invalid --channel '$CHANNEL' (expected stable|early|develop)"; exit 1 ;;
esac

if [[ "$MODE" == "list" ]]; then
    list_products
    exit 0
fi

if ! command -v pio &> /dev/null; then
    print_error "PlatformIO CLI (pio) is not installed"
    print_info "Install with: pip install platformio"
    exit 1
fi

# Resolve the product set for this run.
case "$MODE" in
    product)
        [[ -n "$ARG" ]] || { print_error "--product needs a name"; exit 1; }
        SELECTED_PRODUCTS=("$ARG")
        ;;
    prefix)
        [[ -n "$ARG" ]] || { print_error "--prefix needs a value"; exit 1; }
        while IFS= read -r p; do SELECTED_PRODUCTS+=("$p"); done < <(products_by_prefix "$ARG")
        if [[ ${#SELECTED_PRODUCTS[@]} -eq 0 ]]; then
            print_error "No products match prefix '$ARG'"
            exit 1
        fi
        ;;
    all)
        while IFS= read -r p; do
            is_excluded_from_all "$p" && continue
            SELECTED_PRODUCTS+=("$p")
        done < <(discover_products)
        if [[ ${#SELECTED_PRODUCTS[@]} -eq 0 ]]; then
            print_error "No buildable products found under $PRODUCTS_DIR"
            exit 1
        fi
        ;;
esac

mkdir -p "$DIST_DIR"

print_info "Products to build (${#SELECTED_PRODUCTS[@]}): ${SELECTED_PRODUCTS[*]}"
print_info "Channel: $CHANNEL | firmware: $([[ "$FS_ONLY" == true ]] && echo no || echo yes) | filesystem: $([[ "$BUILD_FS" == true || "$FS_ONLY" == true ]] && echo yes || echo no)"
echo

FAILED=()
for product in "${SELECTED_PRODUCTS[@]}"; do
    if ! build_one "$product"; then
        FAILED+=("$product")
    fi
    echo
done

print_header "Summary"
if [[ ${#RESULTS[@]} -gt 0 ]]; then
    print_success "Produced ${#RESULTS[@]} file(s) in $DIST_DIR:"
    for f in "${RESULTS[@]}"; do
        echo "    $(basename "$f")"
    done
else
    print_warning "No output files were produced"
fi

if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo
    print_error "Failed products (${#FAILED[@]}): ${FAILED[*]}"
    exit 1
fi

echo
print_info "Build only. Tagging, GitHub releases, and OTA publishing are not"
print_info "performed for the legacy line; those remain a manual step."
