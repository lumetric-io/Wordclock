#!/bin/bash

# Legacy Wordclock Build Script
#
# Selects a version, builds firmware (and optionally the littlefs filesystem
# image), and hands off to tools/publish-ota.sh for the frozen legacy ESP32
# product line. Each product is a PlatformIO env declared in
# products/<name>/platformio.env.ini and pulled in via platformio.ini's
# extra_configs; the env name equals the product directory name, and
# tools/set_grid_filter.py narrows the compiled grid variants from that
# product's product.json.
#
# Scope: version + build + publish handoff. For a single product it can pick or
# accept a version, write it into that product's product_config.h (so the
# firmware actually reports it, which is what OTA compares), build the bits into
# dist/, and optionally invoke tools/publish-ota.sh to ship them. It deliberately
# does NOT tag, push, or cut a GitHub release: git on the frozen legacy line is
# Ron's to drive, and the real OTA publish writes to root-owned /srv/ota, so it
# needs sudo and is Ron's to run (this script only ever offers it).
#
# Multi-product builds (--all / --prefix) do not touch versions: they build every
# selected product at whatever its product_config.h already says.
#
# Usage:
#   tools/release.sh                        # no flags on a terminal: interactive picker
#   tools/release.sh --list
#   tools/release.sh --product wordclock-legacy-nl-v4
#   tools/release.sh --product wordclock-legacy-nl-v4 --channel develop --fw-version 26.4.1-dev.1 --fs
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

# Version selection state (single product only).
#   VERSION_ACTION: ""  -> leave product_config.h untouched (multi-product, or a
#                          plain build with no version chosen)
#                   set -> write NEW_VERSION into product_config.h before building
#                   keep-> reuse the current version as-is (no write)
NEW_VERSION=""
VERSION_ACTION=""
VERSION_FROM_FLAG=false
# VER_PRODUCT is the single product whose version we are selecting; the version
# helpers below read it for the per-product "wordclock-" prefix.
VER_PRODUCT=""
# Publish handoff (single product only).
DO_PUBLISH=false

print_header()  { echo -e "${BLUE}========================================${NC}"; echo -e "${BLUE}$1${NC}"; echo -e "${BLUE}========================================${NC}"; }
print_success() { echo -e "${GREEN}\xE2\x9C\x93 $1${NC}"; }
print_error()   { echo -e "${RED}\xE2\x9C\x97 $1${NC}"; }
print_warning() { echo -e "${YELLOW}\xE2\x9A\xA0 $1${NC}"; }
print_info()    { echo -e "${BLUE}\xE2\x84\xB9 $1${NC}"; }

usage() {
    cat <<'EOF'
Legacy Wordclock Build Script

Selects a version, builds firmware (and optionally the littlefs filesystem
image) into dist/, and can hand the result off to tools/publish-ota.sh.

Selection (choose one; with none, a terminal opens an interactive picker):
  -p, --product <name>    Build a single product (e.g. wordclock-legacy-nl-v4)
      --prefix <prefix>   Build every product whose name is <prefix> or starts
                          with "<prefix>-" (e.g. wordclock-legacy, wordclock-logo)
  -a, --all               Build every legacy product (excludes wordclock-nextgen)
  -l, --list              List buildable products with version and grids, then exit

Version (single product only; the interactive picker prompts for one):
      --fw-version <v>    Set the firmware version for this build and write it into
                          the product's product_config.h before building (also
                          updates UI_VERSION to ui-<v>). The 'legacy-...-' prefix is
                          added automatically. Alias: --version.
                          Multi-product builds ignore versions and build whatever
                          each product_config.h already says.

Build options:
  -c, --channel <name>    Release channel: stable | early | develop (default: stable).
                          Passed as RELEASE_CHANNEL; on stable/early the console.*
                          logs are stripped from the UI, on develop they are kept.
                          Also drives the proposed version: stable is a clean
                          release, early carries -rc.N, develop carries -dev.N.
      --fs                Also build the littlefs filesystem image and copy to dist
      --fs-only           Build only the filesystem image (skip firmware)
      --no-clean          Skip `pio run --target clean` (faster incremental builds)
  -o, --output <dir>      Output directory (default: dist/)

