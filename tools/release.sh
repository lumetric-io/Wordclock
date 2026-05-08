#!/bin/bash

# Wordclock Release Pipeline Script
# Automates version bumping, building, tagging, and GitHub release creation
#
# Usage:
#   ./release.sh                    - Full release pipeline
#   ./release.sh --update-manifest  - (deprecated) Use publish-ota.sh instead

# Require bash (arrays, [[ ]], etc.) - re-exec with bash if run via sh
if [ -z "${BASH_VERSINFO}" ]; then
    exec bash "$0" "$@"
fi

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRODUCT="${PRODUCT:-}"
CHANNEL="${CHANNEL:-}"
NEW_VERSION="${NEW_VERSION:-}"
VERSION_SET=false
UI_ONLY=false
SKIP_GIT_RELEASE=false
TESTS_RUN=false
TESTS_SKIPPED=false
PIO_ENV=""
PRODUCT_CONFIG=""
VERSION_FILE=""
CONFIG_FILE="$PROJECT_ROOT/src/config.h"
BUILD_OUTPUT=""
DIST_DIR="$PROJECT_ROOT/dist"
MODE="full"
SUDO_MODE="${SUDO_MODE:-auto}" # auto|always|never

VALID_PRODUCTS=("nextgen-30x30" "nextgen-50x50" "nextgen-logo-55x50" "nextgen-logo-100x100" "nextgen-mini" "nextgen-bootstrap")

# When set, release builds all listed products in one go (same version base, one tag/release, multiple binaries)
BUILD_ALL_PRODUCTS=()
RELEASE_BINARIES=()

# Functions
get_release_filename() {
    local version="$1"
    local product="$2"
    local product_subtype="${product#wordclock-}"
    local version_suffix="$version"

    if [[ -n "$product_subtype" && "$version" == "$product_subtype"-* ]]; then
        version_suffix="${version#${product_subtype}-}"
    fi

    echo "${product}-${version_suffix}.bin"
}

# List all valid products whose name starts with the given prefix (e.g. wordclock-legacy)
get_products_by_prefix() {
    local prefix="$1"
    for p in "${VALID_PRODUCTS[@]}"; do
        if [[ "$p" == "$prefix" || "$p" == "$prefix"-* ]]; then
            echo "$p"
        fi
    done
}

# Return count of products matching prefix; used for menu labels
count_products_by_prefix() {
    get_products_by_prefix "$1" | wc -l
}

# For build-all mode: strip product prefix from NEW_VERSION to get base (e.g. legacy-27.0.0 -> 27.0.0)
get_base_version() {
    local version="$1"
    local product_prefix="$2"
    local base="${version%%+*}"
    base="${base#ui-}"
    if [[ -n "$product_prefix" ]]; then
        if [[ "$base" == "$product_prefix"-* ]]; then
            base="${base#${product_prefix}-}"
        elif [[ "$base" == *"-$product_prefix" ]]; then
            base="${base%-${product_prefix}}"
        fi
    fi
    echo "$base"
}

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

run_privileged() {
    local sudo_cmd="$1"
    shift
    if [[ -n "$sudo_cmd" ]]; then
        $sudo_cmd "$@"
    else
        "$@"
    fi
}

resolve_sudo_cmd() {
    local server_dir="$1"
    local server_manifest="$2"

    case "$SUDO_MODE" in
        never)
            echo ""
            return 0
            ;;
        always)
            if command -v sudo &> /dev/null; then
                if sudo -n true 2>/dev/null; then
                    echo "sudo -n"
                    return 0
                fi
                print_error "SUDO_MODE=always but sudo is not available without a password prompt"
                print_info "Run with SUDO_MODE=never and fix permissions, or grant NOPASSWD for sudo."
                return 1
            fi
            print_error "SUDO_MODE=always but sudo is not installed"
            return 1
            ;;
        auto|*)
            if [[ -w "$server_dir" ]] && { [[ -z "$server_manifest" ]] || [[ ! -e "$server_manifest" ]] || [[ -w "$server_manifest" ]]; }; then
                echo ""
                return 0
            fi
            if command -v sudo &> /dev/null; then
                if sudo -n true 2>/dev/null; then
                    echo "sudo -n"
                    return 0
                fi
                print_error "Server paths are not writable and sudo would prompt for a password"
                print_info "Fix permissions (e.g., add user to www-data and chmod g+w) or run once with sudo."
                return 1
            fi
            print_error "Server paths are not writable and sudo is not installed"
            return 1
            ;;
    esac
}

check_prerequisites() {
    print_header "Checking Prerequisites"
    
    # Check if git is available
    if ! command -v git &> /dev/null; then
        print_error "git is not installed"
        exit 1
    fi
    print_success "git found"
    
    # Check if pio is available
    if ! command -v pio &> /dev/null; then
        print_error "PlatformIO CLI is not installed"
        print_info "Install with: pip install platformio"
        exit 1
    fi
    print_success "PlatformIO found"
    
    # Check if gh is available
    if ! command -v gh &> /dev/null; then
        print_error "GitHub CLI is not installed"
        print_info "Install with: brew install gh (macOS) or sudo apt install gh (Linux)"
        exit 1
    fi
    print_success "GitHub CLI found"
    
    # Check if authenticated with GitHub
    if ! gh auth status &> /dev/null; then
        print_error "Not authenticated with GitHub CLI"
        print_info "Run: gh auth login"
        exit 1
    fi
    print_success "GitHub CLI authenticated"
    
    # Check if Python is available (for firmware.json update)
    if ! command -v python3 &> /dev/null; then
        print_warning "Python 3 is not installed (firmware.json update will be skipped)"
    else
        print_success "Python 3 found"
    fi
    
    # Check if we're in a git repository
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        print_error "Not in a git repository"
        exit 1
    fi
    print_success "Git repository detected"
    
    # Check for uncommitted changes
    if [[ -n $(git status -s) ]]; then
        print_warning "You have uncommitted changes"
        read -p "Continue anyway? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
    
    echo ""
}

get_current_version() {
    local source_file="$VERSION_FILE"
    if [[ "$UI_ONLY" == true ]]; then
        grep -E '^#define UI_VERSION' "$source_file" | sed 's/.*"\(.*\)".*/\1/'
    else
        grep -E '^#define FIRMWARE_VERSION' "$source_file" | sed 's/.*"\(.*\)".*/\1/'
    fi
}

