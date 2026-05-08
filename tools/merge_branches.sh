#!/bin/bash

# Wordclock Branch Merge Script
# Automates merging from dev to early-release-channel and from early-release-channel to main
#
# Usage:
#   ./merge_branches.sh                    - Full merge pipeline (dev -> early-release-channel -> main)
#   ./merge_branches.sh --dev-to-early     - Only merge dev to early-release-channel
#   ./merge_branches.sh --early-to-main    - Only merge early-release-channel to main

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV_BRANCH="dev"
EARLY_BRANCH="early-release-channel"
MAIN_BRANCH="main"

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
        git status -s
        echo ""
        read -p "Continue anyway? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    else
        print_success "Working tree is clean"
    fi
    
    # Check if we're on a valid branch
    local current_branch=$(git branch --show-current)
    if [[ -z "$current_branch" ]]; then
        print_error "Not on any branch (detached HEAD state)"
        print_info "Please checkout a branch first"
        exit 1
    fi
    print_success "Current branch: $current_branch"
    
    echo ""
}

check_branch_exists() {
    local branch=$1
    local branch_type=$2  # "local" or "remote"
    
    if [[ "$branch_type" == "local" ]]; then
        if git show-ref --verify --quiet refs/heads/"$branch"; then
            return 0
        else
            return 1
        fi
    else
        if git ls-remote --heads origin "$branch" | grep -q "$branch"; then
            return 0
        else
            return 1
        fi
    fi
}

fetch_latest() {
    print_header "Fetching Latest Changes"
    
    print_info "Fetching from origin..."
    if git fetch origin; then
        print_success "Fetched latest changes from origin"
    else
        print_error "Failed to fetch from origin"
        exit 1
    fi
    echo ""
}

check_branch_status() {
    local branch=$1
    local branch_display=$2
    
    print_header "Checking $branch_display Branch Status"
    
    # Check if branch exists locally
    if ! check_branch_exists "$branch" "local"; then
        print_error "Branch '$branch' does not exist locally"
        print_info "Creating local branch from origin/$branch..."
        
        # Check if it exists on remote
        if check_branch_exists "$branch" "remote"; then
            git checkout -b "$branch" "origin/$branch"
            print_success "Created local branch '$branch' from origin"
        else
            print_error "Branch '$branch' does not exist on remote either"
            print_info "You may need to create it first"
            return 1
        fi
    else
        print_success "Branch '$branch' exists locally"
    fi
    
    # Check if branch exists on remote
    if check_branch_exists "$branch" "remote"; then
        print_success "Branch '$branch' exists on remote"
        
        # Check if local branch is up to date
        git fetch origin "$branch" > /dev/null 2>&1
        local local_commit=$(git rev-parse "$branch")
        local remote_commit=$(git rev-parse "origin/$branch" 2>/dev/null || echo "")
        
        if [[ -n "$remote_commit" ]]; then
            if [[ "$local_commit" == "$remote_commit" ]]; then
                print_success "Local branch is up to date with remote"
            else
                # Check if local is ahead, behind, or diverged
                local base_commit=$(git merge-base "$branch" "origin/$branch" 2>/dev/null || echo "")
                if [[ -n "$base_commit" ]]; then
                    if git merge-base --is-ancestor "$branch" "origin/$branch" 2>/dev/null; then
                        print_warning "Local branch is behind remote"
                        print_info "Local:  $(git log -1 --oneline "$branch")"
                        print_info "Remote: $(git log -1 --oneline "origin/$branch")"
                        echo ""
                        read -p "Pull latest changes? (Y/n): " -n 1 -r
                        echo
                        if [[ ! $REPLY =~ ^[Nn]$ ]]; then
                            git checkout "$branch"
                            git pull origin "$branch"
                            print_success "Pulled latest changes"
                        fi
                    elif git merge-base --is-ancestor "origin/$branch" "$branch" 2>/dev/null; then
                        print_warning "Local branch is ahead of remote"
                        print_info "You may want to push your changes"
                    else
                        print_warning "Local and remote branches have diverged"
                        print_info "You may need to rebase or merge"
                    fi
                fi
            fi
        fi
    else
        print_warning "Branch '$branch' does not exist on remote"
        print_info "This is okay if you're creating it for the first time"
    fi
    
    echo ""
    return 0
}

