#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 [--no-pairing] <BLE_MAC> <SSID> <PASS>"
}

prompt_default() {
  local label="$1"
  local default="$2"
  local value=""
  read -r -p "$label [$default]: " value
  value=${value:-$default}
  printf "%s" "$value"
}

prompt_required() {
  local label="$1"
  local value=""
  while [[ -z "$value" ]]; do
    read -r -p "$label: " value
  done
  printf "%s" "$value"
}

prompt_secret_required() {
  local label="$1"
  local value=""
  while [[ -z "$value" ]]; do
    read -r -s -p "$label: " value
    echo
  done
  printf "%s" "$value"
}

scan_ble() {
  local seconds="$1"
  python3 - "$seconds" <<'PY'
import asyncio, re, sys
from bleak import BleakScanner

try:
    timeout = float(sys.argv[1])
except Exception:
    timeout = 8.0

async def main():
    devices = await BleakScanner.discover(timeout=timeout)
    seen = {}
    name_re = re.compile(r"^Wordclock-[A-Za-z0-9]+$")
    for d in devices:
        name = (d.name or "").replace("\t", " ").strip()
        if not name_re.match(name):
            continue
        rssi = getattr(d, "rssi", None)
        if rssi is None:
            rssi = -999
        if d.address not in seen or rssi > seen[d.address][1]:
            seen[d.address] = (name, rssi)

    def sort_key(item):
        return item[1][1]

    for address, (name, rssi) in sorted(seen.items(), key=sort_key, reverse=True):
        rssi_str = "" if rssi == -999 else str(rssi)
        print(f"{address}\t{name}\t{rssi_str}")

try:
    asyncio.run(main())
except Exception as e:
    print(f"ERROR: {e}", file=sys.stderr)
    sys.exit(1)
PY
}

scan_wifi() {
  local seconds="$1"
  python3 - "$seconds" <<'PY'
import os, re, subprocess, sys, time

try:
    duration = float(sys.argv[1])
except Exception:
    duration = 5.0

airport = os.environ.get(
    "AIRPORT_PATH",
    "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport",
)

if not os.path.exists(airport):
    print("ERROR: airport tool not found", file=sys.stderr)
    sys.exit(1)

with_bssid_re = re.compile(r"^(?P<ssid>.+?)\s+([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\s+(?P<rssi>-?\d+)\s+")
no_bssid_re = re.compile(r"^(?P<ssid>.+?)\s+(?P<rssi>-?\d+)\s+")
networks = {}
end = time.time() + duration
last_output = ""

while time.time() < end:
    try:
        out = subprocess.check_output([airport, "-s"], stderr=subprocess.DEVNULL)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    text = out.decode(errors="ignore")
    last_output = text
    lines = text.splitlines()
    if lines:
        lines = lines[1:]

    for line in lines:
        if not line.strip():
            continue
        line = line.rstrip()
        m = with_bssid_re.match(line)
        if not m:
            m = no_bssid_re.match(line)
        if m:
            ssid = m.group("ssid").replace("\t", " ").strip()
            if not ssid:
                continue
            try:
                rssi = int(m.group("rssi"))
            except Exception:
                continue
            if ssid not in networks or rssi > networks[ssid]:
                networks[ssid] = rssi
            continue

        cols = re.split(r"\s{2,}", line.strip())
        if not cols:
            continue
        ssid = cols[0].replace("\t", " ").strip()
        if not ssid:
            continue
        rssi = None
        if len(cols) > 1:
            try:
                rssi = int(cols[1])
            except Exception:
                rssi = None
        if rssi is None and len(cols) > 2:
            try:
                rssi = int(cols[2])
            except Exception:
                rssi = None
        if rssi is None:
            continue
        if ssid not in networks or rssi > networks[ssid]:
            networks[ssid] = rssi

    time.sleep(1)

if not networks:
    print("DEBUG: No WiFi networks parsed from airport output.", file=sys.stderr)
    if last_output:
        print("DEBUG: Raw airport -s output:", file=sys.stderr)
        for line in last_output.splitlines()[:20]:
            print(f"DEBUG: {line}", file=sys.stderr)
else:
    print(f"DEBUG: Parsed {len(networks)} unique SSIDs.", file=sys.stderr)

for ssid, rssi in sorted(networks.items(), key=lambda x: x[1], reverse=True):
    print(f"{ssid}\t{rssi}")
PY
}

