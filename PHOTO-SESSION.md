# Photo-session firmware, legacy port: `photo/legacy-session-wifi`

**This branch is not a feature branch. Never merge it to `legacy-main`, never
cherry-pick out of it, never tag it. Publish it to the `develop` OTA channel
only, never `stable`, never `early`.**

It is the legacy-product port of `photo/session-wifi` (the nextgen photo
build; its PHOTO-SESSION.md is the original of this document). Purpose is the
same: every clock must join the studio Wi-Fi seconds after power-on, with no
config portal, no captive AP, and nothing on the LED face except the time.

Branched from `legacy-main` at `2ace71f` (2026-08-25), which includes the
fs-OTA stale-mount fix. Keep it rebased on `legacy-main` if that matters for
a future shoot; the diff is deliberately small.

## What is different from `legacy-main`

| Behaviour | `legacy-main` | This branch |
|---|---|---|
| Wi-Fi at power-on | WiFiManager `autoConnect` | studio SSID from `include/secrets.h`, ~20 s, **then reboots into the normal behaviour if absent** |
| Config portal | opens after the usual offline timeout | suppressed *only* while on the studio network |
| Credentials in flash | written to `nvs.net80211` | `WiFi.persistent(false)` on the studio path, never written |
| Fleet registration | on first connect | **compiled out** |
| Heartbeat | hourly | **compiled out** (never initialised) |
| Fleet commands | arrive via heartbeat | never arrive (heartbeat is out) |
| MQTT / Home Assistant | connects, publishes discovery | **compiled out** (never initialised) |
| OTA, automatic (connect + 02:00) | on | **compiled out** |
| OTA, manual (admin UI) | on | **unchanged, works both directions** |
| Time | NTP | **no NTP**; sell mode forced on at boot |
| Version | ordinary `-dev.N` | ordinary `-dev.N`, **not marked** |

The version string does **not** identify a photo build; `tools/release.sh`
rewrites `FIRMWARE_VERSION` from the channel on every bump, so a marker would
not survive (see the nextgen document for the history). Identify a running
photo build from its boot log instead: `[photo] Connecting to hardcoded SSID`,
`[photo] NTP skipped`, and no MQTT started line.

The switch is `PHOTO_SESSION_WIFI`, set to `1` in `[env:base]` of
`platformio.ini`. It defaults to `0` in `src/photo_session.h`, so any file
that escapes into `legacy-main` compiles to shipping behaviour.

Touched files: `platformio.ini`, `src/photo_session.h` (new),
`src/photo_sell_time.h` (new), `src/network.cpp`, `src/runtime_services.cpp`,
`src/main.cpp`, `src/display_settings.h`, `.gitignore`.

## Home Assistant

A photo clock never speaks MQTT: `initMqtt()` and `mqttEventLoop()` are gated
out in `runtime_services.cpp`, so the client is never constructed. It would
otherwise publish its retained discovery entities into the owner's Home
Assistant and leave them there long after the unit was erased and re-sold.
No call site needed its own guard; every publish path returns early on
`!mqtt.connected()`, which an uninitialised client never is.

## What happens when the studio network isn't there

Power-on, ~20 s attempt on the studio SSID, not found: **the clock sets a
mark in RTC RAM and reboots**. The second boot skips the hardcoded path
entirely and runs the ordinary Wi-Fi behaviour: stored credentials first,
config portal after the usual timeout. From there the dashboard, the LAN and
manual OTA all work normally.

The mark is in RTC RAM, not NVS, on purpose: a soft reset keeps it (so the
fallback boot doesn't loop), a power cycle loses it (so unplugging a clock at
the studio makes it try the studio network again). Nobody has to remember to
clear anything.

Why reboot instead of falling through in the same boot: once
`WiFi.begin(ssid, pass)` has run with storage forced to RAM, the driver's
in-memory config *is* the studio network, and the argless `WiFi.begin()` that
WiFiManager and the reconnect loop use to mean "this clock's own network"
would keep retrying the studio SSID. Rebooting is cheaper and testable.

## Sell time

