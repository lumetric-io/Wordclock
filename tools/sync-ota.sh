#!/usr/bin/env bash
# Push a locally staged OTA tree to the OTA server.
#
#   tools/sync-ota.sh [--dry-run] [--prune] [--help]
#
# Why this exists: publish-ota.sh writes a plain directory tree, and until now
# that tree had to be written straight into the live webroot, which meant the
# publisher could only run on the OTA host itself. Staging locally and syncing
# afterwards splits those two jobs, so a release can be cut anywhere.
#
# The sync runs in TWO PASSES and the order is the whole point:
#
#   1. everything except *.json  -- the firmware.bin / fs.bin payloads
#   2. only *.json               -- the manifests and channel pointers
#
# A clock polls its channel JSON and immediately fetches whatever binary that
# JSON names. If the JSON landed first, a clock polling mid-upload would be
# handed a URL for a binary that is still half written. Sending the JSON last
# means every manifest on the server always points at something that is
# already complete.
#
# --seed pulls in the other direction: it copies the *.json already on the
# host into staging, without any binaries. publish-ota.sh reads the live
# channel JSON to decide two things -- which firmware manifest an fs-only
# publish should keep pointing at (publish-ota.sh:608,692,791), and whether
# the filesystem actually changed since the last release (:609-612). Against
# an empty staging tree it finds neither: an --ui-only release aborts, and a
# normal release silently republishes the filesystem even when it is
# identical, costing every clock a needless write. Seeding first restores
# both. JSON only, so it stays a few kB.
#
# Known limitation -- OTA_TARGET must go over the WireGuard tunnel:
#   The ota-deploy key is pinned in authorized_keys with
#   restrict,from="192.168.30.2". Over the public route (ron@ssh.lumetric.nl)
#   this container arrives with a different source address, so the key is
#   refused. Falling back to the public host therefore needs a second key, or
#   an extra from= entry for the public egress address -- decided and applied
#   on the server, not worked around here.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

OTA_STAGE="${OTA_STAGE:-$PROJECT_ROOT/dist/ota-stage}"
OTA_TARGET="${OTA_TARGET:-vps-ota:/srv/ota/}"

DRY_RUN=false
SEED=false
SEED_PRODUCT=""

die() { echo "❌ $*" >&2; exit 1; }

usage() {
  sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  cat <<EOF

Options:
  --dry-run   Show what would transfer, change nothing on the server.
              This never deletes anything on the host. See --prune.
  --seed      Reverse direction: copy the *.json already on the host into
              staging and stop. Run this before publishing so publish-ota.sh
              can see what the channel currently ships. Never deletes.
  --product <id>
              Narrow --seed to one product. Without it the whole tree's json
              is pulled, which also widens what a later push could overwrite.
  --prune     Removed. Refuses with an explanation; pruning needs a host-side
              job that knows which artifacts the channels still reference.
  --help      This text.

Environment:
  OTA_STAGE   Local staging tree   (default: \$PROJECT_ROOT/dist/ota-stage)
  OTA_TARGET  rsync destination    (default: vps-ota:/srv/ota/)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY_RUN=true; shift ;;
    --prune)
      die "--prune was removed; it cannot be made safe here.
   rsync --delete removes whatever is not in staging, and staging is never a
   full mirror: a seed pulls *.json only, so pruning deletes the firmware.bin
   and fs.bin the live channel still points at. Verified 2026-09-02 against a
   scratch target: with a fully seeded tree the JSON survived and the binaries
   for the current stable did not.
   Pruning belongs on the host, as a job that reads the channel JSONs and
   removes only artifact directories nothing references any more." ;;
    --seed)    SEED=true;    shift ;;
    --product) SEED_PRODUCT="${2:-}"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) die "unknown argument: $1 (try --help)" ;;
  esac
done

command -v rsync > /dev/null || die "rsync not found locally"

if [[ "$SEED" != true && -n "$SEED_PRODUCT" ]]; then
  die "--product only applies to --seed"
fi

