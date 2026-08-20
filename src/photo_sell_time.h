#pragma once

// Sell time for the photo build — branch `photo/session-wifi` only.
//
// The shoot wants every clock showing the same face all afternoon, so the
// firmware doesn't sync NTP at all. It reuses the sell mode that already
// exists on main (DisplaySettings::isSellMode, the override in
// ClockDisplay::prepareDisplayTime) rather than inventing a second way to
// force a time — nothing here touches the render path.
//
// Two things are needed to make that work without a clock:
//
//  1. A plausible system time. ClockDisplay only reaches the sell-mode
//     override once updateTimeCache() has succeeded, and getLocalTime()
//     rejects anything before 2016 — so with NTP skipped and the clock at the
//     epoch, a photo build would sit on the no-time indicator forever.
//  2. That time held still. Sell mode overrides hour and minute, but night
//     mode reads the *un*overridden cached time. Left running, the system
//     clock walks into the night window and blanks the display several hours
//     into a shoot. Pinning it once a second is cheaper than special-casing
//     night mode, and it keeps log timestamps consistent as a side effect.

#include "photo_session.h"

#if PHOTO_SESSION_WIFI

#include <sys/time.h>
#include <time.h>

#include "display_settings.h"
#include "log.h"

// Midnight UTC on the shoot date; the time-of-day is taken from the shared
// sell-mode constants so night mode and the logs can never disagree with the
// face. No timezone is configured (configTzTime is never called), so the
// C library stays on UTC and there is no DST edge to think about.
#define PHOTO_CLOCK_EPOCH_DAY 1787184000UL  // 2026-08-20 00:00:00 UTC
#ifndef PHOTO_CLOCK_EPOCH
#define PHOTO_CLOCK_EPOCH \
  (PHOTO_CLOCK_EPOCH_DAY + SELL_MODE_HOUR * 3600UL + SELL_MODE_MINUTE * 60UL)
#endif

inline void photoPinSystemClock() {
  struct timeval tv;
  tv.tv_sec = static_cast<time_t>(PHOTO_CLOCK_EPOCH);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

// Call instead of initTimeSync(). displaySettings.begin() must have run first.
inline void photoInitSellTime() {
  photoPinSystemClock();
  // Volatile on purpose: forcing it through the persisting setter would leave
  // sell_on=true in NVS, and a clock returned to stable firmware would then
  // show the sell time to its owner forever. The admin toggle still persists,
  // because there the operator meant it.
  displaySettings.setSellModeVolatile(true);
  logInfo("🕰️ [photo] NTP skipped; sell time on. Toggle it in the admin portal.");
}

inline void photoTickSellTime(unsigned long nowMs) {
  static unsigned long lastPinMs = 0;
  if (lastPinMs != 0 && nowMs - lastPinMs < 1000UL) return;
  lastPinMs = nowMs;
  photoPinSystemClock();
}

#endif  // PHOTO_SESSION_WIFI
