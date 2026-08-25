#!/usr/bin/env bash
# Verify the static-asset cache validators that serveFile() attaches
# (Cache-Control + ETag + If-None-Match -> 304). See ROADMAP.md, "Static assets
# are served with no cache headers".
#
#   ./tools/check-cache-headers.sh [host] [path]
#
# Defaults to wordclock.local and /dashboard.html. Pass an IP if mDNS is
# unreliable on your machine, the device serves the same thing either way.
#
# Run this against the device rather than a browser: a browser that cached a
# page *before* this firmware shipped holds a copy with no validators at all,
# so it may reuse it without ever asking and never see the new headers. curl
# has no cache and always asks.
set -uo pipefail

HOST="${1:-wordclock.local}"
PATHQ="${2:-/dashboard.html}"
URL="http://${HOST}${PATHQ}"

pass=0; fail=0
ok()   { echo "  PASS  $*"; pass=$((pass+1)); }
bad()  { echo "  FAIL  $*"; fail=$((fail+1)); }

echo "==> ${URL}"

hdrs="$(curl -s -D - -o /dev/null -m 10 "$URL" 2>/dev/null)" || {
  echo "  device unreachable at ${HOST}, pass an IP as the first argument"; exit 1; }

status="$(printf '%s' "$hdrs" | head -1 | tr -d '\r')"
etag="$(printf '%s' "$hdrs" | grep -i '^ETag:' | head -1 | sed 's/^[Ee][Tt][Aa][Gg]: *//' | tr -d '\r')"
cc="$(printf '%s' "$hdrs" | grep -i '^Cache-Control:' | head -1 | sed 's/^[^:]*: *//' | tr -d '\r')"

echo "==> Unconditional GET"
echo "    ${status}"
case "$status" in *200*) ok "200 OK" ;; *) bad "expected 200, got: ${status}" ;; esac
[ -n "$etag" ] && ok "ETag present: ${etag}" || bad "no ETag header (old firmware, or fs.bin not installed)"
[ "$cc" = "no-cache" ] && ok "Cache-Control: no-cache" || bad "Cache-Control was '${cc:-<absent>}', expected 'no-cache'"

if [ -z "$etag" ]; then
  echo; echo "  ${pass} passed, ${fail} failed, stopping, nothing to revalidate against."; exit 1
fi

echo "==> Conditional GET (If-None-Match)"
# Ask curl for the byte count rather than measuring a temp file: curl opens its
# -o target lazily, on the first body byte, so a bodyless 304 leaves no file at
# all and `wc -c` reports an error instead of the zero we are hoping for.
cond="$(curl -s -D - -o /dev/null -w '\nWC_SIZE:%{size_download}' -m 10 -H "If-None-Match: ${etag}" "$URL" 2>/dev/null)"
cstatus="$(printf '%s' "$cond" | head -1 | tr -d '\r')"
bodysize="$(printf '%s' "$cond" | sed -n 's/^WC_SIZE:\([0-9]*\)$/\1/p' | tail -1)"
bodysize="${bodysize:-unknown}"
echo "    ${cstatus}  (body: ${bodysize} bytes)"
case "$cstatus" in
  *304*) ok "304 Not Modified" ;;
  *) bad "expected 304, got: ${cstatus}, check that If-None-Match is in collectHeaders()" ;;
esac
[ "$bodysize" = "0" ] && ok "no body on 304" || bad "304 carried ${bodysize} bytes of body"

echo "==> Stale ETag must NOT be honoured"
stale="$(curl -s -o /dev/null -w '%{http_code}' -m 10 -H 'If-None-Match: "definitely-not-the-tag"' "$URL" 2>/dev/null)"
[ "$stale" = "200" ] && ok "stale tag returns 200 (full body)" || bad "stale tag returned ${stale}, expected 200"

echo "==> 404 must carry no validators"
n404="$(curl -s -D - -o /dev/null -m 10 "http://${HOST}/no-such-file-$$.html" 2>/dev/null)"
if printf '%s' "$n404" | grep -qi '^ETag:'; then
  bad "404 carried an ETag"
else
  ok "404 has no ETag"
fi

echo
echo "  ${pass} passed, ${fail} failed"
[ "$fail" -eq 0 ] || exit 1
