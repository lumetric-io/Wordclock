#!/usr/bin/env bash
# Validate the logging subsystem against a running clock over HTTP.
#
#   tools/test-log-features.sh [--host <ip>] [--destructive] [--db]
#
# Why this exists: every log setting is reported back to the fleet in the
# heartbeat (logLevel, logRetentionDays, logDeleteOnBoot) and three of the five
# device commands do nothing but move them. The portal judges a command
# complete from what the next beat reports, so a setting that is stored and
# reported but does not actually take is invisible to the whole chain: the
# command closes, the dashboard goes green, and the log the operator asked for
# is not there. Reading the setting back therefore proves nothing. Every check
# below looks at the effect instead.
#
# The three features and what "effect" means for each:
#
#   level filtering   a sub-threshold line must be absent from the RAM ring
#                     buffer and from the file, not merely reported filtered.
#   file sink         a line that passed the filter must reach /logs and grow
#                     the file. This is the check that caught the stall on
#                     2026-08-22 (see FINDINGS at the bottom).
#   delete_on_boot    survives, or does not survive, an actual restart.
#   retention         see the SKIP in phase 6: not reachable over HTTP.
#
# Non-destructive by default: it changes the log level and the two log settings
# and puts all three back, including on Ctrl-C. --destructive additionally
# restarts the clock twice, which costs the current log files.
#
# Exit code is 0 only if no check failed. A SKIP is not a pass and says so.

set -uo pipefail

HOST="192.168.20.131"
DESTRUCTIVE=0
CHECK_DB=0
PG_CONTAINER="lumetric-postgres"
DB_NAME="lumetric_registry"
DB_ROLE="lumetric"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host)        HOST="$2"; shift 2 ;;
    --destructive) DESTRUCTIVE=1; shift ;;
    --db)          CHECK_DB=1; shift ;;
    -h|--help)     sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

BASE="http://${HOST}"
PASS=0; FAIL=0; SKIP=0

pass() { echo "    PASS  $*"; PASS=$((PASS+1)); }
fail() { echo "    FAIL  $*"; FAIL=$((FAIL+1)); }
skip() { echo "    SKIP  $*"; SKIP=$((SKIP+1)); }
info() { echo "          $*"; }

get()  { curl -s --max-time 15 "${BASE}$1"; }
post() { curl -s --max-time 15 -X POST "${BASE}$1"; }

# The clock answers /api/logs/summary with the sum over /logs, deduplicated per
# date. Using the total rather than one file's size keeps the growth checks
# correct across a midnight rollover mid-run.
log_bytes() { get /api/logs/summary | sed -n 's/.*"total_bytes":\([0-9]*\).*/\1/p'; }
ram_log()   { curl -s --max-time 25 "${BASE}/log"; }
settings()  { get /api/logs/settings; }
json_num()  { sed -n "s/.*\"$2\":\([0-9]*\).*/\1/p" <<<"$1"; }
json_bool() { sed -n "s/.*\"$2\":\(true\|false\).*/\1/p" <<<"$1"; }

level_name() { case "$1" in 0) echo DEBUG ;; 1) echo INFO ;; 2) echo WARN ;; 3) echo ERROR ;; *) echo "?" ;; esac; }

# A probe that is guaranteed to attempt exactly one INFO write and to change
# nothing else: /setLogLevel logs "Log level changed to: X" *after* applying X,
# so setting the level to what it already is is a no-op that still emits a
# line. That property is also what makes phase 4 work.
probe() { get "/setLogLevel?level=$1" > /dev/null; }

# Poll rather than sleep a fixed time: a line is flushed to the file the moment
# it ends in a newline, but the request itself has to land first.
wait_for_growth() { # wait_for_growth <baseline bytes> <seconds>
  local base="$1" deadline=$((SECONDS + $2)) now
  while (( SECONDS < deadline )); do
    now="$(log_bytes)"
    [[ -n "$now" && "$now" -gt "$base" ]] && { echo "$now"; return 0; }
    sleep 1
  done
  echo "${now:-$base}"; return 1
}

echo "==> Target ${BASE}"
IDENT="$(get /api/firmware/identity)"
[[ -n "${IDENT}" ]] || { echo "    FAIL  no answer from ${BASE}"; exit 1; }
info "${IDENT}"

