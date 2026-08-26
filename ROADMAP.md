# Roadmap

Park-it items: ideas surfaced during work but deferred. Not commitments — a
place to capture context so the idea isn't lost when the conversation ends.

## Color picker — extend input methods

Surfaced 2026-05-10. Today the swatch palette + native "Custom…" picker
covers the common case; these are extensions for power users.

- **Hex text field** (`#RRGGBB` / `RRGGBB`). Cheapest to ship — server's
  `/setColor?color=…` already accepts any hex. ~30 lines of JS in
  `data/dashboard.html` (and `data/admin.html`) next to the
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

Surfaced 2026-08-09, after the firmware side landed. The picker itself needed no
firmware change; splitting the German dialect into two questions did (below).

**What exists.** `GET/POST /api/language` and `GET/POST /api/dialect`
(`app-device-api.md` §1.3). A language is a physical front plate, so switching
it reboots; a dialect is a second way of reading the same plate and applies
live. German ships four dialects on one plate; Dutch has exactly one, so the
same UI serves both: the dialect section simply has a list of length 1 and
collapses.

**Built 2026-08-09** — section `10 Language`, last in the Display tab of
`data/dashboard.html`, plus `dashboard.language.*` in `en.json`/`nl.json`. The
notes below record why it is shaped the way it is.

**Verified on real hardware 2026-08-12** — a `nextgen-50x50` running
`26.08.12-dev.1` switched from Dutch to German through the picker and the whole
chain followed: NVS write, reboot, grid variant swap to `DE_50x50_V1`, and a
heartbeat reporting `lang=de dialect=de-nord langSrc=user` — up from
`nl/nl/migrated`, which is exactly the transition the display gate's release
criterion is built on. First time the multi-language path has run outside a
test.

One false alarm on the way, worth knowing before it wastes an hour again: the
picker appeared to be missing entirely. It was not. `curl` showed the device
serving the new `dashboard.html` while the browser rendered a cached copy; a
hard refresh fixed it. The `ui` version in the heartbeat cannot rule this out
either — `getUiVersion()` falls back to the compiled `UI_VERSION` whenever
`/.fs_image_version` is absent, which is the case after any USB `uploadfs`,
so new-app-with-stale-files still reports the new UI version. See the cache
headers section below; that is the real fix.

**Built 2026-08-14 — the German dialect is two questions, not one.** The plate
varies along two axes that speakers mix freely: the quarter hours (`viertel nach
zehn` vs `viertel elf`) and :20/:40 (`zwanzig vor elf` vs `zehn nach halb elf`).
Offering them as one four-way list asks the customer to find their own speech in
a sentence pair, and only two of the four combinations even had a rule table
before this. Now:

- Two new `PhraseRules` tables complete the cross-product: `de-nord-halb` and
  `de-sued-zwanzig`. Each dialect stays one **complete** table — the engine
  composes nothing at runtime, so `test_phrase_rules` keeps asserting exactly
  what renders, and a table asking for a word the plate lacks still fails.
- A variant may declare `DialectAxis[]`; each dialect carries `axisValues`, one
  per axis. The mapping is asserted **total and unique** in `test_language`,
  which is what makes the parallel array safe.
- `GET /api/dialect` grew an `axes` array; `POST` accepts `axis=&value=` beside
  the existing `dialect=`. Dialect ids, NVS, the field migration, the heartbeat
  and Home Assistant are all unchanged — a client that ignores `axes` behaves
  exactly as before. The dashboard renders one radio group per axis and falls
  back to the flat list when a device reports none.

Open: whether all four readings are attested and whether "Gemischt (nach)" /
"Gemischt (viertel)" are the right labels. Folded into the native-DE review
already pending for the website; the axis machinery does not depend on the
answer, only the four labels do.

**Decided 2026-08-09 — every language stays switchable.** `OFFERED_LANGUAGES`
is `null`, so the picker offers whatever the firmware reports, German included
on `nextgen-50x50`. The reasoning: the customer picks at setup, but when that
goes wrong — wrong pick, wrong plate shipped, someone else did the setup — they
have to be able to fix it themselves instead of opening a support ticket. That
outweighs hiding German from a Dutch owner who has no reason to choose it. The
section is placed last in the tab rather than first for the same reason:
reachable, not prominent. Set `OFFERED_LANGUAGES` to a list if a language ever
does need holding back; the active language is always offered even when
filtered out, so a device switched over by hand can never get stuck.