increment_version() {
    local version=$1
    local branch=$2
    local product_prefix="${PRODUCT#wordclock-}"
    local prefix_to_apply=""
    
    # Remove build metadata if present (everything after +)
    local base_version="${version%%+*}"
    base_version="${base_version#ui-}"
    if [[ -n "$product_prefix" ]]; then
        if [[ "$base_version" == "$product_prefix"-* ]]; then
            base_version="${base_version#${product_prefix}-}"
            prefix_to_apply="${product_prefix}-"
        elif [[ "$base_version" == *"-$product_prefix" ]]; then
            base_version="${base_version%-${product_prefix}}"
            prefix_to_apply="${product_prefix}-"
        else
            prefix_to_apply="${product_prefix}-"
        fi
    fi
    
    # Check if version has pre-release tag (e.g., -dev.5, -rc.1)
    if [[ "$base_version" =~ -([a-zA-Z]+)\.([0-9]+)$ ]]; then
        # Has pre-release with number (e.g., -dev.5, -rc.1)
        local prerelease_type="${BASH_REMATCH[1]}"
        local prerelease_num="${BASH_REMATCH[2]}"
        local incremented_num=$((prerelease_num + 1))
        echo "${prefix_to_apply}${base_version%-${prerelease_type}.${prerelease_num}}-${prerelease_type}.${incremented_num}"
    elif [[ "$base_version" =~ -([a-zA-Z]+)$ ]]; then
        # Has pre-release without number (e.g., -dev) - add .1
        local prerelease_type="${BASH_REMATCH[1]}"
        echo "${prefix_to_apply}${base_version}.1"
    else
        # No pre-release tag - increment patch version (X.Y.Z -> X.Y.Z+1)
        if [[ "$base_version" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
            local major="${BASH_REMATCH[1]}"
            local minor="${BASH_REMATCH[2]}"
            local patch="${BASH_REMATCH[3]}"
            local incremented_patch=$((patch + 1))
            echo "${prefix_to_apply}${major}.${minor}.${incremented_patch}"
        else
            # Unknown format, return as-is
            if [[ -n "$prefix_to_apply" ]]; then
                echo "${prefix_to_apply}${base_version}"
            else
                echo "${base_version}"
            fi
        fi
    fi
}

is_prerelease_version() {
    local version="$1"
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
        return 0
    fi
    return 1
}

strip_prerelease_version() {
    local version="$1"
    local product_prefix="${PRODUCT#wordclock-}"
    local base_version="${version%%+*}"
    local ui_prefix=""
    local prefix_to_apply=""

    if [[ "$base_version" == ui-* ]]; then
        ui_prefix="ui-"
        base_version="${base_version#ui-}"
    fi

    if [[ -n "$product_prefix" ]]; then
        if [[ "$base_version" == "$product_prefix"-* ]]; then
            base_version="${base_version#${product_prefix}-}"
            prefix_to_apply="${product_prefix}-"
        elif [[ "$base_version" == *"-$product_prefix" ]]; then
            base_version="${base_version%-${product_prefix}}"
            prefix_to_apply="${product_prefix}-"
        else
            prefix_to_apply="${product_prefix}-"
        fi
    fi

    base_version="${base_version%%-*}"
    echo "${ui_prefix}${prefix_to_apply}${base_version}"
}

add_channel_prerelease_if_missing() {
    local version="$1"
    local product_prefix="${PRODUCT#wordclock-}"
    local base_version="${version#ui-}"
    local prefix_to_apply=""
    local prerelease=""

    if [[ -n "$product_prefix" ]]; then
        if [[ "$base_version" == "$product_prefix"-* ]]; then
            base_version="${base_version#${product_prefix}-}"
            prefix_to_apply="${product_prefix}-"
        elif [[ "$base_version" == *"-$product_prefix" ]]; then
            base_version="${base_version%-${product_prefix}}"
            prefix_to_apply="${product_prefix}-"
        else
            prefix_to_apply="${product_prefix}-"
        fi
    fi

    if [[ "$CHANNEL" == "develop" ]]; then
        prerelease="-dev.1"
    elif [[ "$CHANNEL" == "early" ]]; then
        prerelease="-rc.1"
    else
        echo "$version"
        return
    fi

    if [[ "$base_version" == *"-"* ]]; then
        echo "$version"
        return
    fi

    echo "${prefix_to_apply}${base_version}${prerelease}"
}

set_product_paths() {
    PIO_ENV="$PRODUCT"
    PRODUCT_CONFIG="$PROJECT_ROOT/products/$PRODUCT/product_config.h"
    if [[ -f "$PRODUCT_CONFIG" ]]; then
        VERSION_FILE="$PRODUCT_CONFIG"
    else
        VERSION_FILE="$CONFIG_FILE"
    fi
    BUILD_OUTPUT="$PROJECT_ROOT/.pio/build/$PIO_ENV/firmware.bin"
}

validate_version() {
    local version=$1
    local product_prefix="${PRODUCT#wordclock-}"
    local base_version="$version"

    if [[ -n "$product_prefix" ]]; then
        if [[ "$base_version" != "$product_prefix"-* ]]; then
            return 1
        fi
        base_version="${base_version#${product_prefix}-}"
    fi
    # Support full semantic versioning (semver 2.0.0)
    # Formats:
    #   - X.Y.Z (stable)
    #   - X.Y.Z-alpha.N (alpha pre-release)
    #   - X.Y.Z-beta.N (beta pre-release)
    #   - X.Y.Z-rc.N (release candidate)
    #   - X.Y.Z-dev (development)
    #   - YYYY.MM.DD[-prerelease] (date-based)
    #   - Any of above with +build.metadata
    
    # Semver regex pattern
    local semver_pattern='^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?(\+[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?$'
    # Date-based pattern
    local date_pattern='^[0-9]{4}\.[0-9]{2}\.[0-9]{2}(-[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?(\+[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?$'
    
    if [[ $base_version =~ $semver_pattern ]] || [[ $base_version =~ $date_pattern ]]; then
        return 0
    else
        return 1
    fi
}

prompt_product_channel() {
    print_header "Product and Channel"

    local current_branch=$(git branch --show-current 2>/dev/null || echo "unknown")
    print_info "Current branch: $current_branch"
    echo ""

    if [[ -n "$PRODUCT" ]]; then
        local product_valid=false
        for p in "${VALID_PRODUCTS[@]}"; do
            if [[ "$PRODUCT" == "$p" ]]; then
                product_valid=true
                break
            fi
        done
        if [[ "$product_valid" != true ]]; then
            print_error "Invalid product: $PRODUCT"
            exit 1
        fi
    fi

    if [[ -z "$PRODUCT" ]]; then
        echo "Select product:"
        echo "  1) nextgen-30x30"
        echo "  2) nextgen-50x50"
        echo "  3) nextgen-logo-55x50"
        echo "  4) nextgen-logo-100x100"
        echo "  5) nextgen-mini"
        echo "  6) nextgen-bootstrap   (first-flash provisioning)"
        read -p "Product number (1-6): " -r
        case $REPLY in
            1) PRODUCT="nextgen-30x30" ;;
            2) PRODUCT="nextgen-50x50" ;;
            3) PRODUCT="nextgen-logo-55x50" ;;
            4) PRODUCT="nextgen-logo-100x100" ;;
            5) PRODUCT="nextgen-mini" ;;
            6) PRODUCT="nextgen-bootstrap" ;;
            *) print_error "Invalid product"; exit 1 ;;
        esac
    fi

    if [[ -z "$CHANNEL" ]]; then
        echo ""
        print_info "Select channel for this branch:"
        echo "  1) stable"
        echo "  2) early"
        echo "  3) develop"
        read -p "Channel (1-3): " -n 1 -r
        echo
        case $REPLY in
            1) CHANNEL="stable" ;;
            2) CHANNEL="early" ;;
            3) CHANNEL="develop" ;;
            *) print_error "Invalid channel"; exit 1 ;;
        esac
    fi

    if [[ "$CHANNEL" != "stable" && "$CHANNEL" != "early" && "$CHANNEL" != "develop" ]]; then
        print_error "Invalid channel: $CHANNEL"
        exit 1
    fi

    set_product_paths

    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        print_info "Build-all mode: ${#BUILD_ALL_PRODUCTS[@]} products (${BUILD_ALL_PRODUCTS[*]})"
    else
        print_info "Using product: $PRODUCT"
    fi
    print_info "Using channel: $CHANNEL"
    print_info "Version file: $VERSION_FILE"
    echo ""
}