if [[ "$SEED" == true ]]; then
  # Pull, not push. No --chmod: these land in a local working tree, where the
  # caller's umask is the right answer. No --delete: seeding must never remove
  # anything a publish has already staged.
  SEED_OPTS=(-rlptv --include='*/' --include='*.json' --exclude='*' --out-format='  %n')
  if [[ "$DRY_RUN" == true ]]; then SEED_OPTS+=(--dry-run); fi

  if [[ -n "$SEED_PRODUCT" ]]; then
    SEED_SRC="${OTA_TARGET%/}/$SEED_PRODUCT/"
    SEED_DST="$OTA_STAGE/$SEED_PRODUCT/"
  else
    SEED_SRC="${OTA_TARGET%/}/"
    SEED_DST="$OTA_STAGE/"
  fi
  mkdir -p "$SEED_DST"

  echo "=== OTA seed ==="
  echo "  Source  : $SEED_SRC"
  echo "  Staging : $SEED_DST"
  echo "  Scope   : ${SEED_PRODUCT:-<whole tree>}, *.json only"
  echo

  seed_out=""; seed_status=0
  seed_out="$(rsync "${SEED_OPTS[@]}" "$SEED_SRC" "$SEED_DST" 2>&1)" || seed_status=$?
  printf '%s\n' "$seed_out"

  if [[ $seed_status -ne 0 ]]; then
    # A product that has never been published has no directory on the host.
    # That is a normal first release, not a reason to stop a pipeline.
    echo
    echo "⚠️  seed incomplete (rsync exit $seed_status)"
    echo "   Nothing was pulled. That is expected for a product that has never"
    echo "   been published; an fs-only release will still fail for it, because"
    echo "   there is no previous firmware manifest to keep pointing at."
    exit 0
  fi

  seed_n="$(printf '%s\n' "$seed_out" | sed -n 's/^  //p' | grep -v '/$' | grep -c . || true)"
  echo "=== Summary ==="
  echo "  json seeded : $seed_n"
  if [[ "$DRY_RUN" == true ]]; then
    echo "  (dry-run: nothing was actually written)"
  fi
  echo "✅ OTA seed complete"
  exit 0
fi

[[ -d "$OTA_STAGE" ]] || die "staging tree does not exist: $OTA_STAGE
   Publish into it first, e.g. OTA_ROOT=$OTA_STAGE tools/publish-ota.sh ..."

# Deliberately not -a: ownership and group must be decided by the destination.
# /srv/ota is ota-deploy:www-data and setgid, so files written there inherit
# the group. Copying local uid/gid over would fight that.
RSYNC_OPTS=(-rlptv --chmod=D2775,F664 --out-format='  %n')
if [[ "$DRY_RUN" == true ]]; then RSYNC_OPTS+=(--dry-run); fi

PASS_COUNT=0

run_pass() {
  local label="$1"; shift
  echo "── $label"
  local out status=0
  out="$(rsync "${RSYNC_OPTS[@]}" "$@" "$PUSH_SRC" "$PUSH_DST" 2>&1)" || status=$?
  printf '%s\n' "$out"
  [[ $status -eq 0 ]] || die "rsync failed ($label, exit $status)"
  # Lines the out-format produced, minus directory entries.
  PASS_COUNT="$(printf '%s\n' "$out" | sed -n 's/^  //p' | grep -v '/$' | grep -c . || true)"
  echo "   → $PASS_COUNT file(s)"
  echo
}

PUSH_SRC="$OTA_STAGE/"
PUSH_DST="$OTA_TARGET"

echo "=== OTA sync ==="
echo "  Staging : $PUSH_SRC"
echo "  Target  : $PUSH_DST"
echo "  Mode    : $($DRY_RUN && echo 'dry-run (nothing is written)' || echo 'live')"
echo

run_pass "pass 1/2  binaries (everything except *.json)" --exclude='*.json'
BIN_COUNT=$PASS_COUNT

run_pass "pass 2/2  manifests and channels (*.json only)" \
  --include='*/' --include='*.json' --exclude='*'
JSON_COUNT=$PASS_COUNT

echo "=== Summary ==="
echo "  binaries transferred : $BIN_COUNT"
echo "  json transferred     : $JSON_COUNT"
echo "  target               : $OTA_TARGET"
if [[ "$DRY_RUN" == true ]]; then
  echo "  (dry-run: nothing was actually written)"
fi
echo "✅ OTA sync complete"
