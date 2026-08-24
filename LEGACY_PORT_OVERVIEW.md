# Legacy firmware port overview

Purpose: catalogue every substantive commit made to the wordclock firmware
**since the legacy line was frozen**, and judge how each could be brought back
onto the legacy (ESP32) firmware. This is the planning document that precedes
building a parallel `legacy-main` branch.

Status: DRAFT / planning. Read-only analysis, nothing ported yet.
Repo: `/home/ron/repos/wordclock`. Generated 2026-08-24.

---

## 1. The base and the boundary

| Fact | Value |
|---|---|
| Last legacy commit | `e353160` "Merge fix/tien-m-led-zero-sentinel into main" (2026-05-08) |
| Already branched as | **`legacy/esp32`** (points exactly at `e353160`, 0 ahead / 0 behind) |
| The cut | `53b61ab` "ESP32 -> ESP32-S3 migration: nextgen-* product line, prune legacy" (2026-05-08) |
| Legacy board | `esp32dev` (ESP32), platform `espressif32@6.4.0` |
| Nextgen board | ESP32-S3 |
| Nextgen source of truth today | `feat/device-commands` = HEAD `60060c2` (main `fac6f79` is 21 commits behind it) |
| Substantive commits since the cut | ~81 (172 total minus 77 version-bumps minus 14 merges) |

**Proposed `legacy-main`:** branch off `legacy/esp32` (= `e353160`) and cherry-pick /
re-implement the PORT-verdict commits below.

```
git checkout -b legacy-main legacy/esp32
```

## 2. Structural delta legacy (e353160) vs nextgen (HEAD)

This delta is what decides portability.

**New `src/` files in nextgen, absent from legacy** (must be brought along if a
change depends on them):
- `bootstrap_main.cpp`, `bootstrap_provision.{cpp,h}` - nextgen-bootstrap first-flash firmware (legacy has none)
- `device_commands.{cpp,h}` - fleet downlink command handling
- `language_settings.{cpp,h}`, `phrase_rules.{cpp,h}`, `led_segments.{cpp,h}` - the i18n / phrase engine + data-driven LED segment table
- `log_sink_health.h` - log sink health
- `grid_variants/de_50x50_v1.*`, `grid_variants/nl_105x105_logo_v1.*` - new grids

**Files legacy HAS that nextgen removed:**
- `setup_state.{cpp,h}` - the setup wizard (nextgen removed it, 86ba669)
- old grid variants: `nl_v1/v2/v3`, `nl_50x50_v1/v2/v3`, `nl_20x20_v0`, `nl_100x100_logo_v1`

**Product line:** legacy = `wordclock-legacy*`, `wordclock-logo*`, `wordclock-mini*`;
nextgen = `nextgen-*`. Dashboards that detect product type by `nextgen-*` env
names (ada38ee) do not match legacy env names.

**Provisioning model:** legacy = flash directly + WiFiManager + setup wizard.
Nextgen = nextgen-bootstrap first-flash firmware + factory-wifi + scrubbed NVS.

## 3. Verdict legend

- **PORT** - board-agnostic; applies to legacy `src`/`data` cleanly or with trivial adaptation.
- **ADAPT** - portable but needs real legacy rework (board API, provisioning model, product/grid mapping, a new file brought along, or a UI/branding decision).
- **SKIP** - nextgen-only; does not make sense on legacy (bootstrap firmware, product renames, S3 hardware, grids legacy lacks).

---

## 4. Per-subsystem portability ledger

### 4.1 Connectivity & fleet

