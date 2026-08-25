#include "log.h"

#ifdef PIO_UNIT_TESTING

LogLevel LOG_LEVEL = DEFAULT_LOG_LEVEL;

void log(String, int) {}

void logln(String, int) {}

void setLogLevel(LogLevel level) {
  LOG_LEVEL = level;
}

void initLogSettings() {}

void logEnableFileSink() {}

void logPauseFileSink() {}

void logResumeFileSink() {}

void logFlushFile() {}

String logLatestFilePath() {
  return String();
}

void logRewriteUnsynced() {}

bool logSinkHealthy() {
  return true;
}

uint32_t logSinkFailureCount() {
  return 0;
}

uint32_t logBytesOnDisk() {
  return 0;
}

#else

#include <Preferences.h>
#include <time.h>
#include <stdlib.h>
#include "fs_compat.h"
#include "log_sink_health.h"

LogLevel LOG_LEVEL = DEFAULT_LOG_LEVEL;

String logBuffer[LOG_BUFFER_SIZE];
int logIndex = 0;

// Whether the sink is SUPPOSED to be running, which is a different question
// from whether it is working. This one is set once by logEnableFileSink() and
// never cleared; a fault lives in sinkHealth instead, where it can heal. The
// two used to be one flag, and merging them is what made a transient failure
// permanent.
static bool fileSinkConfigured = false;
// Set while the littlefs partition is being rewritten raw (fs OTA, manual fs
// upload). A write through the stale mount in that window lands on blocks the
// new image owns; volatile because the OTA task flips it while logln runs on
// the main loop.
static volatile bool fileSinkPaused = false;
static LogSinkHealth sinkHealth;
static File logFile;
static String currentLogTag;
static unsigned long lastFlushMs = 0;
static const unsigned long LOG_FLUSH_INTERVAL_MS = 5000;
static uint32_t LOG_RETENTION_DAYS = 1;
static bool LOG_DELETE_ON_BOOT = true;

static void closeLogFile() {
  if (logFile) {
    logFile.flush();
    logFile.close();
  }
}

// Drop the handle and start the recovery clock. Deliberately silent: this runs
// from inside log(), so a logWarn() here would recurse straight back into it.
// The fleet learns about it from the heartbeat instead, which is the half of
// this fix that lives in the portal.
static void failSink() {
  closeLogFile();
  logFile = File();
  currentLogTag = "";
  sinkHealth.noteFailure(millis(), LOG_SINK_RETRY_INTERVAL_MS);
}

static String determineLogTag() {
  time_t now = time(nullptr);
  if (now < 1640995200) {
    return String("unsynced");
  }
  struct tm lt = {};
  localtime_r(&now, &lt);
  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &lt);
  return String(buf);
}

static void ensureLogDirectory() {
  if (!FS_IMPL.exists("/logs")) {
    FS_IMPL.mkdir("/logs");
  }
}

static void ensureLogFile() {
  if (!fileSinkConfigured) return;
  String tag = determineLogTag();
  if (tag.length() == 0) return;
  if (!logFile || tag != currentLogTag) {
    // Throttle only the reopen, and only while the sink is down. The healthy
    // path (first line after boot, and every midnight rollover) is unchanged.
    if (!sinkHealth.shouldAttempt(millis())) return;
    closeLogFile();
    ensureLogDirectory();
    // Cleanup old logs before opening new file
    File dir = FS_IMPL.open("/logs");
    if (dir) {
      time_t now = time(nullptr);
      const time_t cutoff = (LOG_RETENTION_DAYS > 0) ? now - (LOG_RETENTION_DAYS * 86400UL) : 0;
      while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
          String name = entry.name();
          if (name.startsWith("/")) name = name.substring(1);
          if (name.startsWith("logs/")) name = name.substring(5);
          bool removeFile = false;
          if (name.startsWith("unsynced")) {
            // Remove unsynced logs older than 1 day
            if (entry.getLastWrite() > 0 && now >= 86400UL && (now - entry.getLastWrite()) > 86400UL) {
              removeFile = true;
            }
          } else if (cutoff > 0 && entry.getLastWrite() > 0) {
            if (entry.getLastWrite() < cutoff) {
              removeFile = true;
            }
          }
          if (removeFile) {
            String full = entry.name();
            entry.close();
            FS_IMPL.remove(full);
            continue;
          }
        }
        entry.close();
      }
      dir.close();
    }
    String path = String("/logs/") + tag + ".log";
    logFile = FS_IMPL.open(path, "a");
    if (!logFile) {
#ifdef ENABLE_DEBUG_LOGGING
      Serial.println("[log] Failed to open log file for writing: " + path);
#endif
      // Was `fileSinkEnabled = false`, with nothing anywhere to set it back.
      // The failure is now recorded and retried instead, because the reasons an
      // open fails here are overwhelmingly transient: a full filesystem that
      // the prune above may itself clear on the next attempt, or a directory
      // handle upset by something else on the device.
      sinkHealth.noteFailure(millis(), LOG_SINK_RETRY_INTERVAL_MS);
      return;
    }
    currentLogTag = tag;
    sinkHealth.noteSuccess();
  }
}

