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
PRUNE=false

die() { echo "❌ $*" >&2; exit 1; }

usage() {
  sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  cat <<EOF

Options:
  --dry-run   Show what would transfer, change nothing on the server.
  --prune     Also delete files on the server that no longer exist in staging.
              OFF by default: an old artifact may still be the version a clock
              that has been offline for weeks is about to ask for.
  --help      This text.

Environment:
  OTA_STAGE   Local staging tree   (default: \$PROJECT_ROOT/dist/ota-stage)
  OTA_TARGET  rsync destination    (default: vps-ota:/srv/ota/)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY_RUN=true; shift ;;
    --prune)   PRUNE=true;   shift ;;
    --help|-h) usage; exit 0 ;;
    *) die "unknown argument: $1 (try --help)" ;;
  esac
done

command -v rsync > /dev/null || die "rsync not found locally"
[[ -d "$OTA_STAGE" ]] || die "staging tree does not exist: $OTA_STAGE
   Publish into it first, e.g. OTA_ROOT=$OTA_STAGE tools/publish-ota.sh ..."

# Deliberately not -a: ownership and group must be decided by the destination.
# /srv/ota is ota-deploy:www-data and setgid, so files written there inherit
# the group. Copying local uid/gid over would fight that.
RSYNC_OPTS=(-rlptv --chmod=D2775,F664 --out-format='  %n')
if [[ "$DRY_RUN" == true ]]; then RSYNC_OPTS+=(--dry-run); fi
# --delete honours the per-pass filters, so pass 1 never deletes JSON and
# pass 2 never deletes binaries.
if [[ "$PRUNE" == true ]]; then RSYNC_OPTS+=(--delete); fi

PASS_COUNT=0

run_pass() {
  local label="$1"; shift
  echo "── $label"
  local out status=0
  out="$(rsync "${RSYNC_OPTS[@]}" "$@" "$OTA_STAGE/" "$OTA_TARGET" 2>&1)" || status=$?
  printf '%s\n' "$out"
  [[ $status -eq 0 ]] || die "rsync failed ($label, exit $status)"
  # Lines the out-format produced, minus directory entries.
  PASS_COUNT="$(printf '%s\n' "$out" | sed -n 's/^  //p' | grep -v '/$' | grep -c . || true)"
  echo "   → $PASS_COUNT file(s)"
  echo
}

echo "=== OTA sync ==="
echo "  Staging : $OTA_STAGE"
echo "  Target  : $OTA_TARGET"
echo "  Mode    : $($DRY_RUN && echo 'dry-run (nothing is written)' || echo 'live')"
echo "  Prune   : $($PRUNE && echo 'yes (--delete)' || echo 'no (stale artifacts kept)')"
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
