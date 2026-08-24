#include "device_commands.h"

#include <ArduinoJson.h>
#include <string.h>
#include <time.h>

#include "log.h"

#ifdef PIO_UNIT_TESTING

// Native stand-ins for the only two things this module does that a PC cannot:
// read the wall clock and restart the chip.
static bool s_testTimeValid = false;
static struct tm s_testTime = {};
static int s_testRestartCount = 0;

static bool readLocalTime(struct tm* out) {
  if (!s_testTimeValid) return false;
  *out = s_testTime;
  return true;
}

static void restartNow() { s_testRestartCount++; }

// The OTA channel reaches this module through two functions rather than
// through display_settings.h, and that indirection is the whole reason this
// file still compiles natively: that header is header-only and opens
// Preferences, so including it here would drag NVS into the test binary.
static String s_testChannel = "stable";
static String currentUpdateChannel() { return s_testChannel; }
static void applyUpdateChannel(const String& channel) { s_testChannel = channel; }

#else

#include "display_settings.h"
#include "system_utils.h"

static String currentUpdateChannel() { return displaySettings.getUpdateChannel(); }
static void applyUpdateChannel(const String& channel) {
  displaySettings.setUpdateChannel(channel);
}

// Timeout 0: never block the render loop waiting for NTP. A clock whose time
// is not yet synced simply does not reboot, which is the intended behaviour
// and not a case worth waiting on.
static bool readLocalTime(struct tm* out) { return getLocalTime(out, 0); }

// safeRestart() flushes settings on the way out, as every other restart path
// in this firmware does.
static void restartNow() { safeRestart(); }

#endif

// Caps, both firmware-side on purpose: a buggy or compromised portal must not
// be able to make a clock allocate its way into a reboot. The real portal
// hands over at most 4 commands and its bodies are a few hundred bytes.
static const size_t MAX_RESPONSE_BYTES = 4096;

// Raised from 4 to 6 when the whitelist grew to five kinds. The table holds at
// most one pending command per kind, so five is now a reachable state, and a
// cap at four would silently defer one of them to the next beat an hour later.
// Six leaves room for one more kind without this becoming a two-repo change
// again. The portal's LIMIT mirrors it; both exist so a runaway issuer cannot
// make a clock parse an unbounded body.
static const int MAX_COMMANDS = 6;

// A reboot is refused below this uptime. This is what stops a clock whose RTC
// is stuck reading 04:00 from rebooting itself in a loop: each boot has to
// survive a quarter of an hour before the scheduled reboot can fire again.
static const unsigned long REBOOT_MIN_UPTIME_MS = 15UL * 60UL * 1000UL;

// The pending reboot lives in RAM only, deliberately. Losing it to a power cut
// is not a failure: the command is still pending server-side and comes back on
// the next beat.
static bool s_rebootArmed = false;
static bool s_rebootImmediate = false;
static int s_rebootHour = -1;
static int s_rebootMinute = -1;

static bool parseLogLevel(const char* name, LogLevel& out) {
  if (strcmp(name, "debug") == 0) { out = LOG_LEVEL_DEBUG; return true; }
  if (strcmp(name, "info") == 0)  { out = LOG_LEVEL_INFO;  return true; }
  if (strcmp(name, "warn") == 0)  { out = LOG_LEVEL_WARN;  return true; }
  if (strcmp(name, "error") == 0) { out = LOG_LEVEL_ERROR; return true; }
  return false;
}

// Strict HH:MM, 24 hour. The portal validates this too; it is re-checked here
// because the whole point of a firmware-side whitelist is that it holds even
// when the other end is wrong.
static bool parseHourMinute(const char* at, int& hour, int& minute) {
  if (strlen(at) != 5) return false;
  if (at[2] != ':') return false;
  for (int i = 0; i < 5; i++) {
    if (i == 2) continue;
    if (at[i] < '0' || at[i] > '9') return false;
  }
  const int h = (at[0] - '0') * 10 + (at[1] - '0');
  const int m = (at[3] - '0') * 10 + (at[4] - '0');
  if (h > 23 || m > 59) return false;
  hour = h;
  minute = m;
  return true;
}

static void applySetLogLevel(JsonObjectConst args, int id) {
  const char* level = args["level"];
  if (!level) {
    logWarn("Fleet command set_log_level without a level, ignored");
    return;
  }

  LogLevel target;
  if (!parseLogLevel(level, target)) {
    logWarn(String("Fleet command set_log_level with unknown level: ") + level);
    return;
  }

  // Already there. This is the common case, not the exception: the command
  // stays pending until the portal sees the new level on a *subsequent* beat,
  // so it is handed over at least once more after it has taken. Returning
  // early keeps that from writing NVS every hour for no change.
  if (LOG_LEVEL == target) return;

  setLogLevel(target);
  logWarn(String("Log level set to ") + level + " by fleet command #" + id);
}

