# Photo-session firmware — `photo/session-wifi`

**This branch is not a feature branch. Never merge it to `main`, never
cherry-pick out of it, never tag it, never publish it to OTA.**

It exists for one product photo shoot. Every clock must join the studio Wi-Fi
seconds after power-on, with no config portal, no captive AP, and nothing on
the LED face except the time.

Branched from `main` at `a7c258c` (2026-08-20).

## What is different from `main`

| Behaviour | `main` | This branch |
|---|---|---|
| Wi-Fi | WiFiManager `autoConnect`, portal after 300 s | hardcoded SSID from `include/secrets.h`, ~20 s at boot then retry every 15 s |
| Config portal | opens on failure | **never opens** (`startWiFiManagerPortal()` is a no-op) |
| Credentials in flash | written to `nvs.net80211` | `WiFi.persistent(false)` — never written |
| Fleet registration | on first connect | **compiled out** |
| Heartbeat | hourly | **compiled out** (never initialised) |
| OTA check | at boot + daily 02:00 | **compiled out** |
| Version | `<product>-<date>` | `<product>-<date>-photo.1` |

The switch is `PHOTO_SESSION_WIFI`, set to `1` in `[env:base]` of
`platformio.ini`. It defaults to `0` in `src/photo_session.h`, so any file that
escapes into `main` compiles to shipping behaviour.

Touched files: `platformio.ini`, `src/photo_session.h` (new), `src/network.cpp`,
`src/runtime_services.cpp`, four `products/*/product_config.h`, `.gitignore`.

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

Do not point a photo clock at the OTA server to "update it back" — the version
comparison for nextgen products treats any differing string as newer, so what
actually happens is unpredictable, and the fleet would gain a device row for a
clock that was deliberately kept out of it.