merge_branch() {
    local source_branch=$1
    local target_branch=$2
    local source_display=$3
    local target_display=$4
    
    print_header "Merging $source_display into $target_display"
    
    # Check if source branch exists
    if ! check_branch_exists "$source_branch" "local"; then
        print_error "Source branch '$source_branch' does not exist locally"
        return 1
    fi
    
    # Check if target branch exists
    if ! check_branch_exists "$target_branch" "local"; then
        print_error "Target branch '$target_branch' does not exist locally"
        return 1
    fi
    
    # Get current branch
    local current_branch=$(git branch --show-current)
    
    # Switch to target branch
    print_info "Switching to $target_branch..."
    if git checkout "$target_branch"; then
        print_success "Switched to $target_branch"
    else
        print_error "Failed to switch to $target_branch"
        return 1
    fi
    
    # Pull latest changes for target branch
    if check_branch_exists "$target_branch" "remote"; then
        print_info "Pulling latest changes for $target_branch..."
        if git pull origin "$target_branch"; then
            print_success "Pulled latest changes"
        else
            print_warning "Failed to pull (may not exist on remote yet)"
        fi
    fi
    
    # Check if merge is needed
    local merge_base=$(git merge-base "$target_branch" "$source_branch" 2>/dev/null || echo "")
    local target_commit=$(git rev-parse "$target_branch")
    local source_commit=$(git rev-parse "$source_branch")
    
    if [[ -n "$merge_base" ]]; then
        if [[ "$target_commit" == "$source_commit" ]]; then
            print_info "Branches are already in sync (no merge needed)"
            echo ""
            return 0
        elif git merge-base --is-ancestor "$source_branch" "$target_branch" 2>/dev/null; then
            print_info "Target branch already contains all commits from source (no merge needed)"
            echo ""
            return 0
        fi
    fi
    
    # Show what will be merged
    print_info "Commits to be merged:"
    if [[ -n "$merge_base" ]]; then
        git log --oneline "$merge_base".."$source_branch" | head -10
        local commit_count=$(git rev-list --count "$merge_base".."$source_branch" 2>/dev/null || echo "?")
        print_info "Total commits: $commit_count"
    else
        print_warning "Cannot determine merge base (branches may have no common history)"
    fi
    echo ""
    
    # Confirm merge
    read -p "Proceed with merge? (Y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Nn]$ ]]; then
        print_warning "Merge cancelled"
        git checkout "$current_branch" 2>/dev/null || true
        return 1
    fi
    
    # Perform merge
    print_info "Merging $source_branch into $target_branch..."
    set +e  # Temporarily disable exit on error to handle merge conflicts
    if git merge "$source_branch" --no-edit; then
        set -e
        print_success "Merge completed successfully"
        
        # Show merge summary
        print_info "Merge summary:"
        git log --oneline -5
        echo ""
        
        # Ask if user wants to push
        read -p "Push $target_branch to origin? (Y/n): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Nn]$ ]]; then
            if git push origin "$target_branch"; then
                print_success "Pushed $target_branch to origin"
            else
                print_error "Failed to push $target_branch"
                print_warning "You may need to push manually: git push origin $target_branch"
            fi
        else
            print_info "Skipped push (you can push manually later)"
        fi
    else
        set -e
        local merge_exit_code=$?
        
        if [[ $merge_exit_code -eq 1 ]]; then
            # Check if there are merge conflicts
            if [[ -n $(git diff --name-only --diff-filter=U) ]]; then
                print_error "Merge conflicts detected!"
                echo ""
                print_info "Conflicted files:"
                git diff --name-only --diff-filter=U
                echo ""
                print_warning "Please resolve conflicts manually:"
                echo "  1. Resolve conflicts in the files listed above"
                echo "  2. Stage resolved files: git add <file>"
                echo "  3. Complete merge: git commit"
                echo "  4. Push when ready: git push origin $target_branch"
                echo ""
                print_info "To abort the merge: git merge --abort"
                return 1
            else
                print_error "Merge failed for unknown reason"
                return 1
            fi
        else
            print_error "Merge failed (exit code: $merge_exit_code)"
            return 1
        fi
    fi
    
    echo ""
    return 0
}