Publish (single product only):
      --publish           After a successful build, invoke tools/publish-ota.sh to
                          ship firmware+fs to OTA2 and repoint the channel. Implies
                          --fs (publish needs the littlefs image). The real publish
                          writes to root-owned /srv/ota, so it runs sudo: that step
                          is Ron's. Without this flag the exact publish command is
                          printed for you to run.
  -h, --help              Show this help

Notes:
  * Only single-product builds select/write a version; --all and --prefix leave
    every product_config.h untouched.
  * A version write leaves a product_config.h.bak and is left UNCOMMITTED: git on
    the frozen legacy line is Ron's to drive.
  * Tagging and GitHub releases are intentionally out of scope for the frozen
    legacy line.
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

# ---- version selection (ported/adapted from nextgen tools/release.sh) --------
#
# Prerelease tags follow the nextgen release.sh convention: develop -> -dev.N,
# early -> -rc.N, stable -> no tag. Unlike nextgen the legacy scheme is not
# date-based; versions are semver-ish 26.x.y. So the proposal is a straight
# increment of the product's current FIRMWARE_VERSION, plus the channel's
# prerelease tag when targeting develop/early. The user can always type their
# own version instead.
#
# (Note: the live develop channel for nl-v4 currently serves an -rc build,
# ...-rc.10, from before this tooling existed. That is a historical artifact of
# what was published by hand, not the convention; new develop builds propose
# -dev.N. isVersionNewer compares the numeric core first, so the flavor switch
# does not affect update detection.)
#
# All helpers below read VER_PRODUCT for the per-product prefix
# (product_prefix = VER_PRODUCT minus the leading "wordclock-").

# Strip ui-, build metadata (+...) and the product prefix, leaving the bare base.
ver_base() {
    local version="$1"
    local prefix="${VER_PRODUCT#wordclock-}"
    local base="${version%%+*}"
    base="${base#ui-}"
    [[ -n "$prefix" && "$base" == "$prefix"-* ]] && base="${base#${prefix}-}"
    echo "$base"
}

# True (0) if the version carries a prerelease tag (a '-' in the base).
ver_is_prerelease() {
    local base; base="$(ver_base "$1")"
    [[ "$base" == *"-"* ]]
}

# Keep ui-/prefix, drop everything from the first '-' of the base. Promotes a
# prerelease to its clean release (e.g. ...-26.4.0-rc.3 -> ...-26.4.0).
ver_strip_prerelease() {
    local version="$1"
    local prefix="${VER_PRODUCT#wordclock-}"
    local base="${version%%+*}"
    local ui_prefix="" apply=""
    [[ "$base" == ui-* ]] && { ui_prefix="ui-"; base="${base#ui-}"; }
    if [[ -n "$prefix" && "$base" == "$prefix"-* ]]; then
        base="${base#${prefix}-}"; apply="${prefix}-"
    elif [[ -n "$prefix" ]]; then
        apply="${prefix}-"
    fi
    base="${base%%-*}"
    echo "${ui_prefix}${apply}${base}"
}

