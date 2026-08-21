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

#else

#include "system_utils.h"

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
static const int MAX_COMMANDS = 4;

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
}

void deviceCommandsTestSetLocalTime(bool valid, int hour, int minute) {
  s_testTimeValid = valid;
  s_testTime = {};
  s_testTime.tm_hour = hour;
  s_testTime.tm_min = minute;
}

int deviceCommandsTestRestartCount() { return s_testRestartCount; }

bool deviceCommandsTestRebootArmed() { return s_rebootArmed; }

#endif
