#!/bin/bash

# Wordclock Release Merge Script
# Merges branches in the release pipeline:
#   - dev → early-release-channel
#   - early-release-channel → main

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Functions
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

check_prerequisites() {
    print_header "Checking Prerequisites"
    
    # Check if git is available
    if ! command -v git &> /dev/null; then
        print_error "git is not installed"
        exit 1
    fi
    print_success "git found"
    
    # Check if we're in a git repository
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        print_error "Not in a git repository"
        exit 1
    fi
    print_success "Git repository detected"
    
    # Check for uncommitted changes
    if [[ -n $(git status -s) ]]; then
        print_warning "You have uncommitted changes"
        print_info "Current changes:"
        git status -s | head -10
        echo ""
        read -p "Continue anyway? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            print_error "Aborted. Please commit or stash your changes first."
            exit 1
        fi
    fi
    
    echo ""
}

get_current_branch() {
    git branch --show-current 2>/dev/null || echo "unknown"
}

determine_merge_path() {
    local current_branch=$(get_current_branch)
    
    case "$current_branch" in
        dev|develop|development)
            SOURCE_BRANCH="dev"
            TARGET_BRANCH="early-release-channel"
            MERGE_TYPE="dev_to_early"
            ;;
        early-release-channel|early-release|early)
            SOURCE_BRANCH="early-release-channel"
            TARGET_BRANCH="main"
            MERGE_TYPE="early_to_main"
            ;;
        *)
            print_error "Invalid branch for merge: $current_branch"
            echo ""
            print_info "This script can only merge from:"
            echo "  - dev/develop branch → early-release-channel"
            echo "  - early-release-channel branch → main"
            echo ""
            print_info "Current branch: $current_branch"
            print_info "Please checkout the source branch first:"
            echo "  git checkout dev                    # for dev → early-release-channel"
            echo "  git checkout early-release-channel  # for early-release-channel → main"
            exit 1
            ;;
    esac
}

fetch_latest() {
    print_header "Fetching Latest Changes"
    
    print_info "Fetching from origin..."
    git fetch origin
    
    # Check if branches exist on remote
    if git ls-remote --heads origin "$SOURCE_BRANCH" | grep -q "refs/heads/$SOURCE_BRANCH"; then
        print_success "Source branch '$SOURCE_BRANCH' exists on remote"
    else
        print_warning "Source branch '$SOURCE_BRANCH' not found on remote"
    fi
    
    if git ls-remote --heads origin "$TARGET_BRANCH" | grep -q "refs/heads/$TARGET_BRANCH"; then
        print_success "Target branch '$TARGET_BRANCH' exists on remote"
    else
        print_warning "Target branch '$TARGET_BRANCH' not found on remote"
    fi
    
    echo ""
}

confirm_merge() {
    print_header "Merge Confirmation"
    
    local current_branch=$(get_current_branch)
    local source_sha=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
    local target_sha=$(git rev-parse --short "origin/$TARGET_BRANCH" 2>/dev/null || echo "unknown")
    
    print_info "Merge plan:"
    echo "  From: $SOURCE_BRANCH (current branch: $current_branch)"
    echo "    SHA: $source_sha"
    echo "  To:   $TARGET_BRANCH"
    echo "    SHA: $target_sha"
    echo ""
    
    # Show commit count
    local commit_count=$(git rev-list --count "$TARGET_BRANCH".."$SOURCE_BRANCH" 2>/dev/null || echo "unknown")
    if [[ "$commit_count" != "unknown" ]] && [[ "$commit_count" != "0" ]]; then
        print_info "Commits to merge: $commit_count"
        echo ""
        print_info "Recent commits that will be merged:"
        git log --oneline "$TARGET_BRANCH".."$SOURCE_BRANCH" | head -5
        echo ""
    fi
    
    read -p "Proceed with merge? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        print_error "Merge cancelled by user"
        exit 0
    fi
    echo ""
}