prompt_version() {
    print_header "Version Information"
    
    local current_version
    current_version=$(get_current_version)
    local current_branch=$(git branch --show-current 2>/dev/null || echo "unknown")
    print_info "Current version: $current_version"
    print_info "Current branch: $current_branch"
    echo ""

    if [[ "$UI_ONLY" == true ]]; then
        print_info "UI-only update: firmware version will remain unchanged"
        echo ""
    fi

    if [[ "$VERSION_SET" == true ]]; then
        if ! validate_version "$NEW_VERSION"; then
            print_error "Invalid version format. Must start with ${PRODUCT#wordclock-}- and follow semver: X.Y.Z[-prerelease][+build]"
            exit 1
        fi
        print_info "Using provided version: $NEW_VERSION"
    else
        # Auto-increment version based on current version
        local proposed_version
        if [[ "$CHANNEL" == "stable" ]] && is_prerelease_version "$current_version"; then
            proposed_version=$(strip_prerelease_version "$current_version")
        elif [[ "$CHANNEL" == "early" ]] && [[ "$current_version" =~ -dev([._-]|$) ]]; then
            proposed_version=$(strip_prerelease_version "$current_version")
        else
            proposed_version=$(increment_version "$current_version" "$current_branch")
        fi

        proposed_version=$(add_channel_prerelease_if_missing "$proposed_version")

        print_info "Proposed version (auto-incremented): $proposed_version"
        echo ""

        # Ask user to confirm or enter different version
        read -p "Use proposed version? (Y/n): " -n 1 -r
        echo

        if [[ $REPLY =~ ^[Nn]$ ]]; then
            echo ""
            print_info "Enter custom version number:"
            while true; do
                read -p "Version: " NEW_VERSION

                if validate_version "$NEW_VERSION"; then
                    break
                else
                    print_error "Invalid version format. Must start with ${PRODUCT#wordclock-}- and follow semver: X.Y.Z[-prerelease][+build]"
                fi
            done
        else
            NEW_VERSION="$proposed_version"
        fi
    fi
    
    # Validate version matches channel expectations
    local has_prerelease=false
    if is_prerelease_version "$NEW_VERSION"; then
        has_prerelease=true
    fi
    
    if [[ "$CHANNEL" == "stable" ]] && [[ "$has_prerelease" == true ]]; then
        print_error "Stable releases cannot have pre-release tags"
        print_error "Current version: $NEW_VERSION"
        print_error "Please use a stable version format (e.g., 27.0.0)"
        exit 1
    fi
    
    if [[ "$CHANNEL" == "early" ]] && [[ "$has_prerelease" == false ]]; then
        print_error "Early releases must have a pre-release tag (e.g., -beta.1, -rc.1)"
        print_error "Current version: $NEW_VERSION"
        print_error "Please add a pre-release tag (e.g., 27.0.0-rc.1)"
        exit 1
    fi
    
    if [[ "$CHANNEL" == "develop" ]] && [[ "$has_prerelease" == false ]]; then
        print_error "Development releases must have a pre-release tag (e.g., -dev.1, -alpha.1)"
        print_error "Current version: $NEW_VERSION"
        print_error "Please add a pre-release tag (e.g., 27.0.0-dev.1)"
        exit 1
    fi
    
    local current_branch=$(git branch --show-current 2>/dev/null || echo "unknown")
    print_info "Final version will be: $NEW_VERSION"
    print_info "Release channel: $CHANNEL"
    print_info "Building from branch: $current_branch"
    echo ""
    
    read -p "Continue? (Y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Nn]$ ]]; then
        print_error "Aborted by user"
        exit 0
    fi
    echo ""
}

prompt_release_notes() {
    print_header "Release Notes"
    echo "Enter release notes (press Ctrl+D when done, Ctrl+C to skip):"
    echo ""
    
    local interrupted=false
    local previous_trap
    previous_trap=$(trap -p INT)
    trap 'interrupted=true' INT

    # Temporarily disable exit on error for handling Ctrl+C
    set +e
    RELEASE_NOTES=$(cat 2>&1)
    local exit_code=$?
    set -e
    if [[ -n "$previous_trap" ]]; then
        eval "$previous_trap"
    else
        trap - INT
    fi
    
    # Check if cat was interrupted (Ctrl+C)
    if [[ "$interrupted" == true || $exit_code -eq 130 ]]; then
        RELEASE_NOTES=""
        echo ""
        print_info "Release notes skipped"
    elif [[ $exit_code -eq 0 ]]; then
        echo ""
        if [[ -n "$RELEASE_NOTES" ]]; then
            print_success "Release notes captured"
        else
            print_info "No release notes provided"
        fi
    else
        RELEASE_NOTES=""
        echo ""
        print_info "No release notes provided"
    fi
    echo ""
}

run_unit_tests() {
    print_header "Running Unit Tests"
    print_info "All tests must pass before creating a release..."
    
    cd "$PROJECT_ROOT"
    
    # Run native unit tests
    if pio test -e native; then
        print_success "All unit tests passed ✓"
        
        # Extract and show test count
        local test_summary=$(pio test -e native 2>&1 | grep "test cases:" || echo "Tests completed")
        print_info "$test_summary"
    else
        print_error "Unit tests failed ✗"
        print_error "Fix failing tests before creating a release"
        echo ""
        print_info "To see detailed test output, run:"
        echo "  pio test -e native -v"
        exit 1
    fi
    echo ""
}