# -foo.N -> -foo.(N+1) (rc, dev, ...); a bare -foo -> -foo.1; X.Y.Z -> X.Y.(Z+1).
ver_increment() {
    local version="$1"
    local prefix="${VER_PRODUCT#wordclock-}"
    local base="${version%%+*}"; base="${base#ui-}"
    local apply=""
    if [[ -n "$prefix" ]]; then
        [[ "$base" == "$prefix"-* ]] && base="${base#${prefix}-}"
        apply="${prefix}-"
    fi
    if [[ "$base" =~ -([a-zA-Z]+)\.([0-9]+)$ ]]; then
        local t="${BASH_REMATCH[1]}" n="${BASH_REMATCH[2]}"
        echo "${apply}${base%-${t}.${n}}-${t}.$((10#$n + 1))"
    elif [[ "$base" =~ -([a-zA-Z]+)$ ]]; then
        echo "${apply}${base}.1"
    elif [[ "$base" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        echo "${apply}${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$((10#${BASH_REMATCH[3]} + 1))"
    else
        echo "${apply}${base}"
    fi
}

# Add the channel's prerelease tag if the base has none. Matches nextgen
# release.sh: develop -> -dev.1, early -> -rc.1, stable -> no tag.
ver_add_channel_prerelease() {
    local version="$1"
    local prefix="${VER_PRODUCT#wordclock-}"
    local base="${version#ui-}"
    local apply=""
    if [[ -n "$prefix" ]]; then
        [[ "$base" == "$prefix"-* ]] && base="${base#${prefix}-}"
        apply="${prefix}-"
    fi
    local tag=""
    case "$CHANNEL" in
        develop) tag="-dev.1" ;;
        early)   tag="-rc.1" ;;
        *)       echo "$version"; return ;;   # stable: no prerelease tag
    esac
    # Only add a tag when the base does not already carry one.
    [[ "$base" == *"-"* ]] && { echo "$version"; return; }
    echo "${apply}${base}${tag}"
}

# Require the product prefix; the base must be semver or date-based.
ver_validate() {
    local version="$1"
    local prefix="${VER_PRODUCT#wordclock-}"
    local base="$version"
    if [[ -n "$prefix" ]]; then
        [[ "$base" == "$prefix"-* ]] || return 1
        base="${base#${prefix}-}"
    fi
    local semver='^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?(\+[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?$'
    local dated='^[0-9]{4}\.[0-9]{2}\.[0-9]{2}(-[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?(\+[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?$'
    [[ $base =~ $semver || $base =~ $dated ]]
}

# 0 if the version's prerelease-ness matches CHANNEL's contract: stable is a
# clean release, early/develop carry a prerelease. (Quiet; caller messages.)
ver_channel_matches() {
    local version="$1"
    if [[ "$CHANNEL" == "stable" ]]; then
        ver_is_prerelease "$version" && return 1 || return 0
    else
        ver_is_prerelease "$version" && return 0 || return 1
    fi
}

# Propose a version for VER_PRODUCT on $CHANNEL given its current one.
ver_propose() {
    local current="$1" proposed
    if [[ "$CHANNEL" == "stable" ]] && ver_is_prerelease "$current"; then
        proposed="$(ver_strip_prerelease "$current")"   # promote rc -> stable
    else
        proposed="$(ver_increment "$current")"
    fi
    ver_add_channel_prerelease "$proposed"
}

# Interactive version selection for a single product. Reads VER_PRODUCT +
# CHANNEL; sets NEW_VERSION and VERSION_ACTION (set|keep).
prompt_version() {
    local current proposed prefix reply
    current="$(product_version "$VER_PRODUCT")"
    prefix="${VER_PRODUCT#wordclock-}"

    print_header "Version for $VER_PRODUCT (channel=$CHANNEL)"
    print_info "Current version: ${current:-<none>}"
    if [[ -n "$current" ]]; then
        proposed="$(ver_propose "$current")"
        print_info "Proposed version: $proposed"
    else
        print_warning "No FIRMWARE_VERSION in product_config.h; you must enter one."
        proposed=""
    fi
    local example="26.4.1"
    case "$CHANNEL" in develop) example="26.4.1-dev.1" ;; early) example="26.4.1-rc.1" ;; esac
    echo
    print_info "Enter = use proposed | k = keep current | or type a version"
    print_info "  (the '${prefix}-' prefix is added automatically; e.g. $example)"
    read -rp "Version [${proposed:-type one}]: " reply || true

    if [[ -z "$reply" ]]; then
        [[ -n "$proposed" ]] || { print_error "No proposed version available; type one."; exit 1; }
        NEW_VERSION="$proposed"; VERSION_ACTION="set"
    elif [[ "$reply" == "k" || "$reply" == "K" ]]; then
        [[ -n "$current" ]] || { print_error "No current version to keep."; exit 1; }
        NEW_VERSION="$current"; VERSION_ACTION="keep"
    else
        # Tolerate a fully-qualified paste; strip the prefix so we don't double it.
        [[ -n "$prefix" && "$reply" == "$prefix"-* ]] && reply="${reply#${prefix}-}"
        NEW_VERSION="${prefix:+${prefix}-}$reply"
        VERSION_ACTION="set"
    fi

    if ! ver_validate "$NEW_VERSION"; then
        print_error "Invalid version: $NEW_VERSION (need ${prefix}-X.Y.Z[-dev.N|-rc.N])"
        exit 1
    fi
    enforce_channel_contract
    print_success "Version: $NEW_VERSION  ($VERSION_ACTION)"
    echo
}