perform_merge() {
    print_header "Performing Merge"
    
    cd "$PROJECT_ROOT"
    
    # Ensure we're on the source branch
    local current_branch=$(get_current_branch)
    if [[ "$current_branch" != "$SOURCE_BRANCH" ]]; then
        print_info "Switching to source branch: $SOURCE_BRANCH"
        git checkout "$SOURCE_BRANCH"
        git pull origin "$SOURCE_BRANCH" || true
    fi
    
    # Switch to target branch
    print_info "Switching to target branch: $TARGET_BRANCH"
    
    # Check if target branch exists locally
    if git show-ref --verify --quiet refs/heads/"$TARGET_BRANCH"; then
        git checkout "$TARGET_BRANCH"
        print_success "Checked out local $TARGET_BRANCH branch"
    else
        # Create local branch tracking remote
        git checkout -b "$TARGET_BRANCH" "origin/$TARGET_BRANCH" 2>/dev/null || {
            print_error "Could not create local branch from origin/$TARGET_BRANCH"
            print_info "Target branch may not exist on remote"
            exit 1
        }
        print_success "Created local $TARGET_BRANCH branch tracking origin/$TARGET_BRANCH"
    fi
    
    # Pull latest changes
    print_info "Pulling latest changes from origin/$TARGET_BRANCH..."
    if git pull origin "$TARGET_BRANCH"; then
        print_success "Target branch is up to date"
    else
        print_warning "Could not pull from origin (may not exist or network issue)"
    fi
    
    echo ""
    
    # Perform merge
    print_info "Merging $SOURCE_BRANCH into $TARGET_BRANCH..."
    
    if git merge "$SOURCE_BRANCH" --no-ff -m "Merge $SOURCE_BRANCH into $TARGET_BRANCH"; then
        print_success "Merge completed successfully"
    else
        print_error "Merge failed due to conflicts"
        echo ""
        print_info "Conflicted files:"
        git diff --name-only --diff-filter=U
        echo ""
        print_warning "Please resolve conflicts manually:"
        echo "  1. Fix conflicts in the files listed above"
        echo "  2. Stage resolved files: git add <file>"
        echo "  3. Complete merge: git commit"
        echo ""
        print_info "Or abort merge: git merge --abort"
        exit 1
    fi
    
    echo ""
}

push_changes() {
    print_header "Push Changes"
    
    local current_branch=$(get_current_branch)
    
    print_info "Current branch: $current_branch"
    print_info "Target branch on remote: origin/$TARGET_BRANCH"
    echo ""
    
    read -p "Push merged changes to origin/$TARGET_BRANCH? (Y/n): " -n 1 -r
    echo
    
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        print_info "Pushing to origin/$TARGET_BRANCH..."
        if git push origin "$TARGET_BRANCH"; then
            print_success "Changes pushed successfully"
        else
            print_error "Failed to push changes"
            print_warning "You may need to push manually:"
            echo "  git push origin $TARGET_BRANCH"
            exit 1
        fi
    else
        print_warning "Push skipped. Remember to push manually:"
        echo "  git push origin $TARGET_BRANCH"
    fi
    
    echo ""
}

print_summary() {
    print_header "Merge Summary"
    
    local current_branch=$(get_current_branch)
    local merge_sha=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
    
    echo -e "${GREEN}Merge completed successfully!${NC}"
    echo ""
    echo "Details:"
    echo "  Merged: $SOURCE_BRANCH → $TARGET_BRANCH"
    echo "  Current branch: $current_branch"
    echo "  Merge commit: $merge_sha"
    echo ""
    
    if [[ "$MERGE_TYPE" == "dev_to_early" ]]; then
        print_info "Next steps:"
        echo "  1. Test the early-release-channel branch"
        echo "  2. When ready, merge to main:"
        echo "     git checkout early-release-channel"
        echo "     ./tools/merge_release.sh"
    elif [[ "$MERGE_TYPE" == "early_to_main" ]]; then
        print_info "Next steps:"
        echo "  1. Verify the merge on main branch"
        echo "  2. Create a release:"
        echo "     ./tools/release.sh"
    fi
    
    echo ""
}

show_help() {
    echo "Wordclock Release Merge Script"
    echo ""
    echo "Usage:"
    echo "  ./tools/merge_release.sh              Merge from current branch"
    echo "  ./tools/merge_release.sh --help       Show this help"
    echo ""
    echo "Supported merge paths:"
    echo "  - dev → early-release-channel"
    echo "    (checkout dev branch first)"
    echo ""
    echo "  - early-release-channel → main"
    echo "    (checkout early-release-channel branch first)"
    echo ""
    echo "Examples:"
    echo "  # Merge dev to early-release-channel"
    echo "  git checkout dev"
    echo "  ./tools/merge_release.sh"
    echo ""
    echo "  # Merge early-release-channel to main"
    echo "  git checkout early-release-channel"
    echo "  ./tools/merge_release.sh"
    echo ""
}

# Main execution
main() {
    if [[ "$1" == "--help" || "$1" == "-h" ]]; then
        show_help
        exit 0
    fi
    
    print_header "Wordclock Release Merge"
    echo "This script will merge branches in the release pipeline"
    echo ""
    
    # Check prerequisites
    check_prerequisites
    
    # Determine merge path based on current branch
    determine_merge_path
    
    # Fetch latest changes
    fetch_latest
    
    # Confirm merge
    confirm_merge
    
    # Perform merge
    perform_merge
    
    # Push changes
    push_changes
    
    # Print summary
    print_summary
}

# Run main
main "$@"



