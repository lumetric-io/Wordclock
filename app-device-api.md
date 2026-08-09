# App ↔ Device HTTP API contract

Complete reference for the local HTTP API exposed by a Wordclock device's
on-board web server. The companion app's native **Settings** screen is the
primary consumer, but this document now covers the **full per-device API
surface** so it can serve as the single source of truth.

- **Base URL:** `http://<device-ip>/` (device's local web server). The device
  also advertises itself on mDNS as `wordclock.local`.
- **Auth:** every UI handler calls `ensureUiAuth()`, which currently **always
  returns `true`** (`src/web_routes.h:134`). No credentials are enforced on UI
  endpoints today; treat the device as **trusted-LAN only**. A small set of
  destructive/admin endpoints use HTTP Basic auth via `ensureAdminAuth()`
  instead — these are called out per-endpoint. See **Known limitations /
  security** at the end.
- **Source:** all per-device routes are registered in `src/web_routes.h`
  (`setupWebRoutes`). A separate **bootstrap/factory** firmware role registers
  its own routes in `src/bootstrap_provision.cpp` — those are documented in
  their own section and are **not** part of the per-device app contract.
- Responses are `text/plain` unless noted as `application/json`.
- Many endpoints are **compile-time gated**. A route that is `#if`-ed out for a
  given build simply does not exist → the server returns `404`. The relevant
  gates: `OTA_ENABLED`, `UPDATE_UPLOAD_ENABLED`, `SUPPORT_OTA_V2`,
  `PRODUCT_VARIANT_MINI`, `PRODUCT_VARIANT_LOGO`, `PRODUCT_VARIANT_NEXTGEN`.

> **Location note:** the firmware repo's `docs/` is an uninitialized git submodule
> (`chronolett-firmware-docs`) and `.gitignore` excludes all `*.md`, so this contract
> is kept at the repo root and whitelisted in `.gitignore`. Mirror it into the `docs`
> submodule when that repo is available.

---

## 1. Settings & display control

These are the endpoints the app's Settings screen drives. Unless noted, params
are passed as **query string** on a `GET`.

| Endpoint | Method | Params | Range / format | Response | Notes |
|---|---|---|---|---|---|
| `/setColor` | GET | `color=RRGGBB` (query) | 6 hex digits; non-hex chars stripped, then must be exactly 6 | `200 "OK"` / `400 "Missing color"` / `400 "Invalid color"` | Sets clock RGB, refreshes display. `web_routes.h:1122` |
| `/getColor` | GET | — | — | `200` `RRGGBB` (white → `FFFFFF`) | Read-back companion to `/setColor`. `web_routes.h:1160` |
| `/setBrightness` | GET | `level=N` (query) | `0`–`255` (`constrain`) | `200 "OK"` / `400 "Missing brightness level"` | Clock LED brightness. `web_routes.h:1343` |
| `/getBrightness` | GET | — | — | `200` `N` (0–255) | `web_routes.h:1334` |
| `/setAnimate` | GET | `state=...` (query) | truthy = `on` \| `1` \| `true`; anything else = off | `200 "OK"` / `400 "Missing state"` | Word-by-word animation. **Accepts `0|1`.** `web_routes.h:1483` |
| `/getAnimate` | GET | — | — | `200` `on` \| `off` | `web_routes.h:1474` |
| `/toggle` | GET | `state=...` (query) | **only `on` enables**; everything else (incl. `1`, `0`, `off`) disables | `200 "OK"` | Clock on/off. ⚠️ **Does NOT accept `0|1`** — see mismatch §9.1. `web_routes.h:1058` |
| `/status` | GET | — | — | `200` `on` \| `off` | Read-back for `/toggle` (clock enabled state). `web_routes.h:992` |
| `/setSellMode` | GET | `state=...` (query) | truthy = `on` \| `1` \| `true` | `200 "OK"` / `400 "Missing state"` | Demo mode: forces the 11:49 display. `web_routes.h:1451` |
| `/getSellMode` | GET | — | — | `200` `on` \| `off` | `web_routes.h:1447` |
| `/setNightModeConfig` | **POST** | **JSON body** (not query) | see §1.2 | `200` JSON (same shape as `/getNightModeConfig`) / `400` on invalid field | `web_routes.h:1506` |
| `/getNightModeConfig` | GET | — | — | `200` JSON (see §1.2) | `web_routes.h:1498` |
| `/setHetIsDuration` | GET | `seconds=N` (query) | `0`–`360` (0 = never, 360 = always) | `200 "OK"` / `400 "Missing seconds"` / `404` on MINI | Not supported on `PRODUCT_VARIANT_MINI` (returns `404`). `web_routes.h:1641` |
| `/getHetIsDuration` | GET | — | — | `200` `N` (0–360) / `404` on MINI | `web_routes.h:1628` |
| `/startSequence` | GET | — | — | `200 "Startup sequence executed"` | Replays the boot/startup LED animation. `web_routes.h:1174` |

### 1.1 Logo variants (compiled only when `PRODUCT_VARIANT_LOGO`)

These exist **only** on the logo product (`#if defined(PRODUCT_VARIANT_LOGO)`,
`web_routes.h:1364`). On non-logo firmware the routes are absent → `404`.

| Endpoint | Method | Params | Response | Notes |
|---|---|---|---|---|
| `/logo/state` | GET | — | `200` JSON: `{ brightness, count, start, colors[] }` | `colors[]` = `count` `RRGGBB` strings. `web_routes.h:1365` |
| `/logo/state` | POST | JSON body | `200` JSON (same shape as GET) / `400` | Body keys (all optional): `brightness` (int 0–255), `all` (`RRGGBB`), `colors` (array of **exactly** `count` `RRGGBB` strings). `web_routes.h:1370` |

### 1.2 Night-mode config (`/setNightModeConfig` POST body / `/getNightModeConfig` response)

`/setNightModeConfig` takes a **JSON body** (`Content-Type` irrelevant; the
firmware reads the raw body from the `plain` arg). All keys optional; only
provided keys are applied. Missing body → `400 "Missing body"`; unparseable →
`400 "Invalid JSON"`.

| Key | Type | Accepted values | Notes |
|---|---|---|---|
| `enabled` | bool / int / string | bool, `!=0`, or `true`/`on`/`1` | |
| `effect` | string | `off` \| `dim` | else `400 "Invalid effect"` |
| `dim_percent` | int | `0`–`100` (clamped) | |
| `start` | string | `"HH:MM"` (`parseTimeString`) | takes priority over `start_minutes`; bad value → `400 "Invalid start time"` |
| `start_minutes` | int | `0`–`1439` | used if `start` absent; out of range → `400 "Invalid start minutes"` |
| `end` | string | `"HH:MM"` | takes priority over `end_minutes`; bad value → `400 "Invalid end time"` |
| `end_minutes` | int | `0`–`1439` | used if `end` absent; out of range → `400 "Invalid end minutes"` |
| `override` | string | `auto` \| `force_on`/`on` \| `force_off`/`off` | else `400 "Invalid override"` |

Response (both GET and the POST reply), from `sendNightModeConfig()`
(`web_routes.h:151`):

```json
{
  "enabled": true,
  "effect": "dim",
  "dim_percent": 20,
  "start": "23:00",
  "end": "07:00",
  "start_minutes": 1380,
  "end_minutes": 420,
  "override": "auto",
  "active": false,
  "schedule_active": true,
  "time_synced": true
}
```

### 1.3 Language & dialect

The language is which **front plate** the clock physically has — a German plate
rendering Dutch words spells nonsense. So switching language swaps the letter
grid, the word table and the LED counts, and is applied by a **reboot**; the
dialect only swaps the phrase table and takes effect immediately.

A build contains one plate per language it supports. `available` therefore
lists what this specific firmware can render, which is a property of the
product, not of the app — never hardcode the list.

| Endpoint | Method | Params | Response | Notes |
|---|---|---|---|---|
| `/api/language` | GET | — | `200` JSON (below) | `web_routes.h:915` |
| `/api/language` | POST | `lang=<iso>` (query) | `200` JSON / `400 "Missing 'lang'"` / `400 "Unknown language: X"` | **Device reboots** when the code differs from the running one. `web_routes.h:932` |
| `/api/dialect` | GET | — | `200` JSON (below) | Dialects of the **active** language only. `web_routes.h:957` |
| `/api/dialect` | POST | `dialect=<id>` (query) | `200` JSON / `400 "Missing 'dialect'"` / `400 "Unknown dialect for active language: X"` | Applied live, no reboot. `web_routes.h:975` |

`GET /api/language`:

```json
{
  "active": "nl",
  "stored": "nl",
  "source": "default",
  "setupComplete": false,
  "rebootRequired": false,
  "available": ["nl", "de"]
}
```

- `active` — what is rendering right now.
- `stored` — what is in NVS. Differs from `active` only between a POST and the
  reboot that carries it out.
- `source` — `default` \| `user` \| `migrated`. `default` means nobody has
  chosen and the build's first plate is rendering; `migrated` means the field
  migration pinned this device to the language it already spoke.
- `setupComplete` — `source != "default"`. Reported now; nothing is gated on it
  yet (see the phasing note below).

`POST /api/language` replies before rebooting:

```json
{ "stored": "de", "source": "user", "rebootRequired": true }
```

⚠️ When `rebootRequired` is `true` the connection drops ~100 ms after the
response. Treat the reply as final and poll `/api/firmware/identity` to detect
the device coming back.

Posting the **already active** language is still a choice: it moves `source` to
`user` without a reboot. That is the whole interaction on a single-language
product — one confirmation.

`GET /api/dialect`:

```json
{
  "active": "de-nord",
  "available": [
    { "id": "de-nord", "label": "Hochdeutsch", "sample": "viertel nach zehn · viertel vor elf" },
    { "id": "de-sued", "label": "Süd-Ost",     "sample": "viertel elf · dreiviertel elf" }
  ]
}
```

`sample` is the phrasing that distinguishes this dialect — it is what the user
picks on, since "Hochdeutsch" vs "Süd-Ost" means little on its own. Dutch has
exactly one dialect (`nl`), so the list is never empty and the app can use the
same UI for both languages.

⚠️ A language switch **clears the stored dialect** — a dialect belongs to one
plate. Re-read `/api/dialect` after the device comes back up.

**Phasing note:** the display is *not* gated on `setupComplete` in this
firmware. Every clock keeps showing the time regardless of `source`. The gate
is a later change, and it will only be armed once fleet telemetry shows no
device still reporting `langSrc: "default"`.

---

## 2. Device info & identity

Telemetry and version/identity probes. `/api/firmware/identity` and
`/api/ble/status` are **deliberately unauthenticated** (they reveal nothing
sensitive) so polling JS can distinguish "offline" from "needs creds".

| Endpoint | Method | Params | Response | Notes |
|---|---|---|---|---|
| `/api/device/info` | GET | — | `200` JSON (see §2.1) | Device telemetry + identity. `web_routes.h:857` |
| `/api/device/register` | POST | — | `200` JSON `{deviceId, token}` / `502` text on failure | Enrols the device with the fleet backend. `web_routes.h:906` |
| `/api/firmware/identity` | GET | — | `200` JSON `{role, firmware, ui, product_id}` | `role` = `"product"` here. Mirror exists in bootstrap (role `"bootstrap"`). `web_routes.h:939` |
| `/buildinfo` | GET | — | `200` JSON (see §2.2) | Firmware/UI build metadata. `web_routes.h:841` |
| `/version` | GET | — | `200` `FIRMWARE_VERSION` (text) | `web_routes.h:1429` |
| `/uiversion` | GET | — | `200` UI version (text) | `web_routes.h:1438` |
| `/api/ble/status` | GET | — | `200` JSON `{active, state, hardware_id}` | BLE provisioning status (unauthenticated). `web_routes.h:459` |
| `/api/ble/start` | POST | — | `200 "OK"` / `409` if already active | Starts BLE provisioning. `web_routes.h:449` |

### 2.1 `/api/device/info` JSON fields

From `web_routes.h:857`:

| Field | Type | Meaning |
|---|---|---|
| `uptime_ms` | number | Uptime in ms (`millis()`) |
| `uptime_human` | string | `"<days>d HH:MM:SS"` |
| `heap_free` | number | Free heap bytes |
| `heap_min_free` | number | Min free heap since boot |
| `cpu_freq_mhz` | number | CPU frequency |
| `chip_model` | string | e.g. `ESP32-S3` |
| `chip_rev` | number | Chip revision |
| `sdk` | string | ESP-IDF SDK version |
| `rssi` | number | Wi-Fi RSSI (dBm) |
| `product_id` | string | `PRODUCT_ID` compile constant |
| `product` | string | `mini` \| `logo` \| `nextgen` (variant), else `product_id` |
| `hardware_id` | string | ESP32 only |
| `device_id` | string | Fleet device id (ESP32 only) |
| `has_device_token` | bool | Whether a fleet token is enrolled (ESP32 only) |
| `temp_c` | number | On-die temperature (ESP32 only) |

### 2.2 `/buildinfo` JSON fields

From `web_routes.h:841`:

| Field | Type | Meaning |
|---|---|---|
| `firmware` | string | `FIRMWARE_VERSION` |
| `ui` | string | UI version (`getUiVersion()`) |
| `git_sha` | string | Build commit SHA |
| `git_branch` | string | Build branch |
| `build_time_utc` | string | Build timestamp (UTC) |
| `environment` | string | PlatformIO env name |
| `ui_sync_supported` | bool | `true` when `SUPPORT_OTA_V2 == 0` (legacy manifest UI sync available) |
| `ota_enabled` | bool | Whether OTA is compiled in |

---

## 3. Firmware update / OTA

All endpoints in this group are gated on **`OTA_ENABLED`** unless noted. On a
build without OTA they return `404`. The device pulls firmware from a remote
OTA server; there is **no device-hosted manifest endpoint** (see note below).

| Endpoint | Method | Params | Response | Notes |
|---|---|---|---|---|
| `/api/update/status` | GET | — | `200` JSON `{running: bool}` | Is an OTA currently in progress. `web_routes.h:924` |
| `/api/update/channel` | GET | — | `200` JSON `{channel, default:"stable"}` | Current update channel. `web_routes.h:649` |
| `/api/update/channel` | POST | `channel=...` (query) **or** JSON body `{"channel":"..."}` | `200` JSON `{channel, default}` / `400` | Allowed: `stable` \| `early` \| `develop` (case-insensitive). `web_routes.h:663` |
| `/getAutoUpdate` | GET | — | `200` `on` \| `off` | Automatic-update toggle state. `web_routes.h:622` |
| `/setAutoUpdate` | GET | `state=...` (query) | `200 "OK"` / `400 "Missing state"` / `400` on develop channel | truthy = `on`\|`1`\|`true`. **Blocked** (`400`) while channel = `develop`. `web_routes.h:631` |
| `/checkForUpdate` | ANY | — | `200 "Firmware update started"` / `409` if already running / `500` | Kicks off a background OTA check+install task. `web_routes.h:1266` |
| `/uploadFirmware` | POST | multipart upload (app firmware `.bin`) | `200` text, then reboots on success | Gated on `UPDATE_UPLOAD_ENABLED`. Manual firmware flash. `web_routes.h:1183` |
| `/uploadFs` | POST | multipart upload (filesystem `.bin`) | `200` text, then reboots on success | Gated on `UPDATE_UPLOAD_ENABLED`. Manual UI/filesystem flash. `web_routes.h:1224` |
| `/syncUI` | POST | — | `200 "UI sync started"` | **Admin auth.** Gated on `SUPPORT_OTA_V2 == 0`. Pulls UI files from manifest. `web_routes.h:1326` |
| `/api/installBootstrap` | POST | — | `202 "Bootstrap install started"` / `409` / `500` | **Admin auth.** Replaces per-device firmware with the **bootstrap** firmware, then reboots into the product picker. `web_routes.h:1299` |

> **`/api/ota/manifest` does not exist on the device.** The task brief listed it,
> but no such route is registered in firmware. The OTA *manifest* is a file
> hosted on the **remote OTA/S3 server** that the device fetches during an update
> check (`ota_updater.cpp`); it is never served by the device's own HTTP server.
> The app should not call `/api/ota/manifest` — use `/api/update/status` and
> `/api/update/channel` instead.

> **App relevance:** `/api/update/status`, `/api/update/channel`,
> `/getAutoUpdate`, `/setAutoUpdate`, and `/checkForUpdate` are the
> app-relevant OTA controls. `/uploadFirmware` / `/uploadFs` are manual
> recovery tools (web UI), and `/syncUI` + `/api/installBootstrap` are
> admin/factory operations — the app should not expose these to end users.

---

## 4. MQTT

Configure and probe the device's MQTT (Home Assistant) integration. Config and
test endpoints accept **form-encoded** args (`application/x-www-form-urlencoded`),
not JSON. The stored password is **never** returned — `/api/mqtt/config` GET
exposes only a `has_pass` boolean.

| Endpoint | Method | Params | Response | Notes |
|---|---|---|---|---|
| `/api/mqtt/status` | GET | — | `200` JSON `{connected: bool, last_error: string}` | Runtime connection state. `web_routes.h:558` |
| `/api/mqtt/config` | GET | — | `200` JSON (see §4.1) | Current config; password redacted. `web_routes.h:484` |
| `/api/mqtt/config` | POST | form: `host`, `port`, `user`, `pass`, `allow_unauth`, `discovery`, `base` | `200 "OK"` / `400` | Saves & applies. `host`/`port` required. Empty `pass` keeps existing. See §4.2. `web_routes.h:501` |
| `/api/mqtt/test` | POST | form: `host`, `port`, `user?`, `pass?`, `allow_unauth?` | `200 "OK"` / `400` / `502 "TCP connect failed"` / `401 "MQTT auth failed (state N)"` | Connectivity test; **does not save**. `web_routes.h:575` |
| `/api/mqtt/clear` | POST | — | `200 "OK"` | Clears stored MQTT settings (disable without factory reset). `web_routes.h:542` |
| `/api/mqtt/reconnect` | POST | — | `200 "MQTT reconnection triggered"` | Forces a reconnect, clearing any abort/back-off state. `web_routes.h:567` |

### 4.1 `/api/mqtt/config` GET response

| Field | Type | Meaning |
|---|---|---|
| `host` | string | Broker host |
| `port` | number | Broker port |
| `user` | string | Username (may be empty) |
| `has_pass` | bool | Whether a password is stored (the password itself is never returned) |
| `allow_unauth` | bool | Anonymous connection allowed |
| `discovery` | string | Home Assistant discovery prefix (default `homeassistant`) |
| `base` | string | Base topic (default `wordclock`) |

### 4.2 MQTT config POST validation

- `host` and `port` are required (`400 "host/port required"` otherwise).
- If `allow_unauth` is truthy (`1`/`true`/`on`), any stored `user`/`pass` is
  cleared (explicit anonymous opt-out).
- Otherwise both a username **and** password are required —
  `400 "user/password required unless 'no auth' is checked"`. An existing stored
  password counts (send an empty `pass` to keep it).

---

## 5. Logs & diagnostics

Log retrieval, retention settings, and LED diagnostics.

| Endpoint | Method | Params | Response | Notes |
|---|---|---|---|---|
| `/api/logs` | GET | — | `200` JSON array `[{name, size, date}]` | Lists log files (deduped by date, largest per date). `web_routes.h:715` |
| `/api/logs/summary` | GET | — | `200` JSON `{total_bytes, count}` | Aggregate log size/count. `web_routes.h:770` |
| `/api/logs/settings` | GET | — | `200` JSON `{retention_days, delete_on_boot, level}` | `level` is the numeric `LogLevel`. `web_routes.h:816` |
| `/api/logs/settings` | POST | form: `retention_days`, `delete_on_boot`, `level` | `200` JSON `{"status":"ok"}` | All keys optional; only provided ones applied. `delete_on_boot` truthy = `true`\|`1`. `web_routes.h:827` |
| `/log` | GET | — | `200` text (in-RAM ring buffer, newline-joined) | Recent log lines held in memory. `web_routes.h:696` |
| `/log/download` | GET | `date=YYYY-MM-DD` (query, optional) | `200` file attachment / `400 "Invalid date format"` / `404` | Without `date`, downloads the latest log file. `web_routes.h:950` |
| `/setLogLevel` | ANY | `level=DEBUG\|INFO\|WARN\|ERROR` (query) | `200 "OK"` / `400 "Missing log level"` / `400 "Invalid log level"` | Note: **string** level here (vs numeric in `/api/logs/settings`). `web_routes.h:1658` |
| `/getLogLevel` | GET | — | `200` `DEBUG`\|`INFO`\|`WARN`\|`ERROR` (text) | `web_routes.h:1683` |
| `/api/led/event` | GET | — | `200` JSON `{event: string}` | Current LED status event (see §5.1). `web_routes.h:1002` |
| `/api/diag/led` | POST | JSON body `{indices:[…], color:"RRGGBB"\|"RRGGBBWW"}` | `200 "OK"` / `400` | **Logo only** (`PRODUCT_VARIANT_LOGO`). Empty `indices` clears the override. Up to 4 indices. `web_routes.h:1024` |

### 5.1 `/api/led/event` values

`event` is one of: `FirmwareCheck`, `FirmwareAvailable`, `FirmwareDownloading`,
`FirmwareApplying`, `NtpFailed`, `MqttDisconnected`, `BleProvisioning`,
`WifiManagerPortal`. These mirror the LED status-indicator animations the device
shows on its panel.

---

## 6. System & device control

Power, network, and reset operations. The factory/password endpoints use
**admin auth** or a one-shot token rather than the open `ensureUiAuth()` stub.

| Endpoint | Method | Params | Response | Notes |
|---|---|---|---|---|
| `/restart` | GET | — | `200` HTML (auto-refresh), then reboots | Restarts the device. May clear logs first if `delete_on_boot`. `web_routes.h:1077` |
| `/resetwifi` | GET | — | `200` HTML, then clears Wi-Fi & restarts | Drops Wi-Fi creds; device returns to its AP/portal. `web_routes.h:1104` |
| `/factorytoken` | GET | — | `200` token (text), valid 60 s | **Public.** Issues a short-lived token for a tokened factory reset. `web_routes.h:369` |
| `/factoryreset` | POST | `token=...` (optional) | `200` HTML / `403 "Forbidden"` | **Admin auth OR** a valid `/factorytoken`. Wipes prefs + Wi-Fi, reboots. `web_routes.h:376` |
| `/setUIPassword` | POST | form: `new`, `confirm` | `200 "OK"` / `400` / `500` | **Admin auth.** Min 6 chars; `new` must equal `confirm`. `web_routes.h:413` |

> The device also serves the static web UI (`/`, `/dashboard.html`,
> `/admin.html`, `/mqtt.html`, `/logs.html`, `/update.html`, their `-v2` /
> `-legacy` aliases, plus CSS/JS/i18n/RAL assets). `/admin.html`, `/admin-v2`,
> and `/admin-legacy` require **admin auth**; the rest are open. These are HTML
> assets for the browser UI, not part of the app's JSON/control contract.

---

## 7. Bootstrap / factory firmware (separate role — NOT the app contract)

When a device runs the **bootstrap** firmware (`nextgen-bootstrap`) instead of a
per-device product build, it registers a different route set in
`src/bootstrap_provision.cpp`. This is a one-shot factory/provisioning tool: an
operator picks a product in the browser and the device OTA-installs the matching
product firmware. **The companion app does not talk to these endpoints.** They
are documented here only so tooling can recognise a device that has booted into
bootstrap mode (detect via `/api/firmware/identity` → `role: "bootstrap"`).

| Endpoint | Method | Response | Notes |
|---|---|---|---|
| `/api/products` | GET | JSON | Installable product list. `bootstrap_provision.cpp:340` |
| `/api/provision/channels` | GET | JSON | Channels for the selected product. `bootstrap_provision.cpp:341` |
| `/api/provision/start` | POST | `202` JSON `{state, product, channel}` | Begins provisioning the chosen product. `bootstrap_provision.cpp:342` |
| `/api/provision/status` | GET | JSON `{state, message, product, channel, bytes_done, bytes_total}` | Provisioning progress. `bootstrap_provision.cpp:343` |
| `/api/bootstrap/self-update` | POST / GET | `202` JSON `{state, product, channel}` / `409` | Updates the bootstrap firmware itself. Always `nextgen-bootstrap`/`stable` — no channel selector by design. `bootstrap_provision.cpp:344,348` |
| `/api/firmware/identity` | GET | JSON `{role:"bootstrap", firmware, ui, product_id:"nextgen-bootstrap"}` | Same shape as the per-device probe; only `role` differs. `bootstrap_provision.cpp:354` |

---

## 8. Known limitations / security

1. **No authentication on UI/control endpoints.** `ensureUiAuth()`
   (`src/web_routes.h:134`) is a stub that **unconditionally returns `true`** —
   it never checks credentials and never sends a `401`. Every endpoint guarded
   by it (color, brightness, toggle, night mode, MQTT config, logs, OTA trigger,
   `/restart`, `/resetwifi`, device info, …) is therefore reachable by **anyone
   on the same network**. The device must be treated as **trusted-LAN only**:
   put it on a trusted/segmented network and do not expose its HTTP port to the
   internet or untrusted clients. The app does not need to send credentials and
   should not rely on a `401` to detect "needs auth" — none is ever sent.
   *(Documented as-is; no firmware change made.)*

2. **Only a few endpoints are actually protected.** `ensureAdminAuth()`
   (HTTP Basic auth) guards `/admin.html` (and its `-v2`/`-legacy` aliases),
   `/setUIPassword`, `/syncUI`, and `/api/installBootstrap`. `/factoryreset`
   accepts admin auth **or** a 60-second token from `/factorytoken`. Everything
   else is open. Notably, **destructive** UI-auth endpoints — `/restart`,
   `/resetwifi`, `/checkForUpdate`, `/api/mqtt/clear` — are **not** protected.

3. **MQTT password is write-only over the API.** `/api/mqtt/config` GET never
   returns the stored password (only `has_pass`). `/api/mqtt/config` POST keeps
   the existing password when an empty `pass` is sent. Good — but note the POST
   body itself travels over **plain HTTP** on the LAN.

4. **No transport encryption.** The web server is plain HTTP. All traffic —
   including MQTT credentials posted to `/api/mqtt/config` and the new UI
   password set via `/setUIPassword` — is sent in clear text on the local
   network.

---

## 9. Mismatches / gaps the app must account for

1. **`/toggle` does NOT honour `state=0|1`.** The handler does
   `clockEnabled = (state == "on")` (`web_routes.h:1061`) — the **only** value
   that turns the clock on is the literal `state=on`. `state=1` turns it **off**.
   → The app must send `/toggle?state=on` and `/toggle?state=off`. If the app
   sends `state=1`/`state=0`, the clock will be turned off in both cases.
   (Contrast `/setAnimate`, `/setSellMode`, `/setAutoUpdate`, which all accept
   `on|1|true`.) **Recommend** aligning one way: either fix the app to send
   `on`/`off`, or widen the firmware parse — firmware change is out of scope here.

2. **`/setNightModeConfig` is POST + JSON body, not a query-string GET.** A GET
   with query params will not apply any settings.

3. **`/setHetIsDuration` / `/getHetIsDuration` return `404` on the MINI product.**
   Hide/disable the "HET IS" control when `product == "mini"`.

4. **Logo endpoints (`/logo/state`, `/api/diag/led`) only exist on the logo
   product.** On mini/nextgen firmware they return `404`. Gate the app's logo UI
   on `product == "logo"`.

5. **OTA endpoints only exist on OTA builds.** `/api/update/*`, `/getAutoUpdate`,
   `/setAutoUpdate`, `/checkForUpdate`, `/api/installBootstrap` return `404` when
   `OTA_ENABLED` is off. `/uploadFirmware` / `/uploadFs` require
   `UPDATE_UPLOAD_ENABLED`; `/syncUI` requires `SUPPORT_OTA_V2 == 0`.

6. **`/api/ota/manifest` is not a device endpoint.** The OTA manifest lives on
   the remote OTA server, not on the device — see §3.

7. **Log level is represented two different ways.** `/setLogLevel` /
   `/getLogLevel` use **string** levels (`DEBUG`…`ERROR`), while
   `/api/logs/settings` uses the **numeric** `LogLevel`. Use the matching form
   per endpoint.

8. **No authentication.** See §8.1 — `ensureUiAuth()` is a stub. The app does not
   send credentials and the device offers no protection beyond local-network
   trust.