static inline const char* levelToTag(int level) {
  switch (level) {
    case LOG_LEVEL_DEBUG: return "DEBUG";
    case LOG_LEVEL_INFO:  return "INFO";
    case LOG_LEVEL_WARN:  return "WARN";
    case LOG_LEVEL_ERROR: return "ERROR";
    default:              return "INFO";
  }
}

static String makeLogPrefix(int level) {
  // Prefer localtime_r with TZ applied; fall back to uptime if RTC not set yet
  time_t now = time(nullptr);
  // Consider time unsynced if before 2022-01-01
  if (now < 1640995200) {
    unsigned long nowMs = millis();
    char out[64];
    snprintf(out, sizeof(out), "[uptime %lu.%03lus][%s] ", nowMs/1000UL, nowMs%1000UL, levelToTag(level));
    return String(out);
  }

  struct tm lt = {};
  localtime_r(&now, &lt);
  char datebuf[32];
  char tzbuf[8];
  strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M:%S", &lt);
  strftime(tzbuf, sizeof(tzbuf), "%Z", &lt);
  unsigned long ms = millis() % 1000UL;
  char out[80];
  // Format: [YYYY-MM-DD HH:MM:SS.mmm TZ][LEVEL]
  snprintf(out, sizeof(out), "[%s.%03lu %s][%s] ", datebuf, ms, tzbuf, levelToTag(level));
  return String(out);
}

void log(String msg, int level) {
  // Filter: only log messages at or above current threshold
  if (level < LOG_LEVEL) return;

  // if (telnetClient && telnetClient.connected()) {
  //   telnetClient.print(msg);
  // }

  String line = makeLogPrefix(level) + msg;
#ifdef ENABLE_DEBUG_LOGGING
  Serial.print(line);
#endif

  if (fileSinkConfigured && !fileSinkPaused) {
    ensureLogFile();
    if (logFile) {
      // print() has always returned how many bytes it took and nothing ever
      // looked. That is the other half of the silent failure: a handle that
      // stays truthy while every write goes nowhere leaves the sink "enabled",
      // "open" and producing nothing, which is indistinguishable from a quiet
      // clock in every readout we have.
      size_t wrote = logFile.print(line);
      if (wrote != line.length()) {
        failSink();
      } else {
        unsigned long now = millis();
        if (lastFlushMs == 0 || (now - lastFlushMs) >= LOG_FLUSH_INTERVAL_MS || line.endsWith("\n")) {
          logFile.flush();
          lastFlushMs = now;
        }
      }
    }
  }

  // Store in ring buffer any message that passes the filter
  logBuffer[logIndex] = line;
  logIndex = (logIndex + 1) % LOG_BUFFER_SIZE;
}

void logln(String msg, int level) {
  log(msg + "\n", level);
}

void setLogLevel(LogLevel level) {
  LOG_LEVEL = level;
  // Persist new level
  Preferences prefs;
  prefs.begin("wc_log", false);
  prefs.putUChar("level", (uint8_t)level);
  prefs.end();
}

void setLogRetentionDays(uint32_t days) {
  if (days < 1) days = 1;
  if (days > 10) days = 10;
  LOG_RETENTION_DAYS = days;
  Preferences prefs;
  prefs.begin("wc_log", false);
  prefs.putUInt("retention", days);
  prefs.end();
}

