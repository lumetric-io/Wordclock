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

// The shoot date. The time-of-day comes from the shared sell-mode constants so
// night mode and the logs can never disagree with the face.
//
// This is a *local* wall-clock time, converted with mktime() rather than
// written as a fixed epoch. The timezone is live even on this build:
// initLogSettings() calls setenv("TZ", TZ_INFO) + tzset() in log.cpp, quite
// independently of initTimeSync(). Pinning a UTC epoch therefore put the face
// two hours ahead of the intended time all summer (CEST = UTC+2). tm_isdst=-1
// lets the TZ rules pick the offset, so this stays right across the October
// changeover too.
#define PHOTO_CLOCK_YEAR 2026
#define PHOTO_CLOCK_MONTH 8
#define PHOTO_CLOCK_DAY 20

inline void photoPinSystemClock() {
  struct tm local = {};
  local.tm_year = PHOTO_CLOCK_YEAR - 1900;
  local.tm_mon = PHOTO_CLOCK_MONTH - 1;
  local.tm_mday = PHOTO_CLOCK_DAY;
  local.tm_hour = SELL_MODE_HOUR;
  local.tm_min = SELL_MODE_MINUTE;
  local.tm_sec = 0;
  local.tm_isdst = -1;  // let the TZ rules decide
  struct timeval tv;
  tv.tv_sec = mktime(&local);
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