// The counterpart to set_log_level, and only worth having alongside it. Turning
// a distant clock up to `debug` buys nothing if the fault shows during boot:
// delete-on-boot defaults to on, so the log describing the boot is wiped by the
// boot after it. This is the switch that makes remote diagnosis of a boot
// problem possible at all.
//
// Commandable in both directions on purpose. Off is not a safe resting state —
// the mini's 1.25 MB partition fills — so whoever turns it off must be able to
// turn it back on without a house call. The firmware is not defenceless if they
// forget: logEnableFileSink() wipes /logs unconditionally below 64 kB free, and
// LOG_RETENTION_DAYS still prunes at every file open.
static void applySetLogDeleteOnBoot(JsonObjectConst args, int id) {
  JsonVariantConst enabled = args["enabled"];
  if (!enabled.is<bool>()) {
    logWarn("Fleet command set_log_delete_on_boot without a boolean, ignored");
    return;
  }

  const bool target = enabled.as<bool>();

  // Same reason as set_log_level: the command is handed over at least once more
  // after it has taken, because the portal only learns it took from a later
  // beat. Returning early keeps that from writing NVS every hour for no change.
  if (getLogDeleteOnBoot() == target) return;

  setLogDeleteOnBoot(target);
  logWarn(String("Log delete-on-boot set to ") + (target ? "on" : "off") +
          " by fleet command #" + id);
}

// The third leg of the same triangle: how much is logged (set_log_level),
// whether it survives a boot (set_log_delete_on_boot), and how long it is
// kept. The three are not interchangeable. Retention pruning runs only when
// the log file is opened, which is at boot and at midnight, so a clock with
// delete-on-boot off but retention at the default 1 day still loses yesterday
// at the first midnight. That is enough to read one boot and not enough to
// catch anything intermittent.
//
// Range is 1 to 10, and out of range is REFUSED rather than clamped. The
// setter clamps, which would be the friendly thing to do everywhere else and
// is exactly wrong here: a portal asking for 30 and a device reporting 10
// means the completion test can never match, so the command would be re-sent
// until it expired and then be reported as stuck having worked perfectly.
static void applySetLogRetentionDays(JsonObjectConst args, int id) {
  JsonVariantConst days = args["days"];
  if (!days.is<int>()) {
    logWarn("Fleet command set_log_retention_days without a number, ignored");
    return;
  }

  const int target = days.as<int>();
  if (target < 1 || target > 10) {
    logWarn(String("Fleet command set_log_retention_days out of range: ") + target);
    return;
  }

  if ((int)getLogRetentionDays() == target) return;

  setLogRetentionDays((uint32_t)target);
  logWarn(String("Log retention set to ") + target + " days by fleet command #" + id);
}

// The one command here that costs no heartbeat field at all: the beat has
// reported `channel` since long before P4.10, so the completion test was
// already in the data.
//
// It sits just inside the policy line rather than comfortably inside it. The
// customer cannot see the channel from the clock face, but it does decide
// which firmware they receive, so this is for moving one unit to `early` for a
// release test and moving it back, not for steering the fleet. Anything that
// changes what a customer's clock looks like stays out.
//
// Validated here even though the portal validates too, and for a sharper
// reason than the usual belt and braces: setUpdateChannel() silently falls
// back to "stable" on an unknown value, so passing a typo straight through
// would quietly move a clock to a channel nobody asked for, and the command
// asking for it could never close.
static void applySetUpdateChannel(JsonObjectConst args, int id) {
  const char* channel = args["channel"];
  if (!channel) {
    logWarn("Fleet command set_update_channel without a channel, ignored");
    return;
  }

  if (strcmp(channel, "stable") != 0 &&
      strcmp(channel, "early") != 0 &&
      strcmp(channel, "develop") != 0) {
    logWarn(String("Fleet command set_update_channel with unknown channel: ") + channel);
    return;
  }

  if (currentUpdateChannel() == channel) return;

  applyUpdateChannel(String(channel));
  logWarn(String("Update channel set to ") + channel + " by fleet command #" + id);
}

