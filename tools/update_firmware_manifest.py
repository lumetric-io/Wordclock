#!/usr/bin/env python3
import json
import sys
import os
from datetime import datetime, timezone
import hashlib
import urllib.request

HTML_FILES = [
    "admin.html",
    "changepw.html",
    "dashboard.html",
    "logs.html",
    "mqtt.html",
    "setup.html",
    "update.html",
]

def version_without_product(version, product):
    product_subtype = product
    if product.startswith("wordclock-"):
        product_subtype = product[len("wordclock-"):]
    if product_subtype and version.startswith(f"{product_subtype}-"):
        return version[len(product_subtype) + 1:]
    return version

def calculate_sha256_from_url(url):
    try:
        with urllib.request.urlopen(url) as response:
            data = response.read()
            return hashlib.sha256(data).hexdigest()
    except Exception as e:
        print(f"Warning: Could not calculate SHA256 for {url}: {e}", file=sys.stderr)
        return ""

def load_manifest(src_path, version, product):
    """Load existing manifest or create a default skeleton."""
    if os.path.exists(src_path):
        with open(src_path, "r", encoding="utf-8") as f:
            return json.load(f)

    version_suffix = version_without_product(version, product)

    # If source doesn't exist, create new structure (but this will only have one channel!)
    # In practice for you: source *should* exist.
    return {
        "version": version,
        "ui_version": version,
        "released": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "firmware": f"https://github.com/lumetric-io/Wordclock/releases/download/{version}/{product}-{version_suffix}.bin",
        "files": [],
        "channels": {},
    }

def write_backup(src_path, version):
    """Create a versioned backup next to the source manifest."""
    if not os.path.exists(src_path):
        return None

    src_dir = os.path.dirname(src_path)
    base = os.path.basename(src_path)

    # firmware.json -> firmware.json.vX.Y.Z.json (same style as your old script)
    backup_path = os.path.join(src_dir, f"{base}.v{version}.json")

    try:
        with open(src_path, "r", encoding="utf-8") as src:
            with open(backup_path, "w", encoding="utf-8") as dst:
                dst.write(src.read())
        return backup_path
    except Exception as e:
        print(f"Warning: Could not create backup: {e}", file=sys.stderr)
        return None

def update_manifest(version, channel, product, src_path, dst_path, release_notes=""):
    released = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    version_suffix = version_without_product(version, product)

    # Load full existing manifest (this is what preserves other channels!)
    manifest = load_manifest(src_path, version, product)

    # Build files list
    files = []
    for html_file in HTML_FILES:
        url = f"https://raw.githubusercontent.com/lumetric-io/Wordclock/{version}/data/{html_file}"
        sha256 = calculate_sha256_from_url(url)
        files.append({"path": f"/{html_file}", "url": url, "sha256": sha256})

    # Ensure channels exists
    if "channels" not in manifest or not isinstance(manifest["channels"], dict):
        manifest["channels"] = {}

    # Update top-level only for stable
    if channel == "stable":
        manifest["version"] = version
        manifest["ui_version"] = version
        manifest["released"] = released
        manifest["firmware"] = f"https://github.com/lumetric-io/Wordclock/releases/download/{version}/{product}-{version_suffix}.bin"
        manifest["files"] = files

    # Update ONLY the requested channel (preserves others)
    manifest["channels"][channel] = {
        "version": version,
        "ui_version": version,
        "released": released,
        "firmware": {
            "version": version,
            "url": f"https://github.com/lumetric-io/Wordclock/releases/download/{version}/{product}-{version_suffix}.bin",
        },
        "files": files,
        "release_notes": release_notes.strip() if release_notes else "",
    }

    # Safety check: don’t allow accidental wipe of channels
    # (If source existed and had multiple channels, we should still have them.)
    if os.path.exists(src_path):
        # basic sanity: channels should be a dict and not empty
        if not isinstance(manifest.get("channels"), dict) or len(manifest["channels"]) == 0:
            raise RuntimeError("Refusing to write: channels would be empty.")

    # Write output (to temp file path)
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

def main():
    # New calling convention:
    #   <version> <channel> <product> <src> <dst> [release_notes]
    if len(sys.argv) < 6:
        print("Usage: update_firmware_manifest.py <version> <channel> <product> <src_manifest> <dst_manifest> [release_notes]", file=sys.stderr)
        sys.exit(1)

    version = sys.argv[1]
    channel = sys.argv[2]
    product = sys.argv[3]
    src_path = sys.argv[4]
    dst_path = sys.argv[5]
    release_notes = sys.argv[6] if len(sys.argv) > 6 else ""

    if not product:
        print("Error: Product is required (e.g., wordclock-legacy)", file=sys.stderr)
        sys.exit(1)

    if channel not in ["stable", "early", "develop"]:
        print(f"Error: Invalid channel '{channel}'. Must be: stable, early, or develop", file=sys.stderr)
        sys.exit(1)

    # Backup before writing (backup the source, not the destination)
    backup_path = write_backup(src_path, version)
    if backup_path:
        print(f"Created backup: {backup_path}")

    try:
        update_manifest(version, channel, product, src_path, dst_path, release_notes)
        print(f"Prepared updated manifest: {dst_path}")
        print(f"  Source:  {src_path}")
        print(f"  Version: {version}")
        print(f"  Channel: {channel}")
        sys.exit(0)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
