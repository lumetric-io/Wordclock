# Roadmap

Park-it items: ideas surfaced during work but deferred. Not commitments — a
place to capture context so the idea isn't lost when the conversation ends.

## Color picker — extend input methods

Surfaced 2026-05-10. Today the v2 swatch palette + native "Custom…" picker
covers the common case; these are extensions for power users.

- **Hex text field** (`#RRGGBB` / `RRGGBB`). Cheapest to ship — server's
  `/setColor?color=…` already accepts any hex. ~30 lines of JS in
  `data/dashboard-v2.html` (and `data/admin-v2.html`) next to the
  "Custom…" button. Validate, then call existing `applyColor(hex)`.
- **RGB triplet** (`r,g,b` 0–255). Three small numeric fields, convert
  client-side to hex, reuse `/setColor`.
- **RAL Classic dropdown / search** (~213 codes). Ship a hardcoded
  `RAL → hex` table in JS (~5–8 KB minified). Scope to RAL Classic only;
  RAL Design (~1,825) and RAL Effect (490) are out of scope.

**Caveat to surface in the UI when RAL is added**: WS2812B addressable
LEDs have a narrower gamut than print/coating systems. Most RAL codes
won't reproduce accurately under emissive light — especially matte
coatings. Mark RAL input as "preview only — LED gamut differs from
coating" so customers don't expect an exact match.

## Multi-language — language & dialect picker in the dashboard

Surfaced 2026-08-09, after the firmware side landed. The device can already do
this; nothing here needs a firmware change.

**What exists.** `GET/POST /api/language` and `GET/POST /api/dialect`
(`app-device-api.md` §1.3). A language is a physical front plate, so switching
it reboots; a dialect is a second way of reading the same plate and applies
live. German ships two dialects on one plate — `de-nord` ("viertel nach zehn")
and `de-sued` ("viertel elf"). Dutch has exactly one, so the same UI serves
both: the dialect section simply has a list of length 1 and collapses.

**What's missing.** No way to reach any of it from a browser. Today it's curl
only.

- **Where**: the Display tab of `data/dashboard.html`, next to the animation
  setting. *Not* `admin.html` — that page is behind `ensureAdminAuth()` (real
  HTTP Basic), so a customer would need the admin password to change their own
  clock's dialect, and it's the page holding factory reset and bootstrap
  re-install. Dialect belongs with brightness, colour and night mode. Put it in
  one page only; two places writing the same NVS key will diverge.
- **Build it data-driven.** `/api/dialect` returns `id`, `label` and `sample`
  per dialect — render whatever the device reports, never a hardcoded list. A
  Dutch product then exercises the same code path as a German one, so the
  single-language case can't rot.
- **Pick on the sample sentence**, not the label: "ES IST VIERTEL NACH DREI" vs
  "ES IST VIERTEL VIER" tells a customer something; "Hochdeutsch" vs "Süd-Ost"
  does not.
- **Preview live.** A dialect change needs no reboot, so the physical clock can
  follow the radio button while the customer is choosing. They look at the
  wall, not the screen.