merge_dev_to_early() {
    print_header "Merge: dev → early-release-channel"
    
    fetch_latest
    check_branch_status "$DEV_BRANCH" "Source (dev)"
    check_branch_status "$EARLY_BRANCH" "Target (early-release-channel)"
    
    if merge_branch "$DEV_BRANCH" "$EARLY_BRANCH" "dev" "early-release-channel"; then
        print_success "Successfully merged dev into early-release-channel"
        return 0
    else
        print_error "Failed to merge dev into early-release-channel"
        return 1
    fi
}

merge_early_to_main() {
    print_header "Merge: early-release-channel → main"
    
    fetch_latest
    check_branch_status "$EARLY_BRANCH" "Source (early-release-channel)"
    check_branch_status "$MAIN_BRANCH" "Target (main)"
    
    if merge_branch "$EARLY_BRANCH" "$MAIN_BRANCH" "early-release-channel" "main"; then
        print_success "Successfully merged early-release-channel into main"
        return 0
    else
        print_error "Failed to merge early-release-channel into main"
        return 1
    fi
}

print_summary() {
    print_header "Merge Summary"
    echo -e "${GREEN}Branch merges completed!${NC}"
    echo ""
    echo "Details:"
    echo "  Merged: $DEV_BRANCH → $EARLY_BRANCH"
    echo "  Merged: $EARLY_BRANCH → $MAIN_BRANCH"
    echo ""
    print_info "Next steps:"
    echo "  1. Verify the merges on GitHub"
    echo "  2. Test the merged branches if needed"
    echo "  3. Continue with release process if applicable"
    echo ""
}

# Main execution
main() {
    local merge_mode="full"
    
    # Parse command line arguments
    if [[ "$1" == "--dev-to-early" || "$1" == "-d" ]]; then
        merge_mode="dev-to-early"
    elif [[ "$1" == "--early-to-main" || "$1" == "-e" ]]; then
        merge_mode="early-to-main"
    elif [[ "$1" == "--help" || "$1" == "-h" ]]; then
        echo "Wordclock Branch Merge Script"
        echo ""
        echo "Usage:"
        echo "  ./merge_branches.sh                    Full merge pipeline (dev → early-release-channel → main)"
        echo "  ./merge_branches.sh --dev-to-early     Only merge dev to early-release-channel"
        echo "  ./merge_branches.sh -d                 Short form of --dev-to-early"
        echo "  ./merge_branches.sh --early-to-main    Only merge early-release-channel to main"
        echo "  ./merge_branches.sh -e                 Short form of --early-to-main"
        echo "  ./merge_branches.sh --help             Show this help"
        echo ""
        exit 0
    fi
    
    print_header "Wordclock Branch Merge Pipeline"
    echo "This script will merge branches with proper checks and validations"
    echo ""
    
    # Check prerequisites
    check_prerequisites
    
    local success=true
    local current_branch=$(git branch --show-current)
    
    # Perform merges based on mode
    case "$merge_mode" in
        "dev-to-early")
            if ! merge_dev_to_early; then
                success=false
            fi
            ;;
        "early-to-main")
            if ! merge_early_to_main; then
                success=false
            fi
            ;;
        "full")
            if merge_dev_to_early; then
                echo ""
                if merge_early_to_main; then
                    print_summary
                else
                    success=false
                fi
            else
                success=false
            fi
            ;;
    esac
    
    # Return to original branch if possible
    if [[ -n "$current_branch" ]] && check_branch_exists "$current_branch" "local"; then
        git checkout "$current_branch" 2>/dev/null || true
    fi
    
    if [[ "$success" == true ]]; then
        exit 0
    else
        print_error "One or more merges failed"
        exit 1
    fi
}

# Run main
main "$@"