# Apply the channel <-> prerelease contract to NEW_VERSION. A freshly set
# version is rejected on mismatch; a kept version is allowed with a warning
# (the user is deliberately reusing an existing published version).
enforce_channel_contract() {
    ver_channel_matches "$NEW_VERSION" && return 0
    if [[ "$VERSION_ACTION" == "keep" ]]; then
        print_warning "Reusing $NEW_VERSION on $CHANNEL (does not match the usual $CHANNEL tag convention)."
        return 0
    fi
    if [[ "$CHANNEL" == "stable" ]]; then
        print_error "stable releases cannot carry a prerelease tag: $NEW_VERSION"
    elif [[ "$CHANNEL" == "develop" ]]; then
        print_error "develop releases need a prerelease tag (e.g. -dev.1): $NEW_VERSION"
    else
        print_error "$CHANNEL releases need a prerelease tag (e.g. -rc.1): $NEW_VERSION"
    fi
    exit 1
}

# Write NEW_VERSION into the product's product_config.h: FIRMWARE_VERSION and,
# when the file already uses the ui- convention, UI_VERSION as ui-<version>.
# Leaves a .bak and prints the diff. Does NOT commit: git on the frozen legacy
# line is Ron's to drive.
write_version_to_config() {
    local product="$1" new_version="$2"
    local cfg="$PRODUCTS_DIR/$product/product_config.h"
    [[ -f "$cfg" ]] || { print_error "No product_config.h for $product"; return 1; }

    local ui_version="$new_version" current_ui
    current_ui="$(grep -E '^#define UI_VERSION' "$cfg" | sed 's/.*"\(.*\)".*/\1/')"
    [[ "$current_ui" == ui-* ]] && ui_version="ui-$new_version"

    cp "$cfg" "$cfg.bak"
    sed -i.tmp "s/^#define FIRMWARE_VERSION .*/#define FIRMWARE_VERSION \"$new_version\"/" "$cfg"
    sed -i.tmp "s/^#define UI_VERSION .*/#define UI_VERSION \"$ui_version\"/" "$cfg"
    rm -f "$cfg.tmp"

    print_success "Wrote FIRMWARE_VERSION=$new_version, UI_VERSION=$ui_version"
    print_info "  $cfg"
    print_info "  backup: $cfg.bak  (uncommitted; committing is Ron's call)"
    if command -v git >/dev/null 2>&1; then
        git -C "$PROJECT_ROOT" diff -- "$cfg" 2>/dev/null \
            | grep -E '^[+-]#define (FIRMWARE|UI)_VERSION' || true
    fi
    echo
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

# Interactive picker, shown only on a terminal when no selection flag was given.
# Builds a numbered menu from the discovered products (so nothing here has to be
# hand-maintained as products come and go), then prompts for channel and fs.
# Sets MODE / ARG / CHANNEL / BUILD_FS as if the equivalent flags were passed.
interactive_select() {
    local -a menu=()
    local p
    while IFS= read -r p; do menu+=("$p"); done < <(discover_products)
    if [[ ${#menu[@]} -eq 0 ]]; then
        print_error "No buildable products found under $PRODUCTS_DIR"
        exit 1
    fi

    print_header "Select a product to build"
    local i ver grids tag
    for i in "${!menu[@]}"; do
        ver="$(product_version "${menu[$i]}")"
        grids="$(product_grids "${menu[$i]}")"
        tag=""
        is_excluded_from_all "${menu[$i]}" && tag="  [not in 'all']"
        printf '  %2d) %-30s %-26s grids: %s%s\n' "$((i + 1))" "${menu[$i]}" "${ver:-<none>}" "${grids:-<none>}" "$tag"
    done
    echo "   a) all legacy products (excludes ${EXCLUDE_FROM_ALL[*]})"
    echo "   q) quit"
    echo

    local reply
    read -rp "Product [1-${#menu[@]}, a, q]: " reply || true
    case "$reply" in
        ""|q|Q) print_info "Nothing selected."; exit 0 ;;
        a|A)    MODE="all" ;;
        *)
            if [[ "$reply" =~ ^[0-9]+$ ]] && (( reply >= 1 && reply <= ${#menu[@]} )); then
                MODE="product"; ARG="${menu[$((reply - 1))]}"
            else
                print_error "Invalid selection: $reply"
                exit 1
            fi
            ;;
    esac

    local ch
    read -rp "Channel [stable/early/develop] (default $CHANNEL): " ch || true
    if [[ -n "$ch" ]]; then
        case "$ch" in
            stable|early|develop) CHANNEL="$ch" ;;
            *) print_error "Invalid channel '$ch' (expected stable|early|develop)"; exit 1 ;;
        esac
    fi

    # Version selection is per-product, so it only applies to a single product
    # (not the "all" mass build, whose products carry different versions).
    if [[ "$MODE" == "product" ]]; then
        VER_PRODUCT="$ARG"
        prompt_version
    fi

    if [[ "$BUILD_FS" != true && "$FS_ONLY" != true ]]; then
        local fs
        read -rp "Also build the littlefs filesystem image? [y/N]: " fs || true
        [[ "$fs" == "y" || "$fs" == "Y" ]] && BUILD_FS=true
    fi

    # Offer the OTA publish handoff when we actually built a chosen version for a
    # single product. The real publish writes to root-owned /srv/ota (sudo), so
    # this only sets intent; Ron runs the privileged step.
    if [[ "$MODE" == "product" && "$VERSION_ACTION" == "set" && "$FS_ONLY" != true ]]; then
        local pub
        read -rp "Publish to OTA (channel $CHANNEL) after a successful build? [y/N]: " pub || true
        if [[ "$pub" == "y" || "$pub" == "Y" ]]; then
            DO_PUBLISH=true
            if [[ "$BUILD_FS" != true ]]; then
                BUILD_FS=true
                print_info "Enabling the littlefs build: publish needs the filesystem image too."
            fi
        fi
    fi
    echo
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
        --fw-version|--version) NEW_VERSION="${2:-}"; VERSION_ACTION="set"; VERSION_FROM_FLAG=true; shift 2 ;;
        --publish)    DO_PUBLISH=true; shift ;;
        --fs)         BUILD_FS=true;  shift ;;
        --fs-only)    FS_ONLY=true;   shift ;;
        --no-clean)   DO_CLEAN=false; shift ;;
        -o|--output)  DIST_DIR="${2:-}"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) print_error "Unknown argument: $1"; echo; usage; exit 1 ;;
    esac