select_ble_device() {
  local scan_seconds="$1"
  local total_seconds="$2"
  local -a ble_addrs=()
  local -a ble_names=()
  local -a ble_rssis=()
  local total_wait=0
  local first_hint=1

  update_ble() {
    local addr="$1"
    local name="$2"
    local rssi="$3"
    local idx=-1
    for i in "${!ble_addrs[@]}"; do
      if [[ "${ble_addrs[$i]}" == "$addr" ]]; then
        idx=$i
        break
      fi
    done
    if [[ $idx -eq -1 ]]; then
      ble_addrs+=("$addr")
      ble_names+=("$name")
      ble_rssis+=("$rssi")
    else
      if [[ -n "$name" ]]; then
        ble_names[$idx]="$name"
      fi
      if [[ "$rssi" =~ ^-?[0-9]+$ ]]; then
        if [[ -z "${ble_rssis[$idx]}" || "$rssi" -gt "${ble_rssis[$idx]}" ]]; then
          ble_rssis[$idx]="$rssi"
        fi
      fi
    fi
  }

  while true; do
    echo
    if [ "$first_hint" -eq 1 ]; then
      echo "Auto-refresh every ${scan_seconds}s for up to ${total_seconds}s."
      echo "Type a number and press Enter to select, or type a MAC."
      first_hint=0
    fi
    echo "Scanning BLE devices for ${scan_seconds}s..."
    ble_tmp="$(mktemp -t wordclock_ble_scan)"
    scan_ble "$scan_seconds" >"$ble_tmp" &
    ble_pid=$!

    if read -r -t "$scan_seconds" -p "Select BLE device [1-${#ble_addrs[@]}] or MAC (Enter to keep scanning): " sel; then
      if [[ -n "$sel" ]]; then
        kill "$ble_pid" 2>/dev/null || true
        wait "$ble_pid" 2>/dev/null || true
        rm -f "$ble_tmp"
        if [[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#ble_addrs[@]}" ]; then
          ADDR="${ble_addrs[$((sel - 1))]}"
          return 0
        fi
        ADDR="$sel"
        return 0
      fi
    fi

    wait "$ble_pid" 2>/dev/null || true
    if [ -s "$ble_tmp" ]; then
      while IFS=$'\t' read -r addr name rssi; do
        [[ -z "$addr" ]] && continue
        update_ble "$addr" "$name" "$rssi"
      done <"$ble_tmp"
    else
      echo "⚠️  BLE scan failed. You can enter a MAC manually."
    fi
    rm -f "$ble_tmp"
    total_wait=$((total_wait + scan_seconds))

    if [ "${#ble_addrs[@]}" -gt 0 ]; then
      echo "Available BLE devices:"
      local index=1
      for i in "${!ble_addrs[@]}"; do
        local addr="${ble_addrs[$i]}"
        local name="${ble_names[$i]}"
        local rssi="${ble_rssis[$i]}"
        if [[ -n "$name" && -n "$rssi" ]]; then
          printf "  [%d] %s (%s, RSSI %s)\n" "$index" "$addr" "$name" "$rssi"
        elif [[ -n "$name" ]]; then
          printf "  [%d] %s (%s)\n" "$index" "$addr" "$name"
        elif [[ -n "$rssi" ]]; then
          printf "  [%d] %s (RSSI %s)\n" "$index" "$addr" "$rssi"
        else
          printf "  [%d] %s\n" "$index" "$addr"
        fi
        index=$((index + 1))
      done
    else
      echo "No BLE devices found yet."
    fi

    if [ "$total_wait" -ge "$total_seconds" ] && [ "${#ble_addrs[@]}" -eq 0 ]; then
      echo "No BLE devices found after ${total_wait}s. You can enter a MAC manually."
      ADDR="$(prompt_required "Bluetooth MAC")"
      return 0
    fi

    sleep 0.2
    sel=""
    if [[ -z "$sel" ]]; then
      continue
    fi
    if [[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#ble_addrs[@]}" ]; then
      ADDR="${ble_addrs[$((sel - 1))]}"
      return 0
    fi
    ADDR="$sel"
    return 0
  done
}

select_wifi_network() {
  local scan_seconds="$1"
  local total_seconds="$2"
  local -a wifi_ssids=()
  local -a wifi_rssis=()
  local total_wait=0
  local first_hint=1

  update_wifi() {
    local ssid="$1"
    local rssi="$2"
    local idx=-1
    for i in "${!wifi_ssids[@]}"; do
      if [[ "${wifi_ssids[$i]}" == "$ssid" ]]; then
        idx=$i
        break
      fi
    done
    if [[ $idx -eq -1 ]]; then
      wifi_ssids+=("$ssid")
      wifi_rssis+=("$rssi")
    else
      if [[ "$rssi" =~ ^-?[0-9]+$ ]]; then
        if [[ -z "${wifi_rssis[$idx]}" || "$rssi" -gt "${wifi_rssis[$idx]}" ]]; then
          wifi_rssis[$idx]="$rssi"
        fi
      fi
    fi
  }

  while true; do
    echo
    if [ "$first_hint" -eq 1 ]; then
      echo "Auto-refresh every ${scan_seconds}s for up to ${total_seconds}s."
      echo "Type a number and press Enter to select, or type an SSID."
      first_hint=0
    fi
    echo "Scanning WiFi networks for ${scan_seconds}s..."
    wifi_tmp="$(mktemp -t wordclock_wifi_scan)"
    scan_wifi "$scan_seconds" >"$wifi_tmp" &
    wifi_pid=$!

    if read -r -t "$scan_seconds" -p "Select WiFi network [1-${#wifi_ssids[@]}] or SSID (Enter to keep scanning): " sel; then
      if [[ -n "$sel" ]]; then
        kill "$wifi_pid" 2>/dev/null || true
        wait "$wifi_pid" 2>/dev/null || true
        rm -f "$wifi_tmp"
        if [[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#wifi_ssids[@]}" ]; then
          SSID="${wifi_ssids[$((sel - 1))]}"
          return 0
        fi
        SSID="$sel"
        return 0
      fi
    fi

    wait "$wifi_pid" 2>/dev/null || true
    if [ -s "$wifi_tmp" ]; then
      while IFS=$'\t' read -r ssid rssi; do
        [[ -z "$ssid" ]] && continue
        update_wifi "$ssid" "$rssi"
      done <"$wifi_tmp"
    else
      echo "⚠️  WiFi scan failed. You can enter an SSID manually."
      echo "Tip: macOS may block WiFi scanning without Location Services permission."
      echo "If scans keep failing, check System Settings → Privacy & Security → Location Services."
      echo "Also verify Wi-Fi is enabled in the menu bar."
    fi
    rm -f "$wifi_tmp"
    total_wait=$((total_wait + scan_seconds))

    if [ "${#wifi_ssids[@]}" -gt 0 ]; then
      echo "Available WiFi networks:"
      local index=1
      for i in "${!wifi_ssids[@]}"; do
        local ssid="${wifi_ssids[$i]}"
        local rssi="${wifi_rssis[$i]}"
        if [[ -n "$rssi" ]]; then
          printf "  [%d] %s (RSSI %s)\n" "$index" "$ssid" "$rssi"
        else
          printf "  [%d] %s\n" "$index" "$ssid"
        fi
        index=$((index + 1))
      done
    else
      echo "No WiFi networks found yet."
    fi

    if [ "$total_wait" -ge "$total_seconds" ] && [ "${#wifi_ssids[@]}" -eq 0 ]; then
      echo "No WiFi networks found after ${total_wait}s. You can enter an SSID manually."
      SSID="$(prompt_required "WiFi SSID")"
      return 0
    fi

    sleep 0.2
    sel=""
    if [[ -z "$sel" ]]; then
      continue
    fi
    if [[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#wifi_ssids[@]}" ]; then
      SSID="${wifi_ssids[$((sel - 1))]}"
      return 0
    fi
    SSID="$sel"
    return 0
  done
}

NO_PAIRING=0
if [[ "${1-}" == "--no-pairing" ]]; then
  NO_PAIRING=1
  shift
fi

ADDR="${1-}"
SSID="${2-}"
PASS="${3-}"

BLE_SCAN_SECONDS=2
BLE_SCAN_TOTAL=20

if [[ -z "$ADDR" ]]; then
  select_ble_device "$BLE_SCAN_SECONDS" "$BLE_SCAN_TOTAL"
else
  echo "Using provided Bluetooth MAC: $ADDR"
fi

echo
WIFI_SCAN_SECONDS=2
WIFI_SCAN_TOTAL=20

if [[ -z "$SSID" ]]; then
  select_wifi_network "$WIFI_SCAN_SECONDS" "$WIFI_SCAN_TOTAL"
else
  echo "Using provided WiFi SSID: $SSID"
fi

if [[ -z "$PASS" ]]; then
  PASS="$(prompt_secret_required "WiFi Password")"
fi

if [[ -z "$ADDR" || -z "$SSID" || -z "$PASS" ]]; then
  usage
  exit 2
fi

echo
echo "Bluetooth MAC: $ADDR"
echo "WiFi SSID: $SSID"
echo "WiFi Password: (hidden)"
if [ "$NO_PAIRING" -eq 1 ]; then
  echo "Pairing prompt trigger: disabled"
else
  echo "Pairing prompt trigger: enabled"
fi
echo
echo "Provisioning over BLE..."

set +e
python3 - "$ADDR" "$SSID" "$PASS" "$NO_PAIRING" <<'PY'
import asyncio, sys, json
from bleak import BleakClient
from bleak.exc import BleakDeviceNotFoundError, BleakError

ADDR = sys.argv[1]
SSID = sys.argv[2]
PASS = sys.argv[3]
NO_PAIRING = len(sys.argv) > 4 and sys.argv[4] == "1"

SSID_UUID  = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
PASS_UUID  = "beb5483e-36e1-4688-b7f5-ea07361b26a9"
CMD_UUID   = "beb5483e-36e1-4688-b7f5-ea07361b26ab"
STATUS_UUID= "beb5483e-36e1-4688-b7f5-ea07361b26aa"

success = False
last_status = None
last_state = None

def on_status(_, data: bytearray):
    global success, last_status, last_state
    try:
        msg = data.decode()
        print("status:", msg)
        try:
            parsed = json.loads(msg)
            last_status = parsed
            state = parsed.get("state")
            if state:
                last_state = state
            if state == "wifi_ok":
                success = True
        except Exception:
            last_status = msg
            if '"state":"wifi_ok"' in msg:
                success = True
    except Exception:
        pass

def status_summary():
    if isinstance(last_status, dict):
        parts = []
        for key in ("state", "wifi_status", "ble_reason", "attempt", "ssid", "rssi"):
            if key in last_status:
                parts.append(f"{key}={last_status[key]}")
        return ", ".join(parts) if parts else "unknown"
    return str(last_status) if last_status else "unknown"

def is_wifi_failure():
    state = ""
    if isinstance(last_status, dict):
        state = str(last_status.get("state", ""))
    elif last_state:
        state = str(last_state)
    if state.startswith("wifi_") and state not in ("wifi_ok", "wifi_connecting"):
        return True
    return False

async def main():
    global success
    client = BleakClient(ADDR, timeout=20.0)
    wrote_all = False
    try:
        print("Connecting...", flush=True)
        await asyncio.wait_for(client.connect(), timeout=20.0)
        if not client.is_connected:
            print("ERROR: Failed to connect.", flush=True)
            return 2
        await client.start_notify(STATUS_UUID, on_status)

        if not NO_PAIRING:
            # trigger pairing prompt
            await asyncio.wait_for(client.read_gatt_char(STATUS_UUID), timeout=5.0)
            print("Accept pairing prompt if it appears...", flush=True)
            await asyncio.sleep(5.0)

        max_attempts = 3
        wait_seconds = 15.0
        retry_delay = 5.0

        for attempt in range(1, max_attempts + 1):
            print(f"WiFi connect attempt {attempt}/{max_attempts}...", flush=True)
            await asyncio.wait_for(client.write_gatt_char(SSID_UUID, SSID.encode(), response=True), timeout=10.0)
            await asyncio.wait_for(client.write_gatt_char(PASS_UUID, PASS.encode(), response=True), timeout=10.0)
            await asyncio.wait_for(client.write_gatt_char(CMD_UUID, b"apply", response=True), timeout=10.0)
            wrote_all = True

            start = asyncio.get_event_loop().time()
            while asyncio.get_event_loop().time() - start < wait_seconds:
                if success:
                    break
                if is_wifi_failure():
                    break
                await asyncio.sleep(0.5)

            if success:
                print("wifi_ok received, sending stop command...", flush=True)
                try:
                    await asyncio.wait_for(client.write_gatt_char(CMD_UUID, b"stop", response=True), timeout=5.0)
                    print("stop command sent.", flush=True)
                except Exception as e:
                    print(f"BLE warning: failed to send stop command: {e}", flush=True)
                return 0

            print(f"WiFi not connected yet ({status_summary()}).", flush=True)
            if attempt < max_attempts:
                print(f"Retrying in {int(retry_delay)}s...", flush=True)
                await asyncio.sleep(retry_delay)
    except BleakError as e:
        print(f"BLE warning: {e}. Continuing.", flush=True)
        return 1 if wrote_all else 2
    finally:
        if client.is_connected:
            await client.disconnect()

    if success:
        print("Provisioning OK (wifi_ok received).")
        return 0
    else:
        print(f"Provisioning uncertain (no wifi_ok). Last status: {status_summary()}")
        return 1

async def run_with_timeout():
    return await asyncio.wait_for(main(), timeout=45.0)

try:
    sys.exit(asyncio.run(run_with_timeout()))
except BleakDeviceNotFoundError:
    print(f"BLE warning: Device not found: {ADDR}")
    print("Make sure it is powered on, in range, and not already connected.")
    sys.exit(2)
except BleakError as e:
    print(f"BLE warning: {e}")
    sys.exit(2)
except asyncio.TimeoutError:
    print("BLE warning: operation timed out.")
    sys.exit(2)
except Exception as e:
    print(f"BLE warning: Unexpected error: {e}")
    sys.exit(2)
PY

BLE_STATUS=$?
set -e
BLE_FAILED=0
BLE_INCOMPLETE=0
if [ "$BLE_STATUS" -ne 0 ]; then
  if [ "$BLE_STATUS" -eq 1 ]; then
    BLE_INCOMPLETE=1
  else
    BLE_FAILED=1
  fi
else
  echo "✅ BLE provisioning reported success."
fi
echo
if [ "$BLE_STATUS" -ne 0 ]; then
  echo "BLE reported an error; waiting 5s before HTTP check..."
  sleep 5
fi
INFO_URL="http://wordclock.local/api/device/info"
CURL_ATTEMPTS=5
CURL_DELAY=1
echo "Checking device at $INFO_URL (up to $CURL_ATTEMPTS attempts, ~$(($CURL_ATTEMPTS * $CURL_DELAY))s total) ..."

SUCCESS=0
for attempt in $(seq 1 "$CURL_ATTEMPTS"); do
  if curl -4 --fail --silent --max-time 5 --connect-timeout 5 "$INFO_URL" >/tmp/wordclock_info.json; then
    SUCCESS=1
    break
  fi
  sleep "$CURL_DELAY"
done

if [ "$SUCCESS" -eq 1 ]; then
  echo "✅ Device responded. Device info:"
  if command -v jq >/dev/null 2>&1; then
    jq . /tmp/wordclock_info.json
  else
    python3 -m json.tool /tmp/wordclock_info.json 2>/dev/null || cat /tmp/wordclock_info.json
  fi
  exit 0
else
  if [ "$BLE_FAILED" -eq 1 ]; then
    echo "❌ BLE provisioning failed (exit $BLE_STATUS)."
  elif [ "$BLE_INCOMPLETE" -eq 1 ]; then
    echo "⚠️  BLE provisioning incomplete (no wifi_ok)."
  fi
  echo "❌ No response from $INFO_URL after $CURL_ATTEMPTS attempts (~$(($CURL_ATTEMPTS * $CURL_DELAY))s)"
  echo "Try: curl -4 -v $INFO_URL (or check mDNS / proxy settings)."
  exit 1
fi