uint32_t getLogRetentionDays() {
  return LOG_RETENTION_DAYS;
}

void setLogDeleteOnBoot(bool enabled) {
  LOG_DELETE_ON_BOOT = enabled;
  Preferences prefs;
  prefs.begin("wc_log", false);
  prefs.putBool("delOnBoot", enabled);
  prefs.end();
}

bool getLogDeleteOnBoot() {
  return LOG_DELETE_ON_BOOT;
}

void initLogSettings() {
  // Load persisted settings if available
  Preferences prefs;
  prefs.begin("wc_log", true);
  uint8_t lvl = prefs.getUChar("level", (uint8_t)DEFAULT_LOG_LEVEL);
  LOG_RETENTION_DAYS = prefs.getUInt("retention", 1);
  LOG_DELETE_ON_BOOT = prefs.getBool("delOnBoot", true);
  prefs.end();

  if (lvl <= LOG_LEVEL_ERROR) {
    LOG_LEVEL = (LogLevel)lvl;
  } else {
    LOG_LEVEL = DEFAULT_LOG_LEVEL;
  }

  // Apply timezone as early as possible so logs use local time
  setenv("TZ", TZ_INFO, 1);
  tzset();
}

static void wipeLogsDirectory() {
  File dir = FS_IMPL.open("/logs");
  if (!dir) return;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String full = entry.name();
      entry.close();
      FS_IMPL.remove(full);
    } else {
      entry.close();
    }
  }
  dir.close();
}

void logEnableFileSink() {
  if (LOG_DELETE_ON_BOOT) {
    wipeLogsDirectory();
#ifdef ENABLE_DEBUG_LOGGING
    Serial.println("[log] Deleted all logs on boot as per settings.");
#endif
  }

  // Low-space recovery: if the filesystem is nearly full (e.g. wordclock-mini's
  // 1.25 MB partition has filled with old logs because delete-on-boot was off),
  // wipe /logs unconditionally so the device can recover on its own.
  bool recoveredFromLowSpace = false;
  size_t total = FS_IMPL.totalBytes();
  size_t used = FS_IMPL.usedBytes();
  if (total > 0 && (total - used) < (64UL * 1024UL)) {
    wipeLogsDirectory();
    recoveredFromLowSpace = true;
#ifdef ENABLE_DEBUG_LOGGING
    Serial.printf("[log] Low filesystem space (%u/%u bytes used); wiped /logs to recover.\n",
                  (unsigned)used, (unsigned)total);
#endif
  }

  fileSinkConfigured = true;
  // Clear any pending retry so a re-arm from the clear-logs route takes effect
  // at once rather than up to a minute later. The failure COUNT is deliberately
  // left alone: it means "since boot", and clearing the logs is not evidence
  // that the sink never dropped out.
  sinkHealth.noteSuccess();
  currentLogTag = "";
  ensureLogFile();
  if (recoveredFromLowSpace) {
    logWarn("/logs wiped: filesystem was nearly full");
  }
}

// Reported on the heartbeat. Three fields rather than one, because they answer
// three different questions and the fleet needs all three: is it writing right
// now, has it ever stopped since boot, and is anything actually landing on
// disk. A clock that recovered at 03:00 looks identical to a healthy one on the
// first field alone.
bool logSinkHealthy() {
  return fileSinkConfigured && sinkHealth.healthy;
}

uint32_t logSinkFailureCount() {
  return sinkHealth.failures;
}

// Total bytes under /logs. The only signal that catches the worst variant of
// this fault, where writes report success and the data still is not there:
// bytes that do not move over hours on a device that is up, at debug level and
// beaconing. Cheap enough to do hourly, and it is the same directory walk
// /api/logs/summary already does on demand.
uint32_t logBytesOnDisk() {
  File dir = FS_IMPL.open("/logs");
  if (!dir) return 0;
  uint32_t total = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      total += (uint32_t)entry.size();
    }
    entry.close();
  }
  dir.close();
  return total;
}

void logCloseFile() {
  closeLogFile();
}