static void applyReboot(JsonObjectConst args, int id) {
  const char* at = args["at"];
  if (!at) at = "now";

  if (strcmp(at, "now") == 0) {
    if (!s_rebootArmed || !s_rebootImmediate) {
      logWarn(String("Reboot armed by fleet command #") + id);
    }
    s_rebootArmed = true;
    s_rebootImmediate = true;
    return;
  }

  int hour = 0;
  int minute = 0;
  if (!parseHourMinute(at, hour, minute)) {
    logWarn(String("Fleet command reboot with unusable time: ") + at);
    return;
  }

  // Re-arming the same target is what happens on every beat between issuing
  // and 04:00, up to eighteen times. It is harmless and must be silent.
  if (s_rebootArmed && !s_rebootImmediate &&
      s_rebootHour == hour && s_rebootMinute == minute) {
    return;
  }

  s_rebootArmed = true;
  s_rebootImmediate = false;
  s_rebootHour = hour;
  s_rebootMinute = minute;
  logWarn(String("Reboot scheduled for ") + at + " local time by fleet command #" + id);
}

void deviceCommandsHandleResponse(const String& body) {
  if (body.length() == 0 || body.length() > MAX_RESPONSE_BYTES) return;

  // Filtered parse: everything outside `commands[].{id,kind,args}` is dropped
  // during deserialization rather than after, so an unexpected field on the
  // response costs no heap at all.
  JsonDocument filter;
  filter["commands"][0]["id"] = true;
  filter["commands"][0]["kind"] = true;
  filter["commands"][0]["args"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, body.c_str(), body.length(),
                      DeserializationOption::Filter(filter))) {
    return;  // silent: the beat itself already succeeded
  }

  JsonArrayConst commands = doc["commands"].as<JsonArrayConst>();
  if (commands.isNull()) return;

  int seen = 0;
  for (JsonObjectConst command : commands) {
    if (++seen > MAX_COMMANDS) break;

    const char* kind = command["kind"];
    if (!kind) continue;
    const int id = command["id"] | 0;
    JsonObjectConst args = command["args"].as<JsonObjectConst>();

    // The whitelist. An unknown kind is dropped, and dropping it silently
    // server-side is the design: the command stays pending, expires, and turns
    // up in the daily fleet brief. There is no protocol-level error path
    // because that would mean two failure channels to read instead of one.
    if (strcmp(kind, "set_log_level") == 0) {
      applySetLogLevel(args, id);
    } else if (strcmp(kind, "set_log_delete_on_boot") == 0) {
      applySetLogDeleteOnBoot(args, id);
    } else if (strcmp(kind, "set_log_retention_days") == 0) {
      applySetLogRetentionDays(args, id);
    } else if (strcmp(kind, "set_update_channel") == 0) {
      applySetUpdateChannel(args, id);
    } else if (strcmp(kind, "reboot") == 0) {
      applyReboot(args, id);
    } else {
      logWarn(String("Ignoring unknown fleet command: ") + kind);
    }
  }
}

void deviceCommandsTick(unsigned long nowMs) {
  if (!s_rebootArmed) return;

  // Also covers "now": a command that arrives during the first quarter hour
  // waits, which is the same loop guard seen from the other side.
  if (nowMs < REBOOT_MIN_UPTIME_MS) return;

  if (!s_rebootImmediate) {
    struct tm local = {};
    // An unsynced clock never reboots on a schedule it cannot read.
    if (!readLocalTime(&local)) return;
    if (local.tm_hour != s_rebootHour || local.tm_min != s_rebootMinute) return;
  }

  // Cleared before restarting, not after, so a restart that somehow returns
  // cannot re-enter here. No OTA guard is needed: an update runs synchronously
  // inside loop() and this tick cannot interleave with it.
  s_rebootArmed = false;
  s_rebootImmediate = false;
  logWarn("Rebooting on fleet command");
  restartNow();
}

#ifdef PIO_UNIT_TESTING

void deviceCommandsTestReset() {
  s_rebootArmed = false;
  s_rebootImmediate = false;
  s_rebootHour = -1;
  s_rebootMinute = -1;
  s_testTimeValid = false;
  s_testTime = {};
  s_testRestartCount = 0;
  s_testChannel = "stable";
}

void deviceCommandsTestSetLocalTime(bool valid, int hour, int minute) {
  s_testTimeValid = valid;
  s_testTime = {};
  s_testTime.tm_hour = hour;
  s_testTime.tm_min = minute;
}

int deviceCommandsTestRestartCount() { return s_testRestartCount; }

bool deviceCommandsTestRebootArmed() { return s_rebootArmed; }

String deviceCommandsTestUpdateChannel() { return s_testChannel; }

#endif