generate_coverage_report() {
    print_header "Code Coverage Report"
    
    # Check if coverage script exists
    local coverage_script="$PROJECT_ROOT/test/generate_coverage.sh"
    if [[ ! -f "$coverage_script" ]]; then
        print_warning "Coverage script not found at $coverage_script"
        print_info "Skipping coverage report"
        COVERAGE_GENERATED=false
        echo ""
        return 0
    fi
    
    # Check if lcov is installed
    if ! command -v lcov > /dev/null 2>&1; then
        print_warning "lcov is not installed (needed for coverage reports)"
        print_info "Install with: brew install lcov (macOS) or apt install lcov (Linux)"
        print_info "Skipping coverage report"
        COVERAGE_GENERATED=false
        echo ""
        return 0
    fi
    
    # Warn about macOS limitations
    local os_name="$(uname -s)"
    if [[ "$os_name" == "Darwin" ]]; then
        print_warning "Note: Coverage instrumentation has known issues on macOS"
        print_info "Coverage report may be incomplete or fail"
        print_info "For accurate coverage, use Linux or CI/CD"
        echo ""
    fi
    
    print_info "Generating code coverage report..."
    echo ""
    
    cd "$PROJECT_ROOT"
    
    # Make script executable
    chmod +x "$coverage_script"
    
    # Run coverage generation (capture output)
    local coverage_exit_code=0
    if bash "$coverage_script" 2>&1; then
        coverage_exit_code=$?
    else
        coverage_exit_code=$?
    fi
    
    # Check if it was successful
    if [[ $coverage_exit_code -eq 0 ]]; then
        # Check if coverage.info was actually created
        if [[ -f "coverage.info" ]]; then
            print_success "Coverage report generated"
            COVERAGE_GENERATED=true
            
            # Extract coverage percentage
            local coverage_lines=$(lcov --summary coverage.info 2>&1 | grep "lines" | awk '{print $2}' || echo "")
            local coverage_funcs=$(lcov --summary coverage.info 2>&1 | grep "functions" | awk '{print $2}' || echo "")
            
            if [[ -n "$coverage_lines" ]]; then
                COVERAGE_PERCENTAGE="$coverage_lines (lines), $coverage_funcs (functions)"
                print_info "Line coverage: $coverage_lines"
                if [[ -n "$coverage_funcs" ]]; then
                    print_info "Function coverage: $coverage_funcs"
                fi
            else
                COVERAGE_PERCENTAGE="Available in report"
            fi
            
            if [[ -d "coverage_html" ]]; then
                print_info "HTML report: coverage_html/index.html"
                COVERAGE_HTML_PATH="coverage_html/index.html"
            fi
        else
            print_info "Coverage report not available on this platform"
            print_info "Tests passed successfully - coverage requires Linux/CI"
            COVERAGE_GENERATED=false
        fi
    else
        print_warning "Coverage report generation completed with warnings"
        print_info "Tests passed but coverage data unavailable (common on macOS)"
        COVERAGE_GENERATED=false
    fi
    
    echo ""
}

pre_build_check() {
    print_header "Pre-Release Build Check"
    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        print_info "Building first product ($PRODUCT) with current version to verify..."
    else
        print_info "Building with current version to verify..."
    fi

    cd "$PROJECT_ROOT"

    if [[ "$UI_ONLY" == true ]]; then
        print_info "Skipping firmware build check (UI-only update)"
        echo ""
        return 0
    fi

    if pio run --environment "$PIO_ENV"; then
        if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
            print_success "Pre-build check passed (first product only)"
        else
            print_success "Pre-build check passed"
        fi
    else
        print_error "Pre-build check failed"
        print_error "Fix build errors before creating a release"
        exit 1
    fi
    echo ""
}

update_version_in_config() {
    print_header "Updating Version"

    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        local base_version
        base_version=$(get_base_version "$NEW_VERSION" "${PRODUCT#wordclock-}")
        for p in "${BUILD_ALL_PRODUCTS[@]}"; do
            local product_version="${p#wordclock-}-${base_version}"
            local target_file="$PROJECT_ROOT/products/$p/product_config.h"
            if [[ ! -f "$target_file" ]]; then
                print_error "Product config not found: $target_file"
                exit 1
            fi
            local current_ui_version
            current_ui_version=$(grep -E '^#define UI_VERSION' "$target_file" | sed 's/.*"\(.*\)".*/\1/')
            local ui_version="$product_version"
            if [[ "$current_ui_version" == ui-* ]]; then
                ui_version="ui-$product_version"
            fi
            cp "$target_file" "$target_file.bak"
            if [[ "$UI_ONLY" != true ]]; then
                sed -i.tmp "s/^#define FIRMWARE_VERSION .*/#define FIRMWARE_VERSION \"$product_version\"/" "$target_file"
            fi
            sed -i.tmp "s/^#define UI_VERSION .*/#define UI_VERSION \"$ui_version\"/" "$target_file"
            rm -f "$target_file.tmp"
            print_success "Updated $p to $product_version"
        done
        print_info "All ${#BUILD_ALL_PRODUCTS[@]} product configs updated (base version: $base_version)"
    else
        local target_file="$VERSION_FILE"
        local current_ui_version
        current_ui_version=$(grep -E '^#define UI_VERSION' "$target_file" | sed 's/.*"\(.*\)".*/\1/')
        local ui_version="$NEW_VERSION"
        if [[ "$current_ui_version" == ui-* ]]; then
            ui_version="ui-$NEW_VERSION"
        fi

        # Create backup
        cp "$target_file" "$target_file.bak"

        # Update FIRMWARE_VERSION and/or UI_VERSION
        if [[ "$UI_ONLY" != true ]]; then
            sed -i.tmp "s/^#define FIRMWARE_VERSION .*/#define FIRMWARE_VERSION \"$NEW_VERSION\"/" "$target_file"
        fi
        sed -i.tmp "s/^#define UI_VERSION .*/#define UI_VERSION \"$ui_version\"/" "$target_file"

        # Remove temp file created by sed
        rm -f "$target_file.tmp"

        if [[ "$UI_ONLY" != true ]]; then
            print_success "Updated FIRMWARE_VERSION to $NEW_VERSION"
        else
            print_info "FIRMWARE_VERSION unchanged (UI-only update)"
        fi
        print_success "Updated UI_VERSION to $ui_version"
    fi

    # Show the changes
    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        print_info "Changes:"
        for p in "${BUILD_ALL_PRODUCTS[@]}"; do
            git diff "$PROJECT_ROOT/products/$p/product_config.h" 2>/dev/null | grep -E '^\+|^\-' | grep -E 'FIRMWARE_VERSION|UI_VERSION' || true
        done
    else
        print_info "Changes:"
        git diff "$VERSION_FILE" | grep -E '^\+|^\-' | grep -E 'FIRMWARE_VERSION|UI_VERSION' || true
    fi
    echo ""
}