| commit | title | files | verdict | reason / legacy-fit | depends on |
|---|---|---|---|---|---|
| `835a40c` | skip render loop + STA scans during initial setup | main.cpp, network.cpp, network_init.h | **PORT** | All files legacy-present, board-agnostic. Foundation for the two WiFi feats. | none |
| `7b11b7e` | re-register mDNS after reconnect | runtime_services.cpp, main.cpp, config.h (+bootstrap) | **PORT** (drop bootstrap hunk) | The customer-facing `wordclock.local`-goes-dead fix. `ESPmDNS` works on ESP32; all hook sites present. Drop the nextgen-bootstrap hunk. | none |
| `13d0fd2` | try factory Wi-Fi before config portal | network.cpp | **SKIP** | Pure nextgen provisioning model (BOOTSTRAP_WIFI_SSID + scrubbed NVS). Legacy has no factory network. Compiles inert, zero value. | 835a40c |
| `499f71e` | enroll Wi-Fi from admin page | network.cpp, network_init.h, web_routes.h, admin.html, i18n | **ADAPT** | Useful on legacy. `esp_wifi_*` / `WIFI_STORAGE_FLASH` exist on classic ESP32 (not S3-only). Strip the `g_usingFactoryWifi` clause (that flag rides 13d0fd2, which we skip). Build-verify. | 13d0fd2 (soft) |
| `040ae60` | report active log level (heartbeat) | heartbeat.cpp, log.cpp/h | **PORT** | Trivial; reposition the field out of the nextgen-only `LanguageSettings` block. Server-first. | none |
| `14cdad2` | bound heartbeat response wait | heartbeat.cpp | **PORT** | Stops a radio dip freezing the display 15s. Caveat (Ron's own note): `setConnectTimeout()` unproven-by-build on espressif32@6.4.0 - do one `pio run`. | none |
| `4443ef1` | report maxAllocHeap | heartbeat.cpp | **PORT** | `ESP.getMaxAllocHeap()` on all ESP32 variants. Server-first (needs portal column). | none |
| `64b136b` | act on commands in heartbeat response | device_commands.* (NEW), heartbeat.cpp, main.cpp | **ADAPT** (bring 2 files) | device_commands.* are new but self-contained + board-agnostic; every dep (log setters, displaySettings channel, safeRestart, getLocalTime) is legacy-present. Whitelist: set_log_level + reboot. | 040ae60; portal downlink |
| `501e0ba` | add set_log_delete_on_boot | device_commands.*, heartbeat.cpp | **ADAPT** (rides 64b136b) | Getter legacy-present. Server-first. | 64b136b |
| `e7dcf9a` | add set_log_retention_days + set_update_channel | device_commands.*, heartbeat.cpp | **ADAPT** (rides 64b136b) | All deps legacy-present. Server-first. | 64b136b, 501e0ba |

**Summary:** The two reconnect ports Ron cares about most, `7b11b7e` (mDNS re-register) and `835a40c` (setup-skip), are clean PORTs touching only legacy-present, board-agnostic files (the only surgery is dropping `7b11b7e`'s bootstrap hunk). `14cdad2` (bounded heartbeat wait) is also a clean high-value PORT, with one real caveat from Ron's own commit note: `setConnectTimeout()` needs a build-check on espressif32@6.4.0 (same platform as legacy). The fleet-command downlink (`64b136b`->`501e0ba`->`e7dcf9a`) is an ADAPT-chain: it drags in the new but self-contained `device_commands.{cpp,h}`; port in commit order, bringing `040ae60` first (logLevel is set_log_level's completion field). All four heartbeat field additions are **server-first** - the shared portal's `heartbeat_clock()` must accept each field before firmware sends it (no extra DB work if nextgen already shipped them). Only nextgen-model blocker is `13d0fd2` (SKIP); when porting `499f71e`, strip its `g_usingFactoryWifi` reference.
### 4.2 i18n, OTA, logging

| commit | title | files | verdict | reason / legacy-fit | depends on |
|---|---|---|---|---|---|
| `66a3ad2` | language-neutral phrase engine + runtime lang/dialect | time_mapper.cpp, grid_layout.*, clock_display.cpp, +NEW phrase_rules/language_settings + rewrite of ALL nl grids | **SKIP (large ADAPT)** | Huge refactor: replaces the Dutch switch with a PhraseRules table and re-authors every grid variant from Dutch words to slot keys. All ~11 legacy variants would need rewriting. Output byte-identical; runtime lang/dialect is meaningless on a Dutch-only line. Zero product value, large risk. | - |
| `95f9a66` | lang/dialect picker in Display tab | dashboard.html, i18n json | **SKIP** | UI for the picker; needs `66a3ad2`; nothing to pick on Dutch-only. | 66a3ad2 |
| `a6de399` | split German dialect into two axes | grid_layout.*, de_50x50, phrase_rules.h | **SKIP** | Purely German; legacy is Dutch-only. | 66a3ad2 |
| `3144de9` | make -v2 aliases 301 redirects | web_routes.h | **SKIP** | Legacy has no `-v2`/`-legacy` routes to redirect. | - |
| `3541105` | retire the -v2 URL aliases | web_routes.h, chronolett.css | **SKIP** | Deletes routes/assets that don't exist in legacy. | 3144de9 |
| `e94a891` | first-boot update channel from FIRMWARE_VERSION | display_settings.h | **PORT** | Real bug: a freshly-flashed early/dev firmware auto-OTAs back to stable on first WiFi. Self-contained board-agnostic string parsing. High value, trivial. | - |
| `ee09c42` | cache validators on static assets | web_routes.h | **PORT** | Fixes stale UI after an fs.bin OTA. `serveFile()`/`getUiVersion()` exist in legacy; pure Arduino WebServer (Cache-Control/ETag/If-None-Match). Minor shape adaptation. | - |
| `6903cb4` | log sink retry instead of latch-off + report | log.cpp/h, heartbeat.cpp, +NEW log_sink_health.h | **ADAPT (high-value, low-risk)** | The bug is PRESENT in legacy (log.cpp:119 one-way `fileSinkEnabled=false` latch + ignored `logFile.print` return). Core patch applies near-directly; carry `log_sink_health.h` + add 3 heartbeat fields. Portal drops unknown keys, so no hard block. | pairs w/ 7f8c106 |
| `7f8c106` | validate log features against a real clock | tools/test-log-features.sh | **ADAPT / optional** | Standalone HTTP+jq diagnostic; port alongside `6903cb4` as its test. | 6903cb4 |
| `7d7de6c` | OTA: keep fs version when image unchanged | tools/publish-ota.sh | **ADAPT** | Board-agnostic LittleFS-hash logic, but `publish-ota.sh` is nextgen-only; legacy publishes via `tools/full_upload.py`. Device guard exists in legacy. Superseded by `dc7cc2f`. | - |
| `dc7cc2f` | OTA: compare fs CONTENTS not bytes | tools/publish-ota.sh | **ADAPT** | Fixes that `7d7de6c` never fired. Port the pair as one unit, re-implemented against legacy's publish path - only if the frozen line still gets fs republishes. | 7d7de6c |
| `f4a044e` | fail build on unknown grid in product.json | tools/set_grid_filter.py | **ADAPT** | Validation-hardening is board-agnostic, but the script isn't tracked at `e353160` and legacy's GRID_MAP differs. Low value on a frozen line not gaining grids. | - |

**Summary:** The i18n cluster (`66a3ad2`/`95f9a66`/`a6de399`) is **not worth porting to a Dutch-only legacy line** - the engine is a large structural refactor with byte-identical output, and runtime language/dialect + the German work have no meaning with one language. The clean, high-value, board-agnostic ports are **`e94a891`** (first-boot channel bug), **`ee09c42`** (stale-UI-after-OTA), and the core of **`6903cb4`** (silent log-sink death, a bug that genuinely exists in legacy `log.cpp`). Do `e94a891` and `ee09c42` first (independent, trivial); port `6903cb4` with its `log_sink_health.h` + heartbeat fields and pair `7f8c106` as its test; port the OTA pair `7d7de6c`->`dc7cc2f` only as a unit and only if legacy still gets fs republishes (re-implemented against `full_upload.py`, since `publish-ota.sh` is nextgen-only). SKIP the two `-v2` route commits. **Board caveat:** NO ESP32-S3 / partition / OTA-format-specific code found in any of these; the only obstacle for the OTA fixes is that the publish *tooling* is nextgen-only, not board incompatibility.
### 4.3 Web UI / dashboard

Legacy serves the **classic** dashboard (`data/{admin,dashboard,logs,mqtt,setup,update}.html`) and still has the setup wizard and the runtime grid selector. It does NOT have `chronolett.css`, `data/i18n/*`, `ral-picker.js`, or any `*-v2.html` pages.

| commit | title | files | verdict | reason / legacy-fit | depends on |
|---|---|---|---|---|---|
| `4d769e6` | v2 dashboard + `/setGridVariant` ID fix | chronolett.css, display_settings.h, web_routes.h | **SPLIT: PORT firmware / ADAPT UI** | The firmware hunk is a real bugfix legacy also has (web_routes.h:799 conflates enum ID with array index) + boot self-heal. Since legacy KEEPS the grid selector, port it. The v2 UI is a branding decision. | firmware: none |
| `86ba669` | Remove setup wizard | setup.html, setup_state.*, main.cpp, heartbeat.cpp, +5 | **ADAPT (Ron decides)** | Provisioning-model change, not a UI port. Legacy still ships the wizard as its provisioning path. Removing it = WiFiManager-only. Ron must own this. | behavioral |
| `14283ed` | Remove runtime grid selector | admin/dashboard.html, display_settings.h, web_routes.h | **SKIP (Ron decides)** | Tied to nextgen's one-variant-per-product build model. Contradicts legacy's runtime-selector model unless legacy also adopts single-variant builds. | nextgen build filter |
| `c324616` | v2 i18n infra + update markup | i18n.js, en/nl.json, routes | **PORT (infra) / ADAPT (markup)** | `i18n.js` is pure front-end, **decoupled from the firmware i18n engine**; engine + `/i18n/*` routes + dictionaries port cleanly. Markup targets `-v2` pages legacy lacks. | none for infra |
| `1d91a52` | i18n markup: dashboard/admin/mqtt/logs | -v2 pages, en/nl.json | **ADAPT** | Dictionaries reusable; the HTML diffs target `-v2` pages, so re-apply `data-i18n` against classic HTML. | c324616 |
| `03399fa` | swatch palette color picker | -v2 pages, chronolett.css | **ADAPT** | Board-agnostic (feeds existing `/setColor`), but UI lives in v2 pages; graft into classic. | chronolett.css |
| `2f1002f` | harden swatch `/setColor` | -v2 pages | **ADAPT** | Front-end robustness; route exists in legacy, code sits in v2 pages. | 03399fa |
| `d543536` | RAL Classic colour picker | ral-picker.js, ral-classic.json, -v2, routes | **ADAPT** | Self-contained asset + routes feed `/setColor`; wiring is v2-page + chronolett.css. | 03399fa |
| `26f5251` | hex + RGB direct-entry | -v2 pages | **ADAPT** | Front-end extension of the picker; same graft. | 03399fa/d543536 |
| `09ad373` | logo fill-all picker + earlyLedClear fix | dashboard-v2.html, led_controller.cpp | **SPLIT: PORT firmware / ADAPT UI** | Firmware one-liner `EARLY_CLEAR_LED_COUNT 256->600` (led_controller.cpp:136) is portable - **verify against legacy strip sizes/RAM**. Picker UI is v2-only. | firmware: none |
| `5ccd457` | promote v2 to default | del `-v2.html`, led_controller.*, routes | **SKIP** | Renames v2->primary + `-legacy` aliases; wholesale v2 adoption. | full v2 adoption |
| `d5a589a` | restore v2 dashboard, drop classic toggle | dashboard.html (full rewrite) | **SKIP** | Replaces classic dashboard.html with v2. This IS "legacy becomes v2". | v2 adoption |
| `cf6fc1e` | mobile tabbed redesign + compact css | chronolett-compact.css, dashboard.html | **SKIP / ADAPT** | Built on the already-promoted v2 dashboard; doesn't apply to classic HTML. | 5ccd457, d5a589a |
| `2ceebc4` | extend mobile redesign to admin/mqtt/logs/update | admin/logs/mqtt/update.html | **SKIP / ADAPT** | Extends the v2 mobile redesign; assumes v2 pages. | cf6fc1e |
| `ada38ee` | detect logo/mini by `nextgen-*` env names | dashboard.html | **SKIP - would BREAK legacy** | Flips detection from `wordclock-*` to `nextgen-*`. Legacy env names ARE `wordclock-*`, so it already has the correct strings. Porting reverses the fix and hides Logo / breaks mini polling. **Do NOT port.** | nextgen naming |

**Summary:** The v2 dashboard is **not worth wholesale adoption on legacy** - it's a Chronolett rebrand + file-rename/promote structure that assumes nextgen product identity. Legacy keeps its classic UI as the base. The safe standalone ports are the **firmware bugfixes buried inside UI commits**: the `/setGridVariant` enum-ID lookup + boot self-heal (`4d769e6`, legacy has the exact bug and keeps the selector) and the `earlyLedClear` count bump (`09ad373`, verify board sizes). The **colour-picker family** (`03399fa`/`2f1002f`/`d543536`/`26f5251`) and the **i18n engine** (`c324616` i18n.js, decoupled from firmware i18n) are board-agnostic but ship their markup inside `-v2` pages, so adopting them on classic legacy is graft work, not a cherry-pick. Two items are **Ron-only behavioral decisions**: removing the setup wizard (`86ba669`, changes provisioning) and removing the runtime grid selector (`14283ed`, needs the single-variant build). **Do NOT port `ada38ee`** - it would break logo/mini detection on legacy's `wordclock-*` hardware. Flags: verify `earlyLedClear 600` against legacy strip lengths/RAM; i18n markup port cost depends on classic-vs-v2 HTML divergence.
### 4.4 Bootstrap, LED/display, build/tools

Legacy base `e353160` has **no bootstrap firmware** (`bootstrap_main.cpp`, `bootstrap_provision.*`, `data/bootstrap.html` all absent) and **none of** `tools/flash.sh`, `tools/release.sh`, `tools/check-cache-headers.sh` (all introduced at or after the S3 cut). Legacy flashes directly + WiFiManager; legacy publishes via `tools/full_upload.py`.

| commit | title | files | verdict | reason / legacy-fit | depends on |
|---|---|---|---|---|---|
| `daa4fd1` | extract installProductFirmware for bootstrap | ota_updater.*, system_utils.cpp | **SKIP** | Bootstrap-only OTA primitives behind a `WORDCLOCK_BOOTSTRAP` gate; commit states "no behavioral change for existing per-device builds". No legacy value. | - |
| `8007202` | add nextgen-bootstrap firmware | bootstrap_main.cpp, bootstrap_provision.*, bootstrap.html, products/nextgen-bootstrap/*, flash.sh, release.sh, platformio.ini | **SKIP** | Builds the entire first-flash provisioning firmware+product+UI; all absent from legacy. | - |
| `1c88814` | trim lib_deps, skip git/OTA for bootstrap | nextgen-bootstrap ini, release.sh | **SKIP** | Bootstrap build config; `release.sh` absent in legacy. | 8007202 |
| `c4ec75e` | skip unit-tests/coverage for bootstrap | release.sh | **SKIP** | Bootstrap `release.sh` branch. | 8007202 |
| `e48d7ee` | predict next-step AP, scrub Wi-Fi creds | bootstrap.html, bootstrap_main.cpp, bootstrap_provision.*, system_utils.cpp | **SKIP** | Bootstrap handoff; `system_utils.cpp` touch is `WORDCLOCK_BOOTSTRAP`-gated. | 8007202 |
| `c8ebc40` | surface OTA phases + byte progress | bootstrap.html, bootstrap_provision.*, ota_updater.cpp | **SKIP** | `ota_updater.cpp` hunk = bootstrap progress hooks, no-op `((void)0)` on per-device builds. | daa4fd1 |
| `88873a4` | carousel product->channel picker | bootstrap.html | **SKIP** | Bootstrap UI only. | 8007202 |
| `8600e4c` | skip release-notes for bootstrap | release.sh | **SKIP** | Bootstrap `release.sh` branch. | 8007202 |
| `2fde2f5` | carousel track -> CSS grid | bootstrap.html | **SKIP** | Bootstrap UI only. | 88873a4 |
| `5411e9f` | name downloads, force 100% | bootstrap.html, ota_updater.cpp | **SKIP** | `ota_updater.cpp` hunk edits bootstrap-only `installProductFirmware`. | c8ebc40 |
| `ecf8077` | flush progress to 100% | bootstrap.html | **SKIP** | Bootstrap UI only. | c8ebc40 |
| `4807493` | 4s status-poll timeout | bootstrap.html | **SKIP** | Bootstrap UI only. | 8007202 |
| `8e0880f` | OTA self-update + re-install path | admin.html, admin-v2.html, bootstrap.html, i18n, bootstrap_provision.cpp, ota_updater.*, web_routes.h, release.sh | **SKIP** | Bootstrap self-update + re-install UI; the admin/web_routes additions serve bootstrap, useless without it. | daa4fd1 |
| `b91ed17` | fw version per channel in picker | bootstrap.html, bootstrap_provision.cpp, ota_updater.* | **SKIP** | Changes `listAvailableChannels` to a bootstrap-only `ChannelTarget` struct. | daa4fd1 |
| `df5dbae` | data-driven LED segment table | led_controller.cpp, +NEW led_segments.{h,cpp}, +NEW test | **PORT** | Board-agnostic: default resolves to one CLOCK segment (+logo), on-wire byte-identical; split arms only if a product defines `CLOCK_DATA_PIN_2`+`CLOCK_SEGMENT_SPLIT` (no legacy product does). `led_segments.cpp` pure/hardware-free, 7 unit tests. Benefit is code hygiene, not function. | none |
| `c04e9bf` | split clock across GPIO 4 + 6 | nextgen-logo-100x100 product_config.h | **SKIP** | ESP32-S3 dual-data-line wiring; explicit "do NOT flash until the strip is physically cut after LED 244". | df5dbae |
| `bdd4adc` | correct split index to 245 | nextgen-logo-100x100 product_config.h | **SKIP** | Tunes c04e9bf; nextgen-100x100 hardware only. | c04e9bf |
| `fc5b196` | cap clock brightness 200/255 | nextgen-logo-100x100 product_config.h, led_state.h | **SKIP** | Per-product cap for the 100x100 brown-out. The `led_state.h` `MAX_BRIGHTNESS` clamp is board-agnostic (byte-identical uncapped) - a latent option only. | df5dbae |
| `0e6fa7e` | rename 100x100->105x105 | platformio.ini, nextgen-logo-*, grid_layout.*, grid_variants/*, bootstrap_provision.cpp, dashboard-legacy.html | **SKIP** | Nextgen product rename + nextgen grid + bootstrap/publish tooling. | c04e9bf |
| `aa2e4a8` | LED diag tool on classic dashboard | led_controller.*, web_routes.h, dashboard.html | **SKIP** | `PRODUCT_VARIANT_LOGO`-only, gated to nextgen-logo-100x100, and REVERTED by 2806cda. | df5dbae |
| `2806cda` | revert LED diag from main | dashboard.html | **SKIP** | Paired revert of aa2e4a8; nothing to port. | aa2e4a8 |
| `a5c8ec0` | sell-mode 10:48, dedup constant | clock_display.cpp, display_settings.h, web_routes.h, admin.html, admin-legacy.html, i18n | **ADAPT** | Board-agnostic time-display fix; C++ core (`SELL_MODE_HOUR/MINUTE`) maps onto legacy (which has sell mode). Label half targets `admin-legacy.html` + `i18n/*` legacy lacks; redirect to legacy's `admin.html`. | none |
| `3bd455a` | sell-mode 08:48 | display_settings.h, admin*.html, i18n | **ADAPT** | Retunes constant + labels; port with a5c8ec0. | a5c8ec0 |
| `a98229d` | sell-mode 08:43 (final) | display_settings.h, admin*.html, i18n | **ADAPT** | Final value; fold the three into one sell-mode port at 08:43. | 3bd455a |
| `d2b7d5c` | flash.sh esptool 5.x dash-form | tools/flash.sh | **ADAPT** | esptool-5.x migration is generic, but `flash.sh` is absent + S3-hardcoded (`CHIP="esp32s3"`, offset 0x0 vs classic 0x1000). A legacy helper needs esp32 chip + 0x1000. | 8007202 |
| `7676c9c` | flash.sh guard missing littlefs.bin | tools/flash.sh | **ADAPT** | Generic guard, same flash.sh absence/S3-hardcoding caveat. | d2b7d5c |
| `9cbca39` | release --all mass-build | tools/release.sh | **SKIP** | Iterates the nextgen product array; `release.sh` absent in legacy. | 8007202 |
| `a3352f7` | anchor --all on highest version | tools/release.sh | **SKIP** | Per-nextgen-product version scan. | 9cbca39 |
| `ed3567f` | version-only custom input | tools/release.sh | **SKIP** | Per-product prefix handling; nextgen-oriented, file absent. | 9cbca39 |
| `f480117` | date default + base-10 arithmetic | tools/release.sh | **ADAPT** | Genuinely generic bash fix (`10#` octal guard on leading-zero dates) worth extracting IF legacy ever gains a release script; lives in nextgen-only `release.sh`. | none |
| `4ee3b00` | 304 body via curl size_download | tools/check-cache-headers.sh | **ADAPT** (cleanest) | Fully board/product-agnostic HTTP-header fix, applies verbatim; only obstacle is the script's absence in legacy (bring it over). | none |

**Summary:** The bootstrap cluster is **unanimously SKIP** - legacy has no first-flash provisioning firmware, so every commit that builds/styles/wires it has no legacy counterpart, and even the shared-looking `daa4fd1`/`ota_updater.*` touches expand to bootstrap-only symbols or `((void)0)` no-ops on per-device builds (legacy gains no per-device OTA improvement). The one genuine **PORT** here is `df5dbae` (data-driven LED segment table): board-agnostic by construction, on-wire byte-identical for a single-string product, pure/tested support file - but the payoff is code hygiene, not behavior, since no legacy product splits its LED string. All the real 100x100/105x105 work (`c04e9bf`/`bdd4adc`/`fc5b196`/`0e6fa7e`) and the LED diag tool (`aa2e4a8`, itself reverted) are nextgen-hardware/nextgen-product = SKIP. **Sell-mode** (`a5c8ec0`+`3bd455a`+`a98229d`) is a worthwhile **ADAPT**: the C++ core ports onto legacy's existing sell mode, but the labels target `admin-legacy.html`+`i18n/*` that legacy lacks, so port the three as one change at the final 08:43 value and redirect labels to legacy's `admin.html`. The **generic tooling fixes** are portable in spirit but blocked by file-absence (ADAPT): `4ee3b00` (304 cache-header check) is the cleanest and applies verbatim once the script is brought over; `f480117`'s `10#` octal date guard is a real bash bug worth extracting; the `flash.sh` pair (`d2b7d5c`/`7676c9c`) must be re-targeted from S3 (chip + 0x0 offset) to esp32dev (chip + 0x1000). **Flag:** these tooling verdicts assume the frozen base `e353160`; if a later legacy branch has since grown its own `flash.sh`/`release.sh`/`check-cache-headers.sh`, those ADAPTs could become near-clean PORTs.

---

## 5. Docs / noise (no port needed)

Pure documentation or throwaway commits since the cut:
`aa602a7` (add ROADMAP), `dcd6114` / `42de39f` / `95b95d0` / `3b2ffa5` / `3c59fe4`
(roadmap updates), `5cfa784` / `3c84467` (app<->device API docs), `0a32d6d`
(gitignore handover/), `7871d20` ("."), `2c78962` (documentation).

## 6. Consolidated shortlist (what actually lands on legacy-main)

Pulling every non-SKIP verdict across sections 4.1-4.4 into one ranked plan.
"Server-first" = the shared portal's `heartbeat_clock()` must accept the new
field before firmware sends it.

### Tier A - clean PORTs, high value, do first
Board-agnostic, touch only legacy-present files, little/no adaptation.

| commit | what it fixes | note |
|---|---|---|
| `835a40c` | skip render loop + STA scans during setup | foundation for the WiFi work |
| `7b11b7e` | re-register mDNS after reconnect (`wordclock.local` goes dead) | **the reconnect fix Ron cares about most**; drop the bootstrap hunk |
| `14cdad2` | bound heartbeat response wait (radio dip freezes display 15s) | one `pio run` to confirm `setConnectTimeout()` on espressif32@6.4.0 |
| `e94a891` | first-boot update channel bug (dev fw self-OTAs back to stable) | trivial, self-contained |
| `ee09c42` | cache validators (stale UI after an fs.bin OTA) | minor Arduino WebServer shape adaptation |

### Tier B - PORTs that need a small carry or a build-check
| commit(s) | what | carry / caveat |
|---|---|---|
| `040ae60` + `4443ef1` | heartbeat reports log level + maxAllocHeap | server-first; reposition out of the nextgen `LanguageSettings` block |
| `df5dbae` | data-driven LED segment table | brings `led_segments.{h,cpp}` + its test; hygiene not behavior (byte-identical) |
| `4d769e6` (fw hunk only) | `/setGridVariant` enum-ID lookup bug + boot self-heal | legacy has the exact bug and keeps the selector; take the firmware hunk, leave the v2 UI |
| `09ad373` (fw hunk only) | `earlyLedClear` 256->600 | **verify against legacy strip lengths / RAM first** |

### Tier C - ADAPTs worth doing (real rework, still valuable)
| commit(s) | what | the adaptation |
|---|---|---|
| `6903cb4` (+ `7f8c106` as test) | log sink retry instead of silent latch-off | the bug IS present in legacy `log.cpp:119`; carry `log_sink_health.h` + 3 heartbeat fields |
| `64b136b` -> `501e0ba` -> `e7dcf9a` | fleet command downlink (set_log_level, reboot, retention, channel) | brings `device_commands.{cpp,h}` (self-contained); port in commit order after `040ae60`; server-side downlink required |
| `499f71e` | enroll Wi-Fi from admin page | strip the `g_usingFactoryWifi` clause (that flag rides skipped `13d0fd2`); build-verify |
| `a5c8ec0` + `3bd455a` + `a98229d` | sell-mode timing fix | port as ONE change at final value 08:43; redirect labels to legacy `admin.html` |
| `4ee3b00` (+ opt. `f480117`) | 304 cache-header test tool + a generic bash date fix | bring `check-cache-headers.sh` over; verbatim otherwise |

### Deliberately NOT porting
- **Bootstrap cluster** (14 commits) - legacy has no first-flash firmware.
- **i18n / phrase engine** (`66a3ad2`/`95f9a66`/`a6de399`) - large refactor, byte-identical output, no meaning on a Dutch-only line.
- **v2 dashboard adoption** (`5ccd457`/`d5a589a`/`cf6fc1e`/`2ceebc4` + the colour-picker family unless Ron wants the graft) - a Chronolett rebrand assuming nextgen product identity.
- **`ada38ee`** - **would BREAK** legacy logo/mini detection (flips it to `nextgen-*` names). Do not port.
- Nextgen-hardware LED work (`c04e9bf`/`bdd4adc`/`fc5b196`/`0e6fa7e`), the `-v2` route commits, and the nextgen-only `release.sh`/`flash.sh` build tooling.

### Ron-only behavioral decisions (not code ports)
- `86ba669` remove setup wizard = a **provisioning-model change** (WiFiManager-only). Legacy still ships the wizard as its provisioning path.
- `14283ed` remove runtime grid selector = needs the **single-variant-per-product build model**; contradicts legacy's runtime selector.

## 7. Next steps

1. **Confirm the base** with Ron: `git checkout -b legacy-main legacy/esp32` (= `e353160`), and decide where this doc gets committed (wordclock repo, on a branch, not `main`).
2. **Cut Tier A first** - the five clean reconnect/OTA/mDNS fixes - as the first `legacy-main` increment; do the one `pio run` build-check (`14cdad2`).
3. Then Tier B (with the `earlyLedClear` strip-size verify) and Tier C, each as its own reviewable commit.
4. Hold the Ron-only behavioral decisions and the v2 dashboard question for a separate conversation.