BASE_SET="$(settings)"
[[ -n "${BASE_SET}" ]] || { echo "    FAIL  /api/logs/settings did not answer"; exit 1; }
BASE_RETENTION="$(json_num "${BASE_SET}" retention_days)"
BASE_DELBOOT="$(json_bool "${BASE_SET}" delete_on_boot)"
BASE_LEVEL="$(json_num "${BASE_SET}" level)"
info "baseline: retention_days=${BASE_RETENTION} delete_on_boot=${BASE_DELBOOT} level=${BASE_LEVEL} ($(level_name "${BASE_LEVEL}"))"

restore() {
  echo "==> Restoring the baseline"
  get "/setLogLevel?level=$(level_name "${BASE_LEVEL}")" > /dev/null
  post "/api/logs/settings?retention_days=${BASE_RETENTION}&delete_on_boot=${BASE_DELBOOT}" > /dev/null
  local now; now="$(settings)"
  if [[ "$(json_num "$now" retention_days)" == "${BASE_RETENTION}" \
     && "$(json_bool "$now" delete_on_boot)" == "${BASE_DELBOOT}" \
     && "$(json_num "$now" level)" == "${BASE_LEVEL}" ]]; then
    info "restored: ${now}"
  else
    echo "    WARN  could not restore the baseline. Now: ${now}, was: ${BASE_SET}"
  fi
}
trap restore EXIT

