#!/usr/bin/env bash
#
# pin-legacy-early-fs.sh - hold every legacy early channel's fs at the fleet baseline.
#
# Why: the 26.8.25 rc builds (v2 dashboard) were published to the legacy early
# channels on 2026-08-25, but every firmware generation in the field still
# applies fs updates through the stale-mount bug (fixed on legacy-main, not yet
# shipped). A clock that picks up an rc now would pull the new fs through the
# buggy path, which is exactly how the .205 clock wedged mid-update.
#
# What: repoints each early channel's fs_manifest_url at the fs.json of the
# version that product's fleet already runs (its de-facto stable). The firmware
# half stays on the current rc target. Clocks compare fs versions by equality,
# so they take the firmware and skip the fs: a firmware-only hop, the safe kind
# on every generation. Once the fleet reports fix-carrying firmware, publish
# normally (or run --undo) and the fs half flows again through the fixed code.
#
# Caveat while pinned: a freshly serial-flashed clock has no /.fs_image_version
# marker, so its first auto-check re-pulls the (older) pinned fs over its new
# one. Lift the pin before provisioning new stock on the early channel.
#
# Usage:
#   tools/pin-legacy-early-fs.sh            # dry-run, prints the plan
#   tools/pin-legacy-early-fs.sh --go       # apply (needs sudo rights on /srv/ota)
#   tools/pin-legacy-early-fs.sh --undo     # dry-run of the reverse
#   tools/pin-legacy-early-fs.sh --undo --go
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUBLISH="$HERE/publish-ota.sh"

GO=false
UNDO=false
for a in "$@"; do
  case "$a" in
    --go)   GO=true ;;
    --undo) UNDO=true ;;
    *) echo "unknown argument: $a (--go, --undo)" >&2; exit 1 ;;
  esac
done

# product | early firmware target (unchanged) | fs baseline the fleet runs
# nl-v3 is absent on purpose: its early channel still targets 26.2.6-rc.15,
# whose fs.json is its own baseline. Nothing to pin there.
PINS=(
  "wordclock-legacy|legacy-26.2.6-rc.15|legacy-26.2.8"
  "wordclock-legacy-nl-v4|legacy-nl-v4-26.8.25-rc.3|legacy-nl-v4-26.4.0"
  "wordclock-legacy-nl-50x50-v3|legacy-nl-50x50-v3-26.8.25-rc.1|legacy-nl-50x50-v3-26.8.24-rc.2"
  "wordclock-legacy-nl-50x50-v1|legacy-nl-50x50-v1-26.8.25-rc.2|legacy-nl-50x50-v1-26.2.6-rc.15"
  "wordclock-legacy-nl-50x50-v2|legacy-nl-50x50-v2-26.8.25-rc.1|legacy-nl-50x50-v2-26.2.6-rc.15"
  "wordclock-legacy-nl-v1|legacy-nl-v1-26.8.25-rc.1|legacy-nl-v1-26.2.6-rc.15"
  "wordclock-legacy-nl-v2|legacy-nl-v2-26.8.25-rc.1|legacy-nl-v2-26.2.6-rc.15"
)

DRY_FLAG="--dry-run"
if [[ "$GO" == true ]]; then DRY_FLAG=""; fi

for pin in "${PINS[@]}"; do
  IFS='|' read -r product target baseline <<<"$pin"
  echo
  echo "########## $product"
  if [[ "$UNDO" == true ]]; then
    "$PUBLISH" -p "$product" -c early --repoint "$target" -y $DRY_FLAG
  else
    "$PUBLISH" -p "$product" -c early --repoint "$target" --fs-from "$baseline" -y $DRY_FLAG
  fi
done

echo
if [[ "$GO" == true ]]; then
  echo "All legacy early channels processed. Spot check:"
  echo "  curl -s http://ota2.chronolett.com/wordclock-legacy-nl-50x50-v3/channels/early.json"
else
  echo "Dry run only. Re-run with --go to apply."
fi
