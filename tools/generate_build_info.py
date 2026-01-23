Import("env")

from datetime import datetime, timezone
import os
import subprocess


def get_git_sha():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return "unknown"


def get_git_branch():
    """Get current git branch name, or 'unknown' if not available."""
    try:
        # Try to get branch name from git
        branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
        # Remove 'heads/' prefix if present (can happen in CI)
        if branch.startswith("heads/"):
            branch = branch[6:]
        return branch
    except Exception:
        return "unknown"


def write_build_info(*args, **kwargs):
    sha = get_git_sha()
    branch = get_git_branch()
    build_time = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    env_name = env.get("PIOENV", "unknown")
    
    # Enable debug logging only when explicitly requested.
    enable_debug = "1" if os.getenv("ENABLE_DEBUG_LOGGING") == "1" else "0"

    content = f"""#pragma once

#define BUILD_GIT_SHA "{sha}"
#define BUILD_GIT_BRANCH "{branch}"
#define BUILD_TIME_UTC "{build_time}"
#define BUILD_ENV_NAME "{env_name}"
#define ENABLE_DEBUG_LOGGING {enable_debug}
"""
    include_dir = env.subst("$PROJECT_INCLUDE_DIR")
    os.makedirs(include_dir, exist_ok=True)
    path = os.path.join(include_dir, "build_info.h")

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

    debug_status = "ENABLED" if enable_debug == "1" else "DISABLED"
    print(f"[buildinfo] SHA={sha} branch={branch} env={env_name} time={build_time} debug={debug_status}")


env.AddPreAction("buildprog", write_build_info)
env.AddPreAction("buildfs", write_build_info)
