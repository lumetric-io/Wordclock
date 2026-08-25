#pragma once

// ---------------------------------------------------------------------------
// PHOTO SESSION BUILD — branch `photo/legacy-session-wifi` only. Never merge
// to legacy-main. Port of `photo/session-wifi` (the nextgen photo build).
// ---------------------------------------------------------------------------
//
// A one-off firmware for a product photo shoot: every clock must be on the
// studio Wi-Fi within seconds of power-on, with no config portal, no captive
// AP and no "connect me" LED animation ever appearing in a frame.
//
// Two things follow from "one-off":
//
//  1. It leaves no trace. WiFi.persistent(false) keeps the studio SSID out of
//     nvs.net80211, so a clock that goes back to a customer does not carry the
//     studio network in flash. (Belt and braces: erase_flash before reflashing
//     a photo clock — see PHOTO-SESSION.md.)
//  2. It stays out of the fleet. Registration, heartbeat and the OTA checks are
//     compiled out, so twenty clocks blinking on for an afternoon never appear
//     as devices, never inflate the telemetry, and — the one that would really
//     hurt — never auto-update themselves mid-shoot.
//
// The switch defaults to 0 so that even if a file from this branch does escape
// into main, it compiles to exactly the shipping behaviour.

#ifndef PHOTO_SESSION_WIFI
#define PHOTO_SESSION_WIFI 0
#endif

#if PHOTO_SESSION_WIFI

#include "secrets.h"

#ifndef PHOTO_WIFI_SSID
#error "PHOTO_SESSION_WIFI=1 but PHOTO_WIFI_SSID is not defined. Add PHOTO_WIFI_SSID / PHOTO_WIFI_PASSWORD to include/secrets.h (gitignored)."
#endif
#ifndef PHOTO_WIFI_PASSWORD
#error "PHOTO_SESSION_WIFI=1 but PHOTO_WIFI_PASSWORD is not defined. Add PHOTO_WIFI_SSID / PHOTO_WIFI_PASSWORD to include/secrets.h (gitignored)."
#endif

// On nextgen this excludes nextgen-logo-105x105 (unresolved 5V brownout when
// the split logo segment enables). No legacy product defines
// CLOCK_SEGMENT_SPLIT today, so on this branch the guard is a tripwire: if a
// future legacy product ever splits the strip, decide about it deliberately
// instead of discovering the brownout in front of a photographer.
#ifdef CLOCK_SEGMENT_SPLIT
#error "This product splits the clock strip (CLOCK_SEGMENT_SPLIT); the nextgen photo build excluded such hardware for an unresolved 5V brownout. Decide deliberately before building it as a photo clock. See PHOTO-SESSION.md."
#endif

// How long initNetwork() waits for the studio AP before handing over to the
// periodic retry in processNetwork(). Kept short: the clock should start
// telling time even if the AP is briefly down, and the retry loop is patient.
#define PHOTO_WIFI_CONNECT_TIMEOUT_MS 20000

// Retry cadence once connected-then-dropped, or never-connected at boot.
#define PHOTO_WIFI_RETRY_INTERVAL_MS 15000

// Defined in network.cpp. Returns true once the STA is associated.
bool connectPhotoWifi();

#endif  // PHOTO_SESSION_WIFI