- **Language change reboots** — warn, then poll `/api/firmware/identity` until
  the device is back, and confirm afterwards ("your clock now shows German —
  does that look right?") with a way back.
- Ships as an `fs.bin` OTA (it's `data/`-only), not an app-only update.
- Still open elsewhere: `data/i18n/de.json` + a `DE` entry in the language
  pill, in the Sie-form used by the website's `de.json`. UI language stays
  separate from clock language.

Independent of the display gate (that's a later, telemetry-gated phase); this
picker can ship as soon as it's built. Full design: `multi-language-design.md`
§6 and §12 (untracked — `.gitignore` whitelists only a few `*.md`).

## Security hardening — full-repo review findings

Surfaced 2026-07-04 from a full firmware security review. The device's whole
posture assumes "trusted-LAN only," and several of those assumptions are
broken. Line numbers verified against the tree at that date; re-check before
acting. Ordered by urgency. The recurring root cause is treating "on the LAN"
as equivalent to "authenticated."

### P0 — Critical

- **Unsigned OTA over plain HTTP = fleet-wide RCE.** `src/ota_updater.cpp`:
  `verifySha256()` is a stub (`return true;`, ~line 86); the manifest `sha256`
  is only `logDebug`-logged, never compared (~865–872); base URL is
  `http://ota2.chronolett.com` (`include/secrets.h:9`); `performHttpOta`
  streams straight into `Update.writeStream()`. Anyone who can answer for that
  host (rogue AP, ARP/DNS spoof, upstream hop, compromised OTA server) serves
  malicious firmware and claims a higher version (`isVersionNewer()` trusts the
  attacker's version string). `installProductFirmware()` skips even the version
  check. **Fix (layered):** HTTPS + pinned CA; verify manifest SHA-256 over the
  streamed bytes and abort on mismatch; sign image/manifest (Ed25519 pubkey
  compiled in) and verify before commit; enable ESP32 Secure Boot v2. SHA over
  HTTP alone is worthless — attacker controls both manifest and image.
- **Unauthenticated firmware/FS upload.** `src/web_routes.h:1183`
  `/uploadFirmware`, `:1224` `/uploadFs` — guarded only by the no-op
  `ensureUiAuth()`. `curl -F` flashes attacker firmware. Gated on
  `UPDATE_UPLOAD_ENABLED` (confirm which products enable it). **Fix:** gate with
  `ensureAdminAuth()`. Note: on ESP32 `WebServer`, returning early from the
  upload callback does not abort an in-progress `Update.write()` — refuse to
  `Update.begin()` / call `Update.abort()` when unauthorized.
- **`ensureUiAuth()` is a stub returning `true`** (`src/web_routes.h:134`) — the
  entire HTTP control surface is open on the LAN (color, brightness, toggle,
  night mode, MQTT config, `/restart`, `/resetwifi`, `/checkForUpdate`, logs).
  The stored UI password, `mustChange` flag, and `/setUIPassword` flow are dead
  code, yet comments still say "protected." **Fix:** wire it to validate stored
  `uiAuth` (and hash the secret — currently plaintext NVS), or move destructive
  endpoints to `ensureAdminAuth()` and delete the misleading dead code.

### P1 — High

- **Public `/factorytoken` makes tokened factory reset unauthenticated.**
  `src/web_routes.h:369`/`:376`. A LAN attacker fetches a token then POSTs it to
  wipe NVS + Wi-Fi and reboot into AP mode — de-provisions any clock. Token
  crypto is fine; the flaw is self-service issuance. Token also isn't
  invalidated after use (replayable within 60 s). **Fix:** remove the public
  path; admin auth / serial-only / physical-button; clear token after use.
- **TLS cert validation disabled everywhere (`setInsecure()`).**
  `device_registration.cpp:27`, `heartbeat.cpp:165`, `ota_updater.cpp:558,589,650`.
  Any MITM cert is accepted → leaks the fleet provisioning key and per-device
  bearer token; another RCE on the legacy OTA path. **Fix:** `setCACert()`
  pinning the real CA, compiled in.
- **Static shared secrets in every binary.** `include/secrets.h`: fleet-wide
  `ADMIN_PASS`, `REGISTER_API_TOKEN`, `OTA_PASSWORD "wordclockota"`,
  `BOOTSTRAP_WIFI_PASSWORD`, `UI_DEFAULT_PASS "changeme"`. `secrets.h` is
  correctly gitignored, but the `#define`s ship in `firmware.bin`, which
  `release.sh` uploads as GitHub release assets; no flash encryption.
  `strings firmware.bin` yields the fleet-wide admin password. **Fix:**
  per-device provisioned credentials; enable flash encryption; rotate all
  shipped values (treat as burned).
- **Destructive endpoints respond to GET → drive-by CSRF.** `/restart` (:1077),
  `/resetwifi` (:1104), `/checkForUpdate` (:1266, `HTTP_ANY`), `/setLogLevel`
  (:1658). A page loaded on the LAN fires `<img src=".../resetwifi">` with no
  interaction. **Fix:** POST-only + CSRF token / same-origin check; combine with
  the auth fix above.
- **Open (passwordless) Wi-Fi config portal.** `network.cpp:66,131`,
  `AP_PASSWORD ""`. Attacker forces a disconnect, joins the open
  `Wordclock_AP` at 192.168.4.1, reconfigures onto an attacker SSID. **Fix:**
  non-empty per-device AP password (derived from hardware ID / on the label).

### P2 — Medium

- **MQTT is plaintext-only** (`mqtt_client.cpp:30`, bare `WiFiClient`); broker
  user/pass sent in the clear. Add a `WiFiClientSecure` + CA-pin option (8883).
- **Admin auth is HTTP Basic over cleartext HTTP** (`web_routes.h:125`);
  sniffable, and shared fleet-wide per the secrets item. Consider session token
  + TLS; per-device secret.
- **`/api/mqtt/test` unauthenticated connect-probe** (`web_routes.h:575`) —
  attacker-supplied host/port, internal port-scan primitive. Admin-gate it.
- **MQTT password + Wi-Fi PSK stored unencrypted in NVS**
  (`mqtt_settings.cpp:47`). Flash dump reveals them. Enable flash encryption.
- **CI "Test Summary" always prints success** (`.github/workflows/tests.yml:52`)
  — `if [ $? -eq 0 ]` checks the preceding `echo`, not the test step. Gate on
  the test step's `outcome`. (Job still fails correctly; only the summary lies.)
- **`secrets_template.h` missing** though `CLAUDE.md` references it — a new
  contributor may mis-roll or copy the real `secrets.h`. Add a placeholder.

### P3 — Low / latent / quality

- **BLE provisioning has no link auth** (`ble_provisioning.cpp:437`); the grid
  passkey binds to nothing. Latent — `BLE_PROVISIONING_ENABLED` is `0` in all
  products. **Must be fixed before any product enables BLE** (require bonding +
  passkey, encrypt the credential characteristics). Related: latent
  `g_ssid`/`g_pass` race between the BLE task and main loop.
- **`RELEASE_CHANNEL=\"$CHANNEL\"`** sets a quoted literal (`release.sh:1062`)
  so `channel == 'develop'` never matches — masked today by an earlier guard.
- **Logo bounds checks key off `getLogoLedCount()` not `LOGO_LED_STORAGE_COUNT`**
  (`logo_leds.cpp`) — safe only because both equal 52. Add
  `static_assert(LOGO_LED_COUNT <= LOGO_LED_STORAGE_COUNT)`.
- **`eval` in shell tooling** (`release.sh:1476`, `flash.sh:112`) — no
  untrusted-input path, but breaks on paths/versions with spaces. Use arrays.
- **Dev-tooling hygiene** (`tools/provision_ble.sh`): Wi-Fi password as CLI arg
  (visible in `ps`/history); predictable world-readable `/tmp/wordclock_info.json`
  (use `mktemp` + `chmod 600`).
- **Minor correctness nits (non-security):** dead `rounded_minute == 60` branch
  (`time_mapper.cpp:44/128`); 7-char hex mis-parse in `/api/diag/led`
  (`web_routes.h:1037`); additive `millis()` wrap on `hetIs_.visibleUntil` after
  ~49.7 days; CI actions pinned to mutable tags rather than commit SHAs.

### Verified safe (not issues, recorded to avoid re-litigating)

LED-buffer writes are centrally bounds-checked (`clockSetPixel`/`logoSetPixel`)
— `/api/diag/led` and `/logo/state` traced end to end, no OOB. `/log/download`
strictly validates `date` (no path traversal). MQTT password never returned by
the API (`has_pass` only). NVS values clamped on read. Bootstrap credential
isolation correct (`WiFi.disconnect(true,true)` + `WiFi.persistent(false)`). No
`strcpy`/`sprintf`/format-string misuse found.

### Suggested order of work

1. OTA authentication (signed images + HTTPS/cert-pinning + real SHA check).
2. Auth on the control surface (real `ensureUiAuth()` or admin-gate destructive
   endpoints; kill the public factory-reset token; POST-only + CSRF).
3. Cert pinning + per-device secrets (removes MITM and shared-secret amplifiers).
4. P2 transport/config items, then quality fixes.