# ---------------------------------------------------------------------------
echo "==> Phase 1: does what the fleet sees match what the device holds?"
# Runs first and reads only, so it compares the last real heartbeat against the
# settings as they were before this script touched anything.
if [[ "${CHECK_DB}" == "1" ]]; then
  DEV="$(get /api/device/info | sed -n 's/.*"device_id":"\([^"]*\)".*/\1/p')"
  if [[ -z "${DEV}" ]]; then
    skip "no device_id from /api/device/info"
  else
    ROW="$(docker exec "${PG_CONTAINER}" psql -U "${DB_ROLE}" -d "${DB_NAME}" -tAc \
      "SELECT log_level, log_retention_days, log_delete_on_boot,
              EXTRACT(EPOCH FROM (NOW() - created_at))::int
       FROM clock_heartbeats WHERE device_id = '${DEV}'
       ORDER BY created_at DESC LIMIT 1;" 2>&1)"
    if [[ "${ROW}" != *"|"* ]]; then
      skip "no heartbeat row for ${DEV}: ${ROW}"
    else
      IFS='|' read -r HB_LVL HB_RET HB_DEL HB_AGE <<<"${ROW}"
      info "last beat ${HB_AGE}s ago: logLevel=${HB_LVL} retention=${HB_RET} deleteOnBoot=${HB_DEL}"
      # A beat older than the heartbeat interval cannot be expected to agree.
      if (( HB_AGE > 4200 )); then
        skip "last beat is ${HB_AGE}s old, too stale to compare"
      else
        [[ "${HB_LVL}" == "$(level_name "${BASE_LEVEL}" | tr 'A-Z' 'a-z')" ]] \
          && pass "heartbeat logLevel agrees with the device" \
          || fail "heartbeat logLevel=${HB_LVL}, device says $(level_name "${BASE_LEVEL}")"
        [[ "${HB_RET}" == "${BASE_RETENTION}" ]] \
          && pass "heartbeat logRetentionDays agrees with the device" \
          || fail "heartbeat logRetentionDays=${HB_RET}, device says ${BASE_RETENTION}"
        [[ "${HB_DEL}" == "t" && "${BASE_DELBOOT}" == "true" ]] || \
        [[ "${HB_DEL}" == "f" && "${BASE_DELBOOT}" == "false" ]] \
          && pass "heartbeat logDeleteOnBoot agrees with the device" \
          || fail "heartbeat logDeleteOnBoot=${HB_DEL}, device says ${BASE_DELBOOT}"
      fi
    fi
  fi
else
  skip "heartbeat cross-check not requested (pass --db on the portal host)"
fi

# ---------------------------------------------------------------------------
echo "==> Phase 2: the file sink is alive"
# The load-bearing check. Everything downstream of it (a raised level to
# reproduce a fault, delete-on-boot to keep the evidence, retention to keep it
# long enough) is worthless on a clock that has stopped writing, and nothing in
# the heartbeat would show it: the settings keep reporting exactly as intended.
B0="$(log_bytes)"
if [[ -z "${B0}" ]]; then
  fail "/api/logs/summary returned no total_bytes"
  SINK_ALIVE=0
else
  info "/logs holds ${B0} bytes"
  probe "$(level_name "${BASE_LEVEL}")"
  B1="$(wait_for_growth "${B0}" 12)"
  if [[ "${B1}" -gt "${B0}" ]]; then
    pass "a logged line reached the file (${B0} -> ${B1} bytes)"
    SINK_ALIVE=1
  else
    fail "the file sink accepted a line but /logs did not grow (still ${B1} bytes)"
    info "the same line in the RAM buffer, for contrast:"
    ram_log | grep -a "Log level changed to" | tail -1 | sed 's/^/          /'
    info "fileSinkEnabled has latched off, or the open file handle is dead."
    info "log.cpp sets fileSinkEnabled=false on a failed open and never retries,"
    info "so only a reboot or a /logs clear can bring the sink back."
    SINK_ALIVE=0
  fi
fi

# ---------------------------------------------------------------------------
echo "==> Phase 3: settings round-trip and the clamp"
post "/api/logs/settings?retention_days=5" > /dev/null
S="$(settings)"
[[ "$(json_num "$S" retention_days)" == "5" ]] \
  && pass "retention_days=5 stored and read back" \
  || fail "retention_days=5 read back as $(json_num "$S" retention_days)"

post "/api/logs/settings?retention_days=30" > /dev/null
S="$(settings)"
if [[ "$(json_num "$S" retention_days)" == "10" ]]; then
  pass "retention_days=30 clamped to 10 by setLogRetentionDays()"
  info "note: the clamp is silent. The HTTP caller cannot tell 30 was refused."
  info "That is fine here but not over the fleet downlink, where a clamped"
  info "value leaves the portal waiting for a report that never arrives. The"
  info "portal side refuses 1..10 in issue_clock_command() for that reason"
  info "(portal/sql/018); this endpoint is the local half and does not."
else
  fail "retention_days=30 read back as $(json_num "$S" retention_days), expected the 1..10 clamp"
fi

post "/api/logs/settings?delete_on_boot=true" > /dev/null
[[ "$(json_bool "$(settings)" delete_on_boot)" == "true" ]] \
  && pass "delete_on_boot=true stored and read back" || fail "delete_on_boot=true did not stick"
post "/api/logs/settings?delete_on_boot=false" > /dev/null
[[ "$(json_bool "$(settings)" delete_on_boot)" == "false" ]] \
  && pass "delete_on_boot=false stored and read back" || fail "delete_on_boot=false did not stick"

# ---------------------------------------------------------------------------
echo "==> Phase 4: level filtering, by absence and presence"
# /setLogLevel applies the level and only then logs at INFO, so the emitted
# line is judged against the level just set. Setting WARN or ERROR must
# therefore leave no trace; setting DEBUG or INFO must leave one. No other
# endpoint is needed, and every step is reversible.
filter_case() { # filter_case <level to set> <expect: yes|no>
  local lvl="$1" expect="$2" before after
  before="$(ram_log | grep -ac "Log level changed to: ${lvl}")"
  get "/setLogLevel?level=${lvl}" > /dev/null
  sleep 2
  after="$(ram_log | grep -ac "Log level changed to: ${lvl}")"
  if [[ "${expect}" == "no" ]]; then
    (( after == before )) \
      && pass "at level ${lvl}, the INFO line was filtered out (RAM buffer unchanged)" \
      || fail "at level ${lvl}, an INFO line was still recorded (${before} -> ${after})"
  else
    (( after > before )) \
      && pass "at level ${lvl}, the INFO line was recorded (${before} -> ${after})" \
      || fail "at level ${lvl}, the INFO line never appeared (${before} -> ${after})"
  fi
}
filter_case ERROR no
filter_case WARN  no
filter_case INFO  yes
filter_case DEBUG yes

# The RAM buffer and the file are fed from the same place in log(), one after
# the other, but only the file can silently drop. Check the filter held there
# too, which is the copy an operator actually downloads.
if [[ "${SINK_ALIVE}" == "1" ]]; then
  get "/setLogLevel?level=ERROR" > /dev/null
  sleep 1
  F0="$(log_bytes)"
  probe ERROR                       # emits nothing: INFO is below ERROR
  sleep 4
  F1="$(log_bytes)"
  [[ "${F1}" == "${F0}" ]] \
    && pass "at level ERROR nothing reached the file either (${F0} bytes, unchanged)" \
    || fail "at level ERROR the file still grew (${F0} -> ${F1})"
  get "/setLogLevel?level=DEBUG" > /dev/null
  F2="$(wait_for_growth "${F1}" 10)"
  [[ "${F2}" -gt "${F1}" ]] \
    && pass "back at DEBUG the file grew again (${F1} -> ${F2})" \
    || fail "back at DEBUG the file did not grow (${F1} -> ${F2})"
else
  skip "file-level filter check: the sink is not writing (phase 2)"
  skip "file resumes after lowering the level: the sink is not writing (phase 2)"
fi

# ---------------------------------------------------------------------------
echo "==> Phase 5: delete_on_boot across a real restart"
if [[ "${DESTRUCTIVE}" != "1" ]]; then
  skip "delete_on_boot=false preserves /logs across a reboot (needs --destructive)"
  skip "delete_on_boot=true wipes /logs across a reboot (needs --destructive)"
  info "both cost a restart and the current log files. Pass --destructive to run them."
else
  reboot_and_wait() {
    get /restart > /dev/null 2>&1 || true
    sleep 20
    local deadline=$((SECONDS + 120))
    while (( SECONDS < deadline )); do
      [[ -n "$(get /api/firmware/identity)" ]] && return 0
      sleep 3
    done
    return 1
  }

  post "/api/logs/settings?delete_on_boot=false" > /dev/null
  probe DEBUG; sleep 2
  P0="$(log_bytes)"
  info "delete_on_boot=false, ${P0} bytes in /logs, restarting"
  if reboot_and_wait; then
    sleep 5
    P1="$(log_bytes)"
    # Not equality: the boot itself logs, so the file can only be bigger.
    (( P1 >= P0 )) \
      && pass "delete_on_boot=false kept /logs across the restart (${P0} -> ${P1} bytes)" \
      || fail "delete_on_boot=false lost log data across the restart (${P0} -> ${P1} bytes)"
  else
    fail "the clock did not come back after the restart"
  fi

  post "/api/logs/settings?delete_on_boot=true" > /dev/null
  probe DEBUG; sleep 2
  Q0="$(log_bytes)"
  info "delete_on_boot=true, ${Q0} bytes in /logs, restarting"
  if reboot_and_wait; then
    sleep 5
    Q1="$(log_bytes)"
    # The wipe happens in logEnableFileSink() before the first line is written,
    # so what is left is this boot only and must be well under what was there.
    (( Q1 < Q0 )) \
      && pass "delete_on_boot=true wiped /logs at boot (${Q0} -> ${Q1} bytes)" \
      || fail "delete_on_boot=true did not wipe /logs (${Q0} -> ${Q1} bytes)"
  else
    fail "the clock did not come back after the restart"
  fi
  post "/api/logs/settings?delete_on_boot=${BASE_DELBOOT}" > /dev/null
fi

# ---------------------------------------------------------------------------
echo "==> Phase 6: retention"
skip "retention pruning cannot be exercised over HTTP"
info "ensureLogFile() prunes only when the date tag changes, and it compares"
info "each file's getLastWrite() against now - retention*86400. Reaching it"
info "needs either a file older than the retention window or a midnight"
info "rollover, and neither can be produced through the HTTP API: there is no"
info "route that writes a log file with a chosen mtime. Verifying it means a"
info "unit test over ensureLogFile() with an injectable clock and filesystem,"
info "or letting a clock run past midnight with a short retention and reading"
info "/api/logs the next day. Phase 3 only proves the value is stored."

# ---------------------------------------------------------------------------
echo ""
echo "==> ${PASS} passed, ${FAIL} failed, ${SKIP} skipped"
(( FAIL == 0 )) || exit 1

# FINDINGS, 2026-08-22, nextgen-50x50-26.08.21-rc.3 at 192.168.20.131
#
# Phase 2 failed on the first run. The clock had written /logs/2026-08-22.log
# up to 00:43:29 CEST and then stopped, while the RAM ring buffer went on
# recording every heartbeat through 08:33. A probe line issued at 08:33:03
# appeared in /log and did not move the file by a single byte. Both log
# settings still reported correctly and every heartbeat kept arriving, so
# nothing in the fleet view showed anything wrong. See the session notes.