`initTimeSync()` is not called. The face is driven by the sell mode that
already exists on `legacy-main` (the "Verkoopmodus" toggle,
`DisplaySettings::isSellMode()`, the override in the clock render path).
`src/photo_sell_time.h` adds the two things the toggle needs with no clock:
a plausible system time set at boot (`getLocalTime()` rejects anything before
2016, so without it the face sits on the no-time indicator) and that time
re-pinned once a second (night mode reads the cached time underneath the
override and would otherwise blank the display a few hours into a shoot).

The face shows **08:43** ("tien over half negen" plus three minute LEDs),
from `SELL_MODE_HOUR`/`SELL_MODE_MINUTE` in `src/display_settings.h`, shared
with the rendered face so they cannot drift apart. The pinned time is a
*local* wall-clock time converted with `mktime()`; the timezone is live on
this build too (`initLogSettings()` sets TZ in `log.cpp` independently of
NTP).

Sell mode is forced on via `setSellModeVolatile()`, which never writes NVS: a
clock later returned to stable firmware must not keep showing the sell time
to its owner. Toggling it in the admin portal uses the normal persisting
setter, so if you turn it on by hand, turn it off before handing the clock
over.

## OTA, and the legacy fs half

Automatic updates are compiled out (the connect-time check and the 02:00
daily check). These are provisioned clocks whose stored channel is usually
`stable`; leaving the automatic path on would mean the clock quietly
reinstalls stable and strips the photo firmware the night before the shoot.
The admin UI's **"check for updates"** is untouched and is how you drive
everything, both directions (the manual path installs any differing version,
downgrades included; field-proven 2026-08-25).

Legacy OTA updates **two halves**, firmware and filesystem, and the fs half
is the risky one: the fs is written by the firmware the clock is *currently
running*, and every legacy build older than this branch point carries the
stale-mount bug. So when publishing the photo build:

```bash
sudo tools/publish-ota.sh -p wordclock-legacy-nl-v4 -c develop \
    --fs-from <fs version the fleet already runs>
```

`--fs-from` pins the channel's fs half to the fs the clock already has, so
installing the photo build is a **firmware-only** hop and never rolls the
stale-mount dice through old code. The photo build renders the face and
serves the admin portal fine on the existing fs. Only publish a fs-carrying
photo build to a clock that is *already running* fix-carrying firmware.

**Installing**: per clock: dashboard, channel `develop`, publish this build
to the product's `channels/develop.json` as above, click check for updates.

**Going back to stable**: per clock: dashboard, channel `stable`, click check
for updates. Both halves come back; the fs downgrade runs through *this*
build's code, which carries the stale-mount fix, so it is safe.

A clock left on `develop` afterwards will not auto-update; `legacy-main`
already excludes the develop channel from automatic checks.

## Products

All legacy products build. No legacy product splits the clock strip, so the
nextgen branch's `nextgen-logo-105x105` exclusion does not apply here; the
`CLOCK_SEGMENT_SPLIT` guard in `src/photo_session.h` stays as a tripwire for
future hardware.

## Before building

Set the studio network in `include/secrets.h` (gitignored; the branch
carries no credentials):

```c
#define PHOTO_WIFI_SSID     "studio-network"
#define PHOTO_WIFI_PASSWORD "..."
```

An empty SSID is a compile error, not a device that boots and silently joins
nothing.

```bash
pio run -e wordclock-legacy-nl-v4 -t upload
pio run -e wordclock-legacy-nl-v4 -t uploadfs   # dashboard assets, as usual
```

## After the shoot, converting a clock back

Photo firmware writes no Wi-Fi credentials to NVS, but it does leave its own
settings behind (brightness, the `develop` update channel) and nothing in the
version string says the unit was ever a photo clock. For a unit going to a
customer:

```bash
esptool.py --chip esp32 erase_flash
```

then flash the product's stable firmware and filesystem serially and
provision it normally. Note the standing legacy quirk: a serially flashed fs
has no `/.fs_image_version` marker, so the first OTA check re-pulls the fs
once; that pull runs through the just-flashed firmware, so flash a
fix-carrying stable.

For a clock of your own going back on the shelf, channel `stable` plus a
manual check for updates (see OTA above) is fine; its NVS and settings
survive.