void logPauseFileSink() {
  // Flag first, then close: a line logged from another task between the two
  // must not reopen the file.
  fileSinkPaused = true;
  closeLogFile();
}

void logResumeFileSink() {
  fileSinkPaused = false;
}

void logFlushFile() {
  if (logFile) {
    logFile.flush();
    lastFlushMs = millis();
  }
}

String logLatestFilePath() {
  ensureLogFile();
  if (!FS_IMPL.exists("/logs")) return "";
  File dir = FS_IMPL.open("/logs");
  if (!dir) return "";
  String latest = "";
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (name.length()) {
        if (latest.length() == 0 || name.compareTo(latest) > 0) {
          latest = name;
        }
      }
    }
    entry.close();
  }
  dir.close();
  return latest;
}

// Rewrites unsynced (uptime-based) logs into a dated log once time is synced.
void logRewriteUnsynced() {
  const char* UNSYNCED = "/logs/unsynced.log";
  time_t now = time(nullptr);
  // Only run if time is valid and the unsynced log exists
  if (now < 1640995200) return;
  if (!FS_IMPL.exists(UNSYNCED)) return;

  File in = FS_IMPL.open(UNSYNCED, "r");
  if (!in) return;

  String outPath = String("/logs/") + determineLogTag() + String(".log");
  File out = FS_IMPL.open(outPath, "a");
  if (!out) {
    in.close();
    return;
  }

  // Approximate boot epoch from current epoch minus uptime (millis)
  uint64_t nowMs = millis();
  uint64_t nowEpochMs = ((uint64_t)now) * 1000ULL;
  uint64_t bootEpochMs = (nowEpochMs > nowMs) ? (nowEpochMs - nowMs) : 0;

  bool converted = false;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    if (line.length() == 0) continue;
    // Expected prefix: [uptime %lu.%03lus][LEVEL] ...
    int upPos = line.indexOf("[uptime ");
    if (upPos != 0) continue;
    int dotPos = line.indexOf('.', upPos + 8);
    int sPos = line.indexOf("s][", dotPos);
    if (dotPos < 0 || sPos < 0) continue;
    int lvlStart = sPos + 3;
    int lvlEnd = line.indexOf(']', lvlStart);
    if (lvlEnd < 0) continue;
    String secStr = line.substring(upPos + 8, dotPos);
    String msStr = line.substring(dotPos + 1, sPos);
    String lvlStr = line.substring(lvlStart, lvlEnd);
    String msg = line.substring(lvlEnd + 2); // skip "] "
    uint64_t upSec = (uint64_t)secStr.toInt();
    uint64_t upMs = (uint64_t)msStr.toInt();
    uint64_t lineMs = bootEpochMs + (upSec * 1000ULL) + upMs;
    time_t lineSec = (time_t)(lineMs / 1000ULL);
    uint16_t lineMsPart = (uint16_t)(lineMs % 1000ULL);
    struct tm lt = {};
    localtime_r(&lineSec, &lt);
    char datebuf[32];
    char tzbuf[8];
    strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M:%S", &lt);
    strftime(tzbuf, sizeof(tzbuf), "%Z", &lt);
    char prefix[96];
    snprintf(prefix, sizeof(prefix), "[%s.%03u %s][%s] ", datebuf, (unsigned)lineMsPart, tzbuf, lvlStr.c_str());
    out.print(prefix);
    out.println(msg);
    converted = true;
  }
  out.flush();
  in.close();
  if (converted) {
    FS_IMPL.remove(UNSYNCED);
  }
}

#endif // PIO_UNIT_TESTING

// Outside the test/firmware split on purpose: LOG_LEVEL exists in both, and
// the fleet needs the same spelling from both. The codes are the wire format
// of the heartbeat's `logLevel` field and the value_code column of the
// `log_level` LoV in the portal, so changing one means changing all three.
const char* logLevelName() {
  switch (LOG_LEVEL) {
    case LOG_LEVEL_DEBUG: return "debug";
    case LOG_LEVEL_INFO:  return "info";
    case LOG_LEVEL_WARN:  return "warn";
    case LOG_LEVEL_ERROR: return "error";
    default:              return "error";
  }
}