Still open after it:

- **"Kiezen op beeld"** (design doc §6) — instead of picking from the *names*
  "Nederlands / Deutsch", show a thumbnail of each front plate's actual letter
  grid, so the customer matches what they see on the wall rather than trusting
  a label. It is the difference between "which language is this?" and "which of
  these two pictures is your clock?", and it is the version that survives a
  customer who doesn't know the word "dialect". Needs `/api/language` to return
  the letter grid per language — a firmware change, so out of scope for a
  `data/`-only ship.
- **German dashboard** (`data/i18n/de.json` + `DE` in the pill). Deferred
  2026-08-09: a German customer can run the dashboard in English for now. The
  clock's language and the dashboard's language are separate settings and
  always were.

How it is shaped, and why:

- **Where**: the Display tab of `data/dashboard.html`, last section in the tab.
  *Not* `admin.html` — that page is behind `ensureAdminAuth()` (real
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
  does not. Same rule per axis option — each carries its own sample, covering
  only what that axis decides, so the two questions stay separable.
- **Ask one question per independent decision.** The device decomposes its own
  dialects; the UI never hardcodes which axes exist or resolves a combination
  itself — it posts one axis and adopts the reading the device settles on.
- **Preview live.** A dialect change needs no reboot, so the physical clock can
  follow the radio button while the customer is choosing. They look at the
  wall, not the screen.
- **Language change reboots** — warn, then wait the device out and confirm
  afterwards ("your clock now shows German — does that look right?") with a way
  back. As built it polls `/api/device/info` and accepts either signal the
  firmware updater uses: the device went unreachable, or `uptime_ms` ran
  backwards. One alone can be missed.
- Ships as an `fs.bin` OTA (it's `data/`-only), not an app-only update.

Independent of the display gate (that's a later, telemetry-gated phase); this
picker can ship as soon as it's built. Full design: `multi-language-design.md`
§6 and §12 (untracked — `.gitignore` whitelists only a few `*.md`).

## `wordclock.local` never comes back after a Wi-Fi-less boot

Found 2026-08-17, on a 50x50 that had just been flashed. Symptom: the clock kept
showing the time, `Wordclock_AP` was being broadcast, and the dashboard was
unreachable. A reboot fixed it.

The chain, all of it existing behaviour:

1. The Wi-Fi connect at boot failed. Common right after a flash — the access
   point still holds the previous association and the radio comes up cold. On
   that path `wm.autoConnect()` returns false and the config portal opens
   **immediately** (`network.cpp:131`), not after the 300 s fallback.
2. The display is independent of Wi-Fi, so the clock kept telling the time. That
   is what makes this look like a UI bug rather than a network one.
3. `runtimeInitOnSetup()` saw no Wi-Fi and left `g_serverInitialized = false` —
   no web server at all yet.
4. `MDNS.begin()` ran regardless, at `main.cpp:73`, with no STA interface.
5. Wi-Fi came back on one of the 60 s retries. `runtimeEnsureOnlineServices()`
   then started the web server, MQTT, the heartbeat and registration, and the
   loop stopped the portal — **but nothing re-registers mDNS**. It is called
   exactly once, in `setup()`, and never again.

So the device was on the LAN and serving the dashboard on its IP the whole time,
while `wordclock.local` stayed dead for the rest of that uptime. Every recovering
service recovers except the name you reach it by. The reboot worked because that
boot's connect succeeded, so mDNS registered against a live interface.

This is worse on a customer than it was here: they have no IP to fall back on,
the clock looks healthy on the wall, and the only advice that works is "pull the
plug". It also fires on any Wi-Fi outage long enough to bounce the device.

**Fixed 2026-08-17.** mDNS now recovers the way every other online service
already did — `startMdns()` / `ensureMdns()` in `runtime_services.cpp`:

- The `MDNS.begin(MDNS_HOSTNAME)` call moved out of `setup()` into
  `runtimeInitOnSetup()` and `runtimeEnsureOnlineServices()`, behind
  `g_mdnsRegistered`, next to `g_serverInitialized`. It registers on the first
  boot *with* a connection, and after any reconnect that follows a boot without
  one. Both call sites only ever run with Wi-Fi up.
- The disconnect edge in `runtimeHandleWifiTransitionLogs()` clears the flag.
  That edge is the only place a drop is visible at all —
  `runtimeEnsureOnlineServices()` returns early while offline and cannot tell a
  first boot from a return. The responder is bound to the STA interface and the
  address can change, so re-registering beats assuming it survived.
- `MDNS.end()` before a re-`begin()`, but **not on the first pass**: ArduinoOTA
  has already brought the mDNS stack up and registered `_arduino._tcp` by then,
  and tearing it down would take OTA discovery with it.
- Failure leaves the flag false so the loop retries, throttled by
  `MDNS_RETRY_INTERVAL_MS` (10 s). Registration runs from the loop now, so
  without a gap a failure would be retried — and logged — every tick.
- Added `MDNS.addService("http", "tcp", 80)`. `bootstrap_main.cpp` did this and
  the per-device firmware did not — an inconsistency with no reason behind it.
  Hostname lookups work without the service record; browsing `_http._tcp` does
  not.

To verify without waiting for a bad boot: connect the clock, force a
disconnect/re-associate (drop the AP, or `WiFi.disconnect()`), and check that
`wordclock.local` resolves again afterwards. The serial log prints
`🌐 mDNS active` on each successful registration, so a reconnect should produce a
second line where it previously produced none.

**`nextgen-bootstrap` fixed the same day, separately.** It has its own
`build_src_filter` and compiles neither `main.cpp` nor `runtime_services.cpp`,
so it could not call `ensureMdns()` — the logic is duplicated in
`bootstrap_main.cpp` (`startMdns()` / `serviceMdns()`), sharing only
`MDNS_HOSTNAME` and `MDNS_RETRY_INTERVAL_MS` from `config.h`. Two differences
from the per-device version, both because bootstrap's shape is different:

- Its boot cannot reach mDNS without a link at all: `connectWifi()` blocks until
  the factory creds or the portal succeed, and restarts otherwise. So the
  Wi-Fi-less-boot path above does not exist there. What did was the rest of it —
  a `begin()` that failed was never retried, and a link that dropped mid-session
  left the responder dead for the remaining uptime.
- There is no reconnect machinery to hang a disconnect edge on, so `serviceMdns()`
  polls `WiFi.status()` from `loop()` itself, throttled to 1 s (the loop spins
  every 2 ms, and provisioning downloads run on their own task). `MDNS.end()` is
  still first-pass-guarded, though only for tidiness — bootstrap runs no
  ArduinoOTA, so there is no `_arduino._tcp` to protect.

This matters more in the factory than on a wall: the operator never sees the
DHCP lease, so `http://wordclock.local/` is the *only* route to the product
picker. A dead responder there means a power-cycle before every retry.

## Static assets are served with no cache headers

Found 2026-08-12, while the language picker appeared to be missing on a
freshly-updated 50x50. It was not missing: the device was serving the new
`dashboard.html` (confirmed with `curl`) while the browser rendered a cached
copy. A hard refresh fixed it.

`serveFile()` (`src/web_routes.h:97`) sends no `Cache-Control`, no `ETag` and no
`Last-Modified`. With no validators at all a browser is free to cache
heuristically and reuse without asking, so **any** `fs.bin` OTA can leave a
customer looking at the previous UI with no indication anything is stale. This
one cost a debugging session; on a customer it costs a support ticket, and the
symptom points at the feature rather than at the cache.

**Fixed 2026-08-18**, in `serveFile()` so every static route inherits it:

- `Cache-Control: no-cache` — revalidate every time. Note this is *not*
  `no-store`; the copy stays usable, the browser just has to ask.
- `ETag: "<ui version>-<file size>"`, and answer `If-None-Match` with `304`.
  The UI version already changes on every `fs.bin` install, so the tag turns
  over exactly when the files do.

Without the ETag half, `no-cache` means re-downloading 92 KB of
`dashboard.html` on every load; with it, an unchanged page costs one round trip
and no body. The version string is read once via `cachedUiVersion()` rather
than per request — `getUiVersion()` opens a file on LittleFS, and the value is
a boot constant anyway since installing an `fs.bin` reboots the device.

Two things that are easy to get wrong here, both handled:

- **`collectHeaders` is not optional.** `WebServer` discards every request
  header it was not told to keep, so without adding `If-None-Match` beside the
  existing `Accept-Encoding` the conditional branch would never fire and every
  revalidation would still cost a full body — the slow half of the fix with
  none of the fast half.
- **The 404 path gets no validators.** There is nothing to revalidate against,
  and tagging a miss would let a browser hold on to it.

The gzip branch tags the `.gz` file's own size, so the two encodings can't
collide on one ETag. It is dead code today (`acceptGzip` is hardcoded false)
but the fallback-to-`.gz`-when-plain-is-missing path is live.

Not covered by any native test — nothing mocks `WebServer`, so `pio test -e
native` proves only that the rest still builds. Verify on device with
`./tools/check-cache-headers.sh <host-or-ip>`: it checks the `200`+`ETag`+
`no-cache` response, the `304`-with-no-body on `If-None-Match`, that a *stale*
tag still gets a full `200`, and that a `404` carries no validators.

**The first load after installing this will still look stale.** A browser that
cached a page before this shipped holds a copy stored with no validators, so it
may reuse it without asking and never see the new headers. One last hard
refresh clears it; every response after that carries validators, so future
`fs.bin` releases revalidate properly. The fix prevents future staleness, it
cannot cure copies already sitting in a cache — verify with the script, not a
browser.

## A heartbeat that lands is reported as failed, and freezes the display for 15 s

**Timeout half fixed 2026-08-17 (not yet built or flashed). The duplicate row
is deliberately left alone — see the end.**

Found 2026-08-17 on the 50x50 dev clock (`f316287d`), right after flashing
`26.08.17-dev.3`:

```
[21:35:13][WARN] 💓 HTTP error: read Timeout
[21:40:01][INFO] 💓 Heartbeat sent successfully
[21:40:01][WARN] Anim step 2/6 dt=982ms (Δ2 leds) ⚠️ slow
```

**The "failed" heartbeat had landed.** Row written server-side at 19:34:58 UTC;
the device gave up at 19:35:13 — exactly the 15 s from `http.setTimeout(15000)`
(`src/heartbeat.cpp:176`). The request arrived and was processed, only the
response never got back in time. The portal answers that endpoint in ~0.09 s
including TLS, so this is not server latency.

Cause is the radio, not the firmware. That heartbeat carried **RSSI −91 dBm**;
every other beacon from that clock sits between −66 and −70, and the retry five
minutes later was −69. Across the fleet it is the one device whose average
(−69) and minimum (−91) are 22 dB apart — every other clock stays within a few
dB of its own average. The link is not weak, it is unstable.

Not heap: `freeHeap` 221 KB and `minFreeHeap` 182 KB on the failing beacon, in
line with every other one. Ruled out.

Two consequences, in order of how much they matter:

1. **The display freezes.** `sendHeartbeat()` runs synchronously in `loop()`.
   A *successful* one already costs ~1 s — that is what the `Anim step` warning
   above is, logged in the same millisecond as the heartbeat. On a timeout the
   animation stalls for the full 15 s, visible on a clock on someone's wall.
   The `:30 s`-only send window (`isAtHalfMinute()`) shows the conflict was
   known; the size of it on the failure path was not.
2. **A duplicate row per dip.** The device sets `s_lastFailureMs`, retries
   after `HEARTBEAT_RETRY_INTERVAL_MS`, and writes a second row for the same
   beacon — confirmed: 19:34:58 and 19:40:01, 5.0 min apart, both with
   `log_level=info`. The hourly cadence had been exactly 60.0 min for hours
   before this. With the server's 100-rows-per-device live ring, dips shorten
   the retained history.

**Fix applied for (1)**: the single 15 s `setTimeout()` is now two bounds,
`HEARTBEAT_CONNECT_TIMEOUT_MS 10000` and `HEARTBEAT_READ_TIMEOUT_MS 5000`. The
two waits are not the same risk. Connect covers DNS + TCP + the TLS handshake —
the slow part on an ESP32, and the part that genuinely needs room on a weak
link, so it keeps a generous bound. Read is the wait for a response to a request
already sent, and the portal answers in well under a second, so 5 s is already
far past hope. Collapsing both to 5 s would have traded one bug for another:
handshakes that used to succeed slowly would start failing.

Worst case is now 10 s rather than 15, and the common case — the dip observed
here, where the request got through — is 5 s. Anything better means getting the
HTTPS call off the render loop, which is a much larger change and not scheduled.

`src/ota_updater.cpp` keeps its six 15 s timeouts on purpose: an OTA is a
deliberate, foreground action where a stalled display is expected, and its
transfers really can take that long.

(2) is not worth fixing properly — an idempotency key on the heartbeat is out of
proportion to a duplicate row now and then. Worth knowing when reading fleet
data: heartbeat gaps well under 60 min are the retry path, not a reconfigured
device.

Not yet compiled: no native test covers `heartbeat.cpp` (it needs the Wi-Fi
stack), so the next `pio run` is what proves `setConnectTimeout()` builds
against espressif32@6.4.0.

## The log file sink stops writing and never comes back

**Fixed structurally 2026-08-22 (not yet built or flashed). The trigger is
still unknown, which is why this stays here.**

Found on the 50x50 dev clock (`f316287d`, `26.08.21-rc.3`) by
`tools/test-log-features.sh`, written the same day to validate the log
features the P4.10 fleet commands drive.

The clock wrote its last line to `/logs/2026-08-22.log` at 00:43:29 local and
wrote nothing for the next eight hours. Everything else looked perfect: the RAM
ring buffer behind `/log` had every line, `/api/logs/settings` reported
`level 0`, `delete_on_boot false`, `retention 3`, and every hourly heartbeat
arrived. A probe line written over HTTP at 08:33:03 appeared in `/log` and
moved the file by zero bytes. A restart brought the sink straight back
(15705 to 20225 bytes).

**Why it could not recover.** `ensureLogFile()` set `fileSinkEnabled = false`
on a failed open, and nothing anywhere set it back: the only re-arm was
`logEnableFileSink()`, which runs at boot and from the clear-logs route. One
bad open at 00:43 therefore disabled file logging until someone rebooted the
clock. Separately, `log()` ignored the return of `logFile.print()`, so a write
that landed short was indistinguishable from one that worked.

**What the fix does.** `src/log_sink_health.h` replaces the one-way latch with
a small state machine: a failed open or a short write marks the sink unhealthy
and schedules a retry 60 s later, and a successful open clears it. The retry
comparison is `(long)(nowMs - retryAtMs) >= 0` because `millis()` wraps every
49.7 days and a clock up that long is exactly the one being asked to log an
intermittent fault. Failures are counted per incident, not per attempt, so a
sink that is down does not add one to the count every minute. Nine native tests
in `test/test_log_sink_health/`; the header has no hardware dependencies, so
what the tests run is what runs on the clock.

**What the fleet gets.** Three heartbeat fields, `logSinkOk`,
`logSinkFailures` and `logBytes` (lumetric `portal/sql/019`, P4.11 in
`infra/ROADMAP.md`). Without them a clock in this state reports healthy in
every respect and all three log commands close green against it, which is what
made this take eight hours to notice. `logSinkFailures` exists because the
retry now makes "dropped out and came back" the common case, and that is
invisible in a boolean sampled hourly.

**Still open: what actually failed at 00:43.** Nothing in the logs, because the
thing that broke is the log. What the recovered files do say is precise, and it
points at the rollover rather than at anything the clock was doing at the time:

- `2026-08-21.log` ends at 23:43:28, the hourly heartbeat.
- `2026-08-22.log` is 190 bytes and contains exactly two lines, the 00:43:28
  and 00:43:29 heartbeat pair, and nothing else ever.

So the day tag changed at 00:43:28, `ensureLogFile()` closed the old file, ran
the retention prune, opened the new one, and both lines were written *and
flushed*. The sink died between then and the 01:43 beat. That rules out the
open itself and leaves the two paths the fix now covers: either the handle went
falsy and every reopen failed (old code: one failure, latched off forever,
silent), or the handle stayed truthy and `print()` returned short (old code: no
one looked). It also puts the prune loop in the frame, since deleting entries
with `FS_IMPL.remove()` while iterating the same open directory handle runs
once per tag change and had just run.

Two hypotheses were tested against the live clock and both refuted: reading the
open file over `/log/download` while it is being appended to does not kill the
sink (5221, 5291, 5361 bytes, growing each time), and 160 filesystem-touching
requests in a burst do not either.

`logSinkFailures` on the fleet is the instrument for the rest. If it climbs,
and especially if it climbs at a clock's local 00:xx, the prune loop is where
to look next. If it never appears again, the retry has already made this a
non-event.

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