commit_version_change() {
    print_header "Committing Version Change"
    
    cd "$PROJECT_ROOT"
    
    # Check if there are changes to commit
    if [[ -z $(git status -s) ]]; then
        print_info "Working tree is clean (no changes to commit)"
        
        # Check if this version was already committed
        local last_commit_msg=$(git log -1 --pretty=%B)
        local expected_msg=""
        if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
            local base_ver
            base_ver=$(get_base_version "$NEW_VERSION" "${PRODUCT#wordclock-}")
            expected_msg="Bump ${PRODUCT}* versions to $base_ver"
            if [[ "$UI_ONLY" == true ]]; then
                expected_msg="Bump ${PRODUCT}* UI versions to $base_ver"
            fi
        else
            expected_msg="Bump $PRODUCT version to $NEW_VERSION"
            if [[ "$UI_ONLY" == true ]]; then
                expected_msg="Bump $PRODUCT UI version to $NEW_VERSION"
            fi
        fi
        if [[ "$last_commit_msg" == "$expected_msg" ]]; then
            print_success "Version $NEW_VERSION already committed"
            echo ""
            print_warning "It appears this release was already started"
            print_info "Do you want to continue with the existing commit?"
            echo ""
            read -p "Continue release from this point? (Y/n): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Nn]$ ]]; then
                print_error "Release cancelled"
                exit 0
            fi
            print_info "Continuing with existing version commit..."
        else
            print_error "No changes to commit, but last commit is not a version bump"
            print_error "Last commit: $last_commit_msg"
            print_info "Expected: $expected_msg"
            echo ""
            print_warning "This may indicate:"
            echo "  - Version was already updated manually"
            echo "  - You're trying to release an already-released version"
            echo ""
            read -p "Force continue anyway? (y/N): " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                print_error "Release cancelled"
                exit 0
            fi
        fi
        echo ""
        return 0
    fi
    
    # Add version file(s) and any modified HTML files (console logs removed)
    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        local base_version
        base_version=$(get_base_version "$NEW_VERSION" "${PRODUCT#wordclock-}")
        for p in "${BUILD_ALL_PRODUCTS[@]}"; do
            git add "$PROJECT_ROOT/products/$p/product_config.h"
        done
        if [[ -n $(git status -s data/*.html 2>/dev/null) ]]; then
            git add data/*.html
            print_info "Also committing HTML files (console logs removed)"
        fi
        if [[ "$UI_ONLY" == true ]]; then
            git commit -m "Bump ${PRODUCT}* UI versions to $base_version"
        else
            git commit -m "Bump ${PRODUCT}* versions to $base_version"
        fi
    else
        git add "$VERSION_FILE"
        if [[ -n $(git status -s data/*.html 2>/dev/null) ]]; then
            git add data/*.html
            print_info "Also committing HTML files (console logs removed)"
        fi
        if [[ "$UI_ONLY" == true ]]; then
            git commit -m "Bump $PRODUCT UI version to $NEW_VERSION"
        else
            git commit -m "Bump $PRODUCT version to $NEW_VERSION"
        fi
    fi
    
    print_success "Version change committed"
    echo ""
}

remove_console_logs_from_source() {
    # Remove console logs from source HTML files if not on dev branch
    if [[ "$CHANNEL" == "develop" ]]; then
        print_info "Channel is 'develop', keeping all console statements"
        return 0
    fi

    print_info "Channel is '$CHANNEL', removing console statements from source files..."
    
    # Use Python script to remove console logs from source files
    if command -v python3 &> /dev/null; then
        cd "$PROJECT_ROOT"
        RELEASE_CHANNEL=\"$CHANNEL\" python3 -c "
import os
import re

def remove_console_statements(content):
    console_methods = r'(log|error|warn|info|debug|trace|table|dir|dirxml|group|groupEnd|time|timeEnd|assert|clear|count|countReset|profile|profileEnd)'
    lines = content.split('\n')
    result_lines = []
    i = 0
    
    while i < len(lines):
        line = lines[i]
        if re.search(r'console\.' + console_methods + r'\s*\(', line):
            if ';' in line:
                match = re.search(r'console\.' + console_methods + r'\s*\([^;]*?\)\s*;', line)
                if match:
                    before = line[:match.start()].rstrip()
                    after = line[match.end():].strip()
                    if before or after:
                        result_lines.append((before + ' ' + after).strip())
                i += 1
                continue
            
            paren_count = 0
            found_start = False
            j = i
            
            while j < len(lines):
                current_line = lines[j]
                if not found_start:
                    match = re.search(r'console\.' + console_methods + r'\s*\(', current_line)
                    if match:
                        found_start = True
                        paren_count = 1
                        remaining = current_line[match.end():]
                        for char in remaining:
                            if char == '(':
                                paren_count += 1
                            elif char == ')':
                                paren_count -= 1
                                if paren_count == 0:
                                    if ';' in remaining:
                                        i = j + 1
                                        break
                else:
                    for char in current_line:
                        if char == '(':
                            paren_count += 1
                        elif char == ')':
                            paren_count -= 1
                            if paren_count == 0:
                                if ';' in current_line:
                                    i = j + 1
                                    break
                
                if paren_count == 0:
                    break
                j += 1
            
            if j < len(lines):
                i = j + 1
            else:
                i += 1
            continue
        else:
            result_lines.append(line)
            i += 1
    
    return '\n'.join(result_lines)

channel = os.environ.get('RELEASE_CHANNEL', '')
if channel == 'develop':
    exit(0)

data_dir = os.path.join(os.getcwd(), 'data')
if not os.path.exists(data_dir):
    print(f'Error: Data directory not found: {data_dir}')
    exit(1)

processed_count = 0
for root, dirs, files in os.walk(data_dir):
    for file in files:
        if file.endswith('.html'):
            html_file = os.path.join(root, file)
            try:
                with open(html_file, 'r', encoding='utf-8') as f:
                    content = f.read()
                original_content = content
                content = remove_console_statements(content)
                if content != original_content:
                    with open(html_file, 'w', encoding='utf-8') as f:
                        f.write(content)
                    processed_count += 1
                    print(f'Processed: {os.path.basename(html_file)}')
            except Exception as e:
                print(f'Error processing {html_file}: {e}')

if processed_count > 0:
    print(f'Removed console statements from {processed_count} file(s)')
else:
    print('No console statements found to remove')
"
        if [[ $? -eq 0 ]]; then
            print_success "Console logs removed from source files"
        else
            print_warning "Failed to remove console logs from source files"
        fi
    else
        print_warning "Python 3 not found, skipping console log removal from source"
    fi
}

release_build() {
    print_header "Release Build"
    RELEASE_BINARIES=()

    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        local base_version
        base_version=$(get_base_version "$NEW_VERSION" "${PRODUCT#wordclock-}")
        if [[ "$UI_ONLY" == true ]]; then
            print_info "UI-only: building filesystem once for first product..."
        else
            print_info "Building ${#BUILD_ALL_PRODUCTS[@]} firmware variants (base version: $base_version)..."
        fi
        cd "$PROJECT_ROOT"
        mkdir -p "$DIST_DIR"

        local idx=0
        for p in "${BUILD_ALL_PRODUCTS[@]}"; do
            idx=$((idx + 1))
            print_info "[$idx/${#BUILD_ALL_PRODUCTS[@]}] Building $p ..."
            PRODUCT="$p"
            set_product_paths

            pio run --target clean --environment "$PIO_ENV"

            print_info "Building filesystem for $p ..."
            if ! RELEASE_CHANNEL="$CHANNEL" pio run --target buildfs --environment "$PIO_ENV"; then
                print_error "Filesystem build failed for $p"
                rollback_changes
                exit 1
            fi

            if [[ "$UI_ONLY" == true ]]; then
                print_success "Filesystem built for $p"
                continue
            fi

            if ! RELEASE_CHANNEL="$CHANNEL" pio run --environment "$PIO_ENV"; then
                print_error "Firmware build failed for $p"
                rollback_changes
                exit 1
            fi

            local product_version="${p#wordclock-}-${base_version}"
            local release_filename
            release_filename=$(get_release_filename "$product_version" "$p")
            local release_binary="$DIST_DIR/$release_filename"
            cp "$BUILD_OUTPUT" "$release_binary"
            RELEASE_BINARIES+=("$release_binary")
            local size
            size=$(ls -lh "$release_binary" | awk '{print $5}')
            print_success "Binary: $release_binary ($size)"
        done

        if [[ "$UI_ONLY" == true ]]; then
            print_success "UI-only build complete (filesystem built for all)"
        else
            print_success "All ${#BUILD_ALL_PRODUCTS[@]} binaries created in $DIST_DIR"
        fi
        echo ""
        return 0
    fi

    if [[ "$UI_ONLY" == true ]]; then
        print_info "Building UI filesystem (UI-only update)..."
    else
        print_info "Building firmware v$NEW_VERSION..."
    fi

    cd "$PROJECT_ROOT"

    # Clean build
    pio run --target clean --environment "$PIO_ENV"

    # Build filesystem first (this runs remove_console_logs.py script)
    print_info "Building filesystem (removing console logs if not on dev branch)..."
    if RELEASE_CHANNEL="$CHANNEL" pio run --target buildfs --environment "$PIO_ENV"; then
        print_success "Filesystem build successful"
    else
        print_error "Filesystem build failed"
        rollback_changes
        exit 1
    fi

    if [[ "$UI_ONLY" == true ]]; then
        print_success "UI-only build complete (firmware skipped)"
        echo ""
        return 0
    fi

    # Build firmware
    if RELEASE_CHANNEL="$CHANNEL" pio run --environment "$PIO_ENV"; then
        print_success "Firmware build successful"
    else
        print_error "Firmware build failed"
        rollback_changes
        exit 1
    fi

    # Create dist directory
    mkdir -p "$DIST_DIR"

    # Copy and rename binary
    local release_filename
    release_filename=$(get_release_filename "$NEW_VERSION" "$PRODUCT")
    RELEASE_BINARY="$DIST_DIR/$release_filename"
    cp "$BUILD_OUTPUT" "$RELEASE_BINARY"
    RELEASE_BINARIES=("$RELEASE_BINARY")

    print_success "Binary created: $RELEASE_BINARY"

    # Show binary size
    local size=$(ls -lh "$RELEASE_BINARY" | awk '{print $5}')
    print_info "Binary size: $size"
    echo ""
}

create_git_tag() {
    if [[ "$UI_ONLY" == true ]]; then
        print_info "Skipping git tag (UI-only update)"
        echo ""
        return 0
    fi
    print_header "Creating Git Tag"
    
    cd "$PROJECT_ROOT"
    
    local tag="$NEW_VERSION"
    
    # Check if tag already exists
    if git rev-parse "$tag" >/dev/null 2>&1; then
        print_warning "Tag $tag already exists locally"
        
        # Check if it exists on remote too
        if git ls-remote --tags --refs origin "refs/tags/$tag" | grep -q .; then
            print_warning "Tag also exists on remote"
            echo ""
            print_info "Options:"
            echo "  1) Use existing tag and continue to push/release steps"
            echo "  2) Delete local and remote tag, then recreate"
            echo "  3) Cancel release"
            echo ""
            read -p "Choose option (1-3): " -n 1 -r
            echo
            case $REPLY in
                1)
                    print_info "Using existing tag $tag"
                    echo ""
                    return 0
                    ;;
                2)
                    print_info "Deleting local tag..."
                    git tag -d "$tag"
                    print_info "Deleting remote tag..."
                    git push origin ":refs/tags/$tag" 2>/dev/null || true
                    print_success "Tags deleted"
                    ;;
                3)
                    print_error "Release cancelled"
                    exit 0
                    ;;
                *)
                    print_error "Invalid choice"
                    exit 1
                    ;;
            esac
        else
            print_info "Tag exists locally but not on remote"
            read -p "Delete and recreate? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                git tag -d "$tag"
                print_info "Local tag deleted"
            else
                print_info "Using existing local tag"
                echo ""
                return 0
            fi
        fi
    fi
    
    # Create annotated tag
    if [[ -n "$RELEASE_NOTES" ]]; then
        git tag -a "$tag" -m "Release $NEW_VERSION" -m "$RELEASE_NOTES"
    else
        git tag -a "$tag" -m "Release $NEW_VERSION"
    fi
    
    print_success "Tag $tag created"
    echo ""
}

push_changes() {
    if [[ "$UI_ONLY" == true ]]; then
        print_info "Skipping push (UI-only update)"
        echo ""
        return 0
    fi
    print_header "Pushing Changes to GitHub"
    
    cd "$PROJECT_ROOT"
    
    local current_branch=$(git branch --show-current)
    local tag="$NEW_VERSION"
    
    print_info "Current branch: $current_branch"
    print_info "Tag: $tag"
    echo ""
    
    # Check if tag already exists on remote
    if git ls-remote --tags --refs origin "refs/tags/$tag" | grep -q .; then
        print_info "Tag $tag already exists on remote"
        read -p "Skip push step? (Y/n): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Nn]$ ]]; then
            print_info "Push skipped (tag already on remote)"
            echo ""
            return 0
        fi
    fi
    
    read -p "Push branch and tag to origin? (Y/n): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        # Push current branch
        if git push origin "$current_branch" 2>&1 | grep -q "Everything up-to-date"; then
            print_info "Branch already up-to-date"
        else
            print_success "Branch pushed"
        fi
        
        # Push tag (check if it already exists)
        if git ls-remote --tags --refs origin "refs/tags/$tag" | grep -q .; then
            print_info "Tag already exists on remote"
        else
            git push origin "$tag"
            print_success "Tag pushed"
        fi
    else
        print_warning "Skipped push (you'll need to push manually)"
    fi
    echo ""
}

create_github_release() {
    if [[ "$UI_ONLY" == true ]]; then
        print_info "Skipping GitHub release (UI-only update)"
        echo ""
        return 0
    fi
    print_header "Creating GitHub Release"
    
    cd "$PROJECT_ROOT"
    
    local tag="$NEW_VERSION"
    local title="$PRODUCT $NEW_VERSION"
    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        title="${PRODUCT}* $NEW_VERSION (${#RELEASE_BINARIES[@]} variants)"
    fi

    # Check if release already exists
    if gh release view "$tag" &>/dev/null; then
        print_warning "Release $tag already exists on GitHub"
        echo ""
        echo "Options:"
        echo "  1) Delete and recreate release"
        echo "  2) Skip release creation (continue with firmware.json update)"
        echo "  3) Abort"
        echo ""
        
        while true; do
            read -p "Choose option (1-3): " -n 1 -r
            echo
            case $REPLY in
                1)
                    print_info "Deleting existing release..."
                    if gh release delete "$tag" --yes; then
                        print_success "Existing release deleted"
                        break
                    else
                        print_error "Failed to delete existing release"
                        return 1
                    fi
                    ;;
                2)
                    print_info "Skipping release creation"
                    print_success "Release already exists at: $(gh release view $tag --json url -q .url)"
                    echo ""
                    return 0
                    ;;
                3)
                    print_error "Aborted by user"
                    exit 1
                    ;;
                *)
                    print_error "Invalid choice"
                    ;;
            esac
        done
        echo ""
    fi
    
    # Prepare release command
    local gh_cmd="gh release create $tag"
    gh_cmd="$gh_cmd --title \"$title\""
    
    # Add release notes
    if [[ -n "$RELEASE_NOTES" ]]; then
        local notes_file="$DIST_DIR/release_notes_$NEW_VERSION.md"
        echo "$RELEASE_NOTES" > "$notes_file"
        gh_cmd="$gh_cmd --notes-file \"$notes_file\""
    else
        gh_cmd="$gh_cmd --generate-notes"
    fi
    
    # Mark as pre-release for early and develop channels
    if [[ "$CHANNEL" == "develop" || "$CHANNEL" == "early" ]]; then
        gh_cmd="$gh_cmd --prerelease"
        print_info "Will mark as pre-release"
    fi
    
    # Mark as latest only for stable channel
    if [[ "$CHANNEL" == "stable" ]]; then
        gh_cmd="$gh_cmd --latest"
        print_info "Will mark as latest release"
    fi
    
    # Add binary (or all binaries for build-all)
    if [[ ${#RELEASE_BINARIES[@]} -gt 0 ]]; then
        for b in "${RELEASE_BINARIES[@]}"; do
            gh_cmd="$gh_cmd \"$b\""
        done
    else
        local single_binary="$DIST_DIR/$(get_release_filename "$NEW_VERSION" "$PRODUCT")"
        gh_cmd="$gh_cmd \"$single_binary\""
    fi

    print_info "Creating release on GitHub..."
    print_info "Command: $gh_cmd"
    echo ""
    
    # Execute release creation
    if eval $gh_cmd; then
        print_success "GitHub release created successfully"
        print_success "View at: $(gh release view $tag --json url -q .url)"
    else
        print_error "Failed to create GitHub release"
        print_warning "You may need to create it manually"
        return 1
    fi
    echo ""
}

# Legacy OTA1 manifest update - deprecated, all products now use OTA2
update_firmware_manifest() {
    # All products now use OTA2 - this function is kept for backward compatibility
    # but does nothing. Use publish_ota2_manifests() instead.
    return 0
}

publish_ota2_manifests() {
    print_header "Publish OTA2 Manifests"

    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        print_info "Publishing OTA2 manifests for ${#BUILD_ALL_PRODUCTS[@]} products..."
        echo ""
        read -p "Publish OTA2 manifests now? (Y/n): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Nn]$ ]]; then
            print_info "Skipping OTA2 publish"
            echo ""
            return 0
        fi
        for p in "${BUILD_ALL_PRODUCTS[@]}"; do
            local vf="$PROJECT_ROOT/products/$p/product_config.h"
            local fw_version fs_version
            fw_version=$(grep -E '^#define FIRMWARE_VERSION' "$vf" | sed 's/.*"\(.*\)".*/\1/')
            fs_version=$(grep -E '^#define UI_VERSION' "$vf" | sed 's/.*"\(.*\)".*/\1/')
            if [[ -z "$fw_version" || -z "$fs_version" ]]; then
                print_error "Could not read versions from $vf"
                return 1
            fi
            if [[ "$UI_ONLY" == true ]]; then
                fw_version=""
            fi
            print_info "Publishing OTA2 for $p (fw=$fw_version, fs=$fs_version)..."
            if [[ "$UI_ONLY" == true ]]; then
                "$PROJECT_ROOT/tools/publish-ota.sh" --product "$p" --channel "$CHANNEL" --fs-version "$fs_version" --fs-only --yes
            else
                "$PROJECT_ROOT/tools/publish-ota.sh" --product "$p" --channel "$CHANNEL" --fw-version "$fw_version" --fs-version "$fs_version" --yes
            fi
        done
        print_success "OTA2 manifests published for all ${#BUILD_ALL_PRODUCTS[@]} products"
    else
        local fw_version fs_version
        fw_version=$(grep -E '^#define FIRMWARE_VERSION' "$VERSION_FILE" | sed 's/.*"\(.*\)".*/\1/')
        fs_version=$(grep -E '^#define UI_VERSION' "$VERSION_FILE" | sed 's/.*"\(.*\)".*/\1/')

        if [[ -z "$fw_version" || -z "$fs_version" ]]; then
            print_error "Could not read versions from $VERSION_FILE"
            return 1
        fi

        print_info "Product: $PRODUCT"
        print_info "Channel: $CHANNEL"
        print_info "FW version: $fw_version"
        print_info "FS version: $fs_version"
        echo ""

        read -p "Publish OTA2 manifests now? (Y/n): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Nn]$ ]]; then
            print_info "Skipping OTA2 publish"
            echo ""
            return 0
        fi

        if [[ "$UI_ONLY" == true ]]; then
            fw_version=""
        fi

        if [[ "$UI_ONLY" == true ]]; then
            "$PROJECT_ROOT/tools/publish-ota.sh" \
              --product "$PRODUCT" \
              --channel "$CHANNEL" \
              --fs-version "$fs_version" \
              --fs-only \
              --yes
        else
            "$PROJECT_ROOT/tools/publish-ota.sh" \
              --product "$PRODUCT" \
              --channel "$CHANNEL" \
              --fw-version "$fw_version" \
              --fs-version "$fs_version" \
              --yes
        fi
    fi
    echo ""
}

rollback_changes() {
    print_header "Rolling Back Changes"

    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        for p in "${BUILD_ALL_PRODUCTS[@]}"; do
            local cfg="$PROJECT_ROOT/products/$p/product_config.h"
            if [[ -f "$cfg.bak" ]]; then
                mv "$cfg.bak" "$cfg"
                print_info "Restored $p config from backup"
            fi
        done
        local base_ver
        base_ver=$(get_base_version "$NEW_VERSION" "${PRODUCT#wordclock-}")
        local expected_msg="Bump ${PRODUCT}* versions to $base_ver"
        if [[ "$UI_ONLY" == true ]]; then
            expected_msg="Bump ${PRODUCT}* UI versions to $base_ver"
        fi
        local last_commit_msg
        last_commit_msg=$(git log -1 --pretty=%B)
        if [[ "$last_commit_msg" == "$expected_msg" ]]; then
            git reset --soft HEAD~1
            for p in "${BUILD_ALL_PRODUCTS[@]}"; do
                git restore --staged "$PROJECT_ROOT/products/$p/product_config.h"
            done
            print_info "Reverted version bump commit"
        fi
    else
        # Restore config.h backup if it exists
        if [[ -f "$VERSION_FILE.bak" ]]; then
            mv "$VERSION_FILE.bak" "$VERSION_FILE"
            print_info "Restored version file from backup"
        fi

        # Revert last commit if it was the version bump
        local last_commit_msg
        last_commit_msg=$(git log -1 --pretty=%B)
        if [[ "$last_commit_msg" == "Bump $PRODUCT version to $NEW_VERSION" || "$last_commit_msg" == "Bump $PRODUCT UI version to $NEW_VERSION" ]]; then
            git reset --soft HEAD~1
            git restore --staged "$VERSION_FILE"
            print_info "Reverted version bump commit"
        fi
    fi

    print_warning "Rollback complete"
}

cleanup() {
    if [[ ${#BUILD_ALL_PRODUCTS[@]} -gt 0 ]]; then
        for p in "${BUILD_ALL_PRODUCTS[@]}"; do
            local cfg="$PROJECT_ROOT/products/$p/product_config.h"
            if [[ -f "$cfg.bak" ]]; then
                rm -f "$cfg.bak"
            fi
        done
    else
        if [[ -f "$VERSION_FILE.bak" ]]; then
            rm -f "$VERSION_FILE.bak"
        fi
    fi
}

print_summary() {
    print_header "Release Summary"
    if [[ "$UI_ONLY" == true ]]; then
        echo -e "${GREEN}UI update $NEW_VERSION completed successfully!${NC}"
    else
        echo -e "${GREEN}Release $NEW_VERSION completed successfully!${NC}"
    fi
    echo ""
    local release_branch=$(git branch --show-current 2>/dev/null || echo "unknown")
    echo "Details:"
    echo "  Product: $PRODUCT"
    echo "  Version: $NEW_VERSION"
    echo "  Branch: $release_branch"
    echo "  Channel: $CHANNEL"
    if [[ "$UI_ONLY" == true ]]; then
        echo "  Binary: (skipped - UI-only)"
        echo "  Tag: (skipped - UI-only)"
    elif [[ ${#RELEASE_BINARIES[@]} -gt 0 ]]; then
        echo "  Binaries (${#RELEASE_BINARIES[@]}):"
        for b in "${RELEASE_BINARIES[@]}"; do
            echo "    - $b"
        done
        echo "  Tag: $NEW_VERSION"
    else
        echo "  Binary: $DIST_DIR/$(get_release_filename "$NEW_VERSION" "$PRODUCT")"
        echo "  Tag: $NEW_VERSION"
    fi
    if [[ "$TESTS_RUN" == true ]]; then
        echo "  Tests: ✓ All passed"
    elif [[ "$TESTS_SKIPPED" == true ]]; then
        echo "  Tests: skipped"
    else
        echo "  Tests: not run"
    fi
    
    # Show coverage info if generated
    if [[ "$COVERAGE_GENERATED" == true ]]; then
        echo "  Coverage: $COVERAGE_PERCENTAGE"
        if [[ -n "$COVERAGE_HTML_PATH" ]]; then
            echo "  Coverage Report: $COVERAGE_HTML_PATH"
        fi
    fi
    
    echo ""
    print_info "Next steps:"
    local step_num=1
    echo "  ${step_num}. Verify the release on GitHub"
    step_num=$((step_num + 1))
    
    # Show coverage report instruction if generated
    if [[ "$COVERAGE_GENERATED" == true && -n "$COVERAGE_HTML_PATH" ]]; then
        echo "  2. Review coverage report: open $COVERAGE_HTML_PATH"
    fi
    
    # All products use OTA2
    echo "  ${step_num}. OTA2 manifests published via tools/publish-ota.sh"
    step_num=$((step_num + 1))
    echo "  ${step_num}. Test the OTA update process"
    step_num=$((step_num + 1))
    echo "  ${step_num}. Announce the release"
    echo ""
}

update_manifest_only() {
    print_header "Firmware Manifest Update (Deprecated)"
    echo ""
    print_warning "The --update-manifest (-m) flag is deprecated."
    print_info "All products now use OTA2. Use publish-ota.sh directly:"
    echo ""
    echo "  ./tools/publish-ota.sh --product <product> --channel <channel>"
    echo ""
    print_info "Example:"
    echo "  ./tools/publish-ota.sh --product nextgen-30x30 --channel stable"
    echo ""
    exit 0
}

# Main execution
main() {
    print_header "Wordclock Release Pipeline"
    echo "This script will guide you through creating a new release"
    echo ""
    
    # Check prerequisites
    check_prerequisites

    # Select product and channel for this branch
    prompt_product_channel
    
    # Get version and release info
    prompt_version
    prompt_release_notes
    
    # Run unit tests (quality gate)
    read -p "Run unit tests? (Y/n): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        run_unit_tests
        TESTS_RUN=true
    else
        print_warning "Skipping unit tests (NOT RECOMMENDED for releases)"
        TESTS_SKIPPED=true
        echo ""
    fi
    
    # Generate coverage report (optional)
    read -p "Generate code coverage report? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        generate_coverage_report
    else
        print_info "Skipping coverage report"
        COVERAGE_GENERATED=false
        echo ""
    fi
    
    # Pre-release build check (skip in build-all: we build all in release_build and would duplicate first product)
    if [[ ${#BUILD_ALL_PRODUCTS[@]} -eq 0 ]]; then
        pre_build_check
    else
        print_header "Pre-Release Build Check"
        print_info "Skipping pre-build check (build-all will build all ${#BUILD_ALL_PRODUCTS[@]} products in release step)"
        echo ""
    fi

    # Update version in config
    update_version_in_config
    
    # Remove console logs from source files if not on dev branch (before commit)
    remove_console_logs_from_source
    
    # Commit the version change (and HTML files if console logs were removed)
    commit_version_change
    
    # Build release binary
    release_build
    
    # Create git tag
    if [[ "$SKIP_GIT_RELEASE" != true ]]; then
        read -p "Skip git tag/push/GitHub release? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            SKIP_GIT_RELEASE=true
        fi
    fi

    if [[ "$SKIP_GIT_RELEASE" != true ]]; then
        create_git_tag
        
        # Push to GitHub
        push_changes
        
        # Create GitHub release
        create_github_release
    else
        print_info "Skipping git tag/push/GitHub release (publish manifests only)"
    fi
    
    # OTA publish (all products use OTA2)
    publish_ota2_manifests
    
    # Cleanup
    cleanup
    
    # Print summary
    print_summary
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --update-manifest|-m)
            MODE="manifest"
            shift
            ;;
        --no-tag-release)
            SKIP_GIT_RELEASE=true
            shift
            ;;
        --product)
            PRODUCT="$2"
            shift 2
            ;;
        --channel)
            CHANNEL="$2"
            shift 2
            ;;
        --version)
            NEW_VERSION="$2"
            VERSION_SET=true
            shift 2
            ;;
        --ui-only)
            UI_ONLY=true
            shift
            ;;
        --help|-h)
            echo "Wordclock Release Pipeline"
            echo ""
            echo "Usage:"
            echo "  ./release.sh                               Full release pipeline"
            echo "  ./release.sh --product <product>           Set product"
            echo "  ./release.sh --channel <channel>           Set channel (stable|early|develop)"
            echo "  ./release.sh --version <version>           Set version (skip prompt)"
            echo "  ./release.sh --ui-only                     UI-only update (no firmware build/tag/release)"
            echo "  ./release.sh --no-tag-release              Build + publish manifests without git tag/release"
            echo "  ./release.sh --update-manifest             (deprecated) Use publish-ota.sh instead"
            echo "  ./release.sh --help                        Show this help"
            echo ""
            echo "Version Auto-increment:"
            echo "  - 26.2.2-dev.5  -> 26.2.2-dev.6  (increments dev number)"
            echo "  - 26.2.2-rc.1   -> 26.2.2-rc.2   (increments rc number)"
            echo "  - 26.2.2        -> 26.2.3        (increments patch version)"
            echo ""
            exit 0
            ;;
        *)
            print_error "Unknown argument: $1"
            exit 1
            ;;
    esac
done

if [[ "$MODE" == "manifest" ]]; then
    update_manifest_only
    exit 0
fi

# Trap errors and cleanup
trap cleanup EXIT

# Run main
main "$@"
