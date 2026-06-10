# App ↔ Device HTTP API contract

Single source of truth for the endpoints the companion app's native **Settings**
screen calls on a Wordclock device over its local HTTP server.

- **Base URL:** `http://<device-ip>/` (device's local web server).
- **Auth:** every handler calls `ensureUiAuth()`, which currently **always returns
  `true`** (`src/web_routes.h:134`). No credentials are enforced today; treat the
  device as trusted-LAN only. Don't rely on a 401 — none is sent.
- **Source:** all per-device routes are registered in `src/web_routes.h`
  (`registerWebRoutes`). Bootstrap-firmware routes (`src/bootstrap_provision.cpp`)
  are a different firmware role and are **not** part of this app contract.
- Responses are `text/plain` unless noted as `application/json`.

> **Location note:** the firmware repo's `docs/` is an uninitialized git submodule
> (`chronolett-firmware-docs`) and `.gitignore` excludes all `*.md`, so this contract
> is kept at the repo root and whitelisted in `.gitignore`. Mirror it into the `docs`
> submodule when that repo is available.

## Endpoints the app uses

| Endpoint | Method | Params | Range / format | Response | Notes |
|---|---|---|---|---|---|
| `/api/device/info` | GET | — | — | `200` JSON (see below) | Device telemetry + identity. `web_routes.h:857` |
| `/setColor` | GET | `color=RRGGBB` (query) | 6 hex digits; non-hex chars stripped, then must be exactly 6 | `200 "OK"` / `400 "Missing color"` / `400 "Invalid color"` | Sets clock RGB, refreshes display. `web_routes.h:1122` |
| `/getColor` | GET | — | — | `200` `RRGGBB` (white → `FFFFFF`) | Read-back companion to `/setColor`. `web_routes.h:1160` |
| `/setBrightness` | GET | `level=N` (query) | `0`–`255` (`constrain`) | `200 "OK"` / `400 "Missing brightness level"` | Clock LED brightness. `web_routes.h:1343` |
| `/getBrightness` | GET | — | — | `200` `N` (0–255) | `web_routes.h:1334` |
| `/setAnimate` | GET | `state=...` (query) | truthy = `on` \| `1` \| `true`; anything else = off | `200 "OK"` / `400 "Missing state"` | Word-by-word animation. **Accepts `0|1`.** `web_routes.h:1483` |
| `/getAnimate` | GET | — | — | `200` `on` \| `off` | `web_routes.h:1474` |
| `/toggle` | GET | `state=...` (query) | **only `on` enables**; everything else (incl. `1`, `0`, `off`) disables | `200 "OK"` | Clock on/off. ⚠️ **Does NOT accept `0|1`** — see mismatch below. `web_routes.h:1058` |
| `/setNightModeConfig` | **POST** | **JSON body** (not query) | see Night-mode body below | `200` JSON (same shape as `/getNightModeConfig`) / `400` on invalid field | `web_routes.h:1506` |
| `/getNightModeConfig` | GET | — | — | `200` JSON (see below) | `web_routes.h:1498` |
| `/setHetIsDuration` | GET | `seconds=N` (query) | `0`–`360` (0 = never, 360 = always) | `200 "OK"` / `400 "Missing seconds"` / `404` on MINI | Not supported on `PRODUCT_VARIANT_MINI` (returns `404`). `web_routes.h:1641` |
| `/getHetIsDuration` | GET | — | — | `200` `N` (0–360) / `404` on MINI | `web_routes.h:1628` |

### Logo variants (compiled only when `PRODUCT_VARIANT_LOGO`)

These exist **only** on the logo product (`#if defined(PRODUCT_VARIANT_LOGO)`,
`web_routes.h:1364`). On non-logo firmware the routes are absent → `404`.

| Endpoint | Method | Params | Response | Notes |
|---|---|---|---|---|
| `/logo/state` | GET | — | `200` JSON: `{ brightness, count, start, colors[] }` | `colors[]` = `count` `RRGGBB` strings. `web_routes.h:1365` |
| `/logo/state` | POST | JSON body | `200` JSON (same shape as GET) / `400` | Body keys (all optional): `brightness` (int 0–255), `all` (`RRGGBB`), `colors` (array of exactly `count` `RRGGBB` strings). `web_routes.h:1370` |

## `/api/device/info` JSON fields

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

## Night-mode config (`/setNightModeConfig` POST body / `/getNightModeConfig` response)

`/setNightModeConfig` takes a **JSON body** (`Content-Type` irrelevant, read from
`plain` arg). All keys optional; only provided keys are applied.

| Key | Type | Accepted values | Notes |
|---|---|---|---|
| `enabled` | bool / int / string | bool, `!=0`, or `true`/`on`/`1` | |
| `effect` | string | `off` \| `dim` | else `400 "Invalid effect"` |
| `dim_percent` | int | `0`–`100` (clamped) | |
| `start` | string | `"HH:MM"` (`parseTimeString`) | takes priority over `start_minutes` |
| `start_minutes` | int | `0`–`1439` | used if `start` absent |
| `end` | string | `"HH:MM"` | takes priority over `end_minutes` |
| `end_minutes` | int | `0`–`1439` | used if `end` absent |
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

## ⚠️ Mismatches / gaps the app must account for

1. **`/toggle` does NOT honour `state=0|1`.** The handler does
   `clockEnabled = (state == "on")` (`web_routes.h:1061`) — the **only** value that
   turns the clock on is the literal `state=on`. `state=1` turns it **off**.
   → The app must send `/toggle?state=on` and `/toggle?state=off`. If the app sends
   `state=1`/`state=0`, the clock will be turned off in both cases. (Contrast
   `/setAnimate`, which *does* accept `on|1|true`.) **Recommend** aligning one way:
   either fix the app to send `on`/`off`, or widen the firmware parse to match
   `/setAnimate` — firmware change is out of scope for this doc.

2. **`/setNightModeConfig` is POST + JSON body, not a query-string GET.** The task
   sketch wrote `/setNightModeConfig…`; the real contract requires an HTTP **POST**
   with a JSON body. A GET with query params will not apply any settings.

3. **`/setHetIsDuration` and `/getHetIsDuration` return `404` on the MINI product.**
   The app should hide/disable the "HET IS" control when `product == "mini"`.

4. **Logo endpoints (`/logo/state`) only exist on the logo product.** On mini/nextgen
   firmware they return `404`. Gate the app's logo UI on `product == "logo"`.

5. **No authentication.** `ensureUiAuth()` is a stub returning `true`. The app does
   not need to send credentials, and the device offers no protection — local network
   trust only.

All endpoints listed in the round-2 task exist in firmware. The only true semantic
gap is item #1 (`/toggle` value handling); items #2–#5 are contract clarifications.