done

if [[ -z "$MODE" ]]; then
    if [[ -t 0 ]]; then
        interactive_select
    else
        print_error "Nothing to do: pass one of --product, --prefix, --all, or --list"
        echo
        usage
        exit 1
    fi
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

# ---- version + publish are single-product only ------------------------------
if [[ "$VERSION_ACTION" == "set" || "$DO_PUBLISH" == true ]] && [[ ${#SELECTED_PRODUCTS[@]} -ne 1 ]]; then
    print_error "Version selection and --publish apply to a single product only."
    print_info  "Use --product <name>; --all / --prefix build every product at its"
    print_info  "existing product_config.h version and do not publish."
    exit 1
fi

# A version passed by flag (rather than chosen interactively) is normalized,
# validated, and checked against the channel here.
if [[ "$VERSION_FROM_FLAG" == true ]]; then
    VER_PRODUCT="${SELECTED_PRODUCTS[0]}"
    prefix="${VER_PRODUCT#wordclock-}"
    [[ -n "$NEW_VERSION" ]] || { print_error "--fw-version needs a value"; exit 1; }
    if [[ -n "$prefix" && "$NEW_VERSION" != "$prefix"-* ]]; then
        NEW_VERSION="${prefix}-${NEW_VERSION}"
    fi
    if ! ver_validate "$NEW_VERSION"; then
        print_error "Invalid --fw-version: $NEW_VERSION (need ${prefix}-X.Y.Z[-dev.N|-rc.N])"
        exit 1
    fi
    enforce_channel_contract
    print_info "Version: $NEW_VERSION (set)"
fi

# Publish needs the firmware and the littlefs image; make sure both get built.
if [[ "$DO_PUBLISH" == true ]]; then
    if [[ "$FS_ONLY" == true ]]; then
        print_error "--publish needs a firmware build; drop --fs-only."
        exit 1
    fi
    if [[ "$BUILD_FS" != true ]]; then
        BUILD_FS=true
        print_info "Enabling the littlefs build: publish needs the filesystem image too."
    fi
fi

# Write the chosen version into product_config.h so the firmware reports it (the
# value OTA compares). Only when a version was actually set, never on keep.
if [[ "$VERSION_ACTION" == "set" ]]; then
    write_version_to_config "${SELECTED_PRODUCTS[0]}" "$NEW_VERSION"
fi

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

# ---- publish handoff (single product only) ----------------------------------
# Only a single-product build can publish: the OTA channel + artifact tree are
# per product. publish-ota.sh reads the version straight from product_config.h,
# so the handoff command needs only the product and channel.
if [[ ${#SELECTED_PRODUCTS[@]} -ne 1 ]]; then
    print_info "Built ${#SELECTED_PRODUCTS[@]} products. Version selection and OTA publishing"
    print_info "are single-product steps; run one product at a time to publish."
    print_info "Tagging and GitHub releases are not performed for the legacy line."
    exit 0
fi

product="${SELECTED_PRODUCTS[0]}"
fw_version="$(product_version "$product")"
fw_bin="$PROJECT_ROOT/.pio/build/$product/firmware.bin"
fs_bin="$PROJECT_ROOT/.pio/build/$product/littlefs.bin"

if [[ "$DO_PUBLISH" == true ]]; then
    if [[ ! -f "$fw_bin" || ! -f "$fs_bin" ]]; then
        print_error "Cannot publish: need both firmware.bin and littlefs.bin in"
        print_error "  $PROJECT_ROOT/.pio/build/$product/"
        print_info  "Rebuild with --fs so the filesystem image is produced too."
        exit 1
    fi
    print_header "Publish to OTA ($product -> $CHANNEL)"
    print_info "Handing off to tools/publish-ota.sh (it will confirm, and use sudo to"
    print_info "write root-owned /srv/ota). Version comes from product_config.h: $fw_version"
    echo
    exec "$PROJECT_ROOT/tools/publish-ota.sh" --product "$product" --channel "$CHANNEL"
fi

# No --publish: print the exact command so the privileged publish stays Ron's.
print_header "Next step: publish to OTA"
if [[ -f "$fw_bin" && -f "$fs_bin" ]]; then
    print_info "Firmware + filesystem are built. To ship $fw_version to the $CHANNEL channel:"
    echo
    echo "    tools/publish-ota.sh --product $product --channel $CHANNEL"
    echo
    print_info "Add --dry-run first to preview the exact JSON without touching /srv/ota."
    print_info "The real publish writes root-owned /srv/ota (sudo): that step is Ron's."
else
    print_info "Firmware built ($fw_version). A publish also needs the littlefs image:"
    print_info "rebuild with --fs, then run tools/publish-ota.sh --product $product --channel $CHANNEL"
fi
print_info "Tagging and GitHub releases are not performed for the legacy line."
