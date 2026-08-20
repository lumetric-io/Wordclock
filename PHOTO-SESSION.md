# Photo-session firmware — `photo/session-wifi`

**This branch is not a feature branch. Never merge it to `main`, never
cherry-pick out of it, never tag it. Publish it to the `develop` OTA channel
only — never `stable`, never `early`.**

It exists for one product photo shoot. Every clock must join the studio Wi-Fi
seconds after power-on, with no config portal, no captive AP, and nothing on
the LED face except the time.

Branched from `main` at `a7c258c` (2026-08-20).

## What is different from `main`

| Behaviour | `main` | This branch |
|---|---|---|
| Wi-Fi at power-on | WiFiManager `autoConnect` | studio SSID from `include/secrets.h`, ~20 s, **then reboots into the `main` behaviour if absent** |
| Config portal | opens after 300 s offline | suppressed *only* while on the studio network |
| Credentials in flash | written to `nvs.net80211` | `WiFi.persistent(false)` on the studio path — never written |
| Fleet registration | on first connect | **compiled out** |
| Heartbeat | hourly | **compiled out** (never initialised) |
| OTA — automatic (boot + 02:00) | on | **compiled out** |
| OTA — manual (admin UI) | on | **unchanged, works both directions** |
| Time | NTP | **no NTP**; sell mode forced on at boot |
| Version | `<product>-<date>` | `<product>-<date>-photo.1` |

The switch is `PHOTO_SESSION_WIFI`, set to `1` in `[env:base]` of
`platformio.ini`. It defaults to `0` in `src/photo_session.h`, so any file that
escapes into `main` compiles to shipping behaviour.

Touched files: `platformio.ini`, `src/photo_session.h` (new), `src/network.cpp`,
`src/runtime_services.cpp`, four `products/*/product_config.h`, `.gitignore`.

## What happens when the studio network isn't there

Power-on → ~20 s attempt on the studio SSID → not found → **the clock sets a
mark in RTC RAM and reboots**. The second boot skips the hardcoded path
entirely and runs the ordinary `main` Wi-Fi behaviour: stored credentials
first, config portal after the usual timeout. From there the dashboard, the LAN
and manual OTA all work normally.

Cost is one reboot and ~25 s whenever a photo clock is powered on away from the
studio. At the studio it never happens.

The mark is in RTC RAM, not NVS, on purpose: a soft reset keeps it (so the
fallback boot doesn't loop), a power cycle loses it (so unplugging a clock at
the studio makes it try the studio network again). Nobody has to remember to
clear anything.

Why reboot instead of just falling through in the same boot: once
`WiFi.begin(ssid, pass)` has run with storage forced to RAM, the driver's
in-memory config *is* the studio network, and the argless `WiFi.begin()` that
WiFiManager and the reconnect loop use to mean "this clock's own network" would
keep retrying the studio SSID. Reinitialising the driver mid-boot to undo that
is fiddly and untestable off-hardware; rebooting is neither.

## Sell time

`initTimeSync()` is not called. The face is driven by the **sell mode** that
already exists on `main` — the "Verkoopmodus" toggle in the admin portal,
`DisplaySettings::isSellMode()`, and the override in
`ClockDisplay::prepareDisplayTime()`. None of that is modified; brightness,
colour, animation and every other setting behave exactly as on `main`.

Two small things in `src/photo_sell_time.h` are needed to make the toggle
actually render with no clock:

- **A plausible system time is set once at boot** (`settimeofday`, 2026-08-20
  10:48 UTC). `ClockDisplay` only reaches the sell-mode override after
  `updateTimeCache()` succeeds, and `getLocalTime()` rejects anything before
  2016 — without this the clock sits on the no-time indicator forever and the
  toggle appears to do nothing.
- **That time is re-pinned once a second.** Sell mode overrides hour and
  minute, but night mode reads the cached time *underneath* the override, so a
  free-running clock walks into the night window and blanks the display a few
  hours into a shoot.

It is forced on via `setSellModeVolatile()`, which does not write NVS — a clock
later returned to stable firmware must not keep showing the sell time to its
owner. Toggling it in the admin portal uses the normal persisting setter, so if
you turn it on by hand, turn it off before handing the clock over.

The face shows **10:48**. That comes from `SELL_MODE_HOUR`/`SELL_MODE_MINUTE`
in `src/display_settings.h` (shared with `main`), and `PHOTO_CLOCK_EPOCH` is
derived from the same two constants, so the pinned clock and the rendered face
cannot drift apart.

## OTA

Automatic updates are off — boot check and the 02:00 daily check are compiled
out. That is not a restriction on you, it is protection: these are provisioned
clocks whose stored channel is usually `stable`, so leaving the automatic path
on would mean the 02:00 check quietly reinstalls stable and strips the photo
firmware off half the set the night before the shoot.

The admin UI's **"check for updates"** is untouched and is how you drive
everything:

**Installing the photo build** — per clock: dashboard → channel `develop`;
publish this build to `nextgen-<product>/channels/develop.json`; click check
for updates.

**Going back to stable** — per clock: dashboard → channel `stable` → click
check for updates. The install goes through even though it is numerically a
downgrade: `parseVersionCore()` stops at the first non-digit, so a
product-prefixed version parses to nothing and `isVersionNewer()` returns true
for *any* differing string. Direction is not enforced for nextgen products.

A clock left on `develop` afterwards will not auto-update — `main` already
excludes the develop channel from automatic checks.

## Products

Build only these four:

- `nextgen-mini`
- `nextgen-30x30`
- `nextgen-50x50`
- `nextgen-logo-55x50`

`nextgen-logo-105x105` is **excluded**: it is the only product that splits the
clock strip, and its 5V rail still browns out when the logo segment enables.
Building it on this branch is a hard `#error` in `src/photo_session.h`.

## Before building

Set the studio network in `include/secrets.h` (gitignored — the branch carries
no credentials):

```c
#define PHOTO_WIFI_SSID     "studio-network"
#define PHOTO_WIFI_PASSWORD "..."
```

An empty SSID is a compile error, not a device that boots and silently joins
nothing.

```bash
pio run -e nextgen-50x50 -t upload
pio run -e nextgen-50x50 -t uploadfs   # dashboard assets, as usual
```

Use `tools/flash.sh` at your own risk — it fetches **VPS-built** artifacts,
which are `main` builds, not these.

## After the shoot — converting a clock back

Photo firmware writes no Wi-Fi credentials to NVS, but it does leave its own
settings (brightness, colour, language) and a `-photo.1` version string. To
return a unit to sellable state:

```bash
esptool.py --chip esp32s3 erase_flash
```

then flash `nextgen-bootstrap` and provision it normally. Full erase rather
than an OTA on top: an OTA would keep the NVS partition, and a customer clock
should not start life carrying a studio setup.

The alternative — channel `stable` + manual check for updates, as described
under **OTA** above — leaves the photo clock's NVS in place: its settings, its
`develop` channel history and its fleet identity all survive. That is fine for
a clock of your own going back on the shelf. `erase_flash` is for units going
to a customer.
