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

void logFlushFile() {}

String logLatestFilePath() {
  return String();
}

void logRewriteUnsynced() {}

#else

#include <Preferences.h>
#include <time.h>
#include <stdlib.h>
#include "fs_compat.h"

LogLevel LOG_LEVEL = DEFAULT_LOG_LEVEL;

String logBuffer[LOG_BUFFER_SIZE];
int logIndex = 0;

static bool fileSinkEnabled = false;
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
  if (!fileSinkEnabled) return;
  String tag = determineLogTag();
  if (tag.length() == 0) return;
  if (!logFile || tag != currentLogTag) {
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
      fileSinkEnabled = false;
      return;
    }
    currentLogTag = tag;
    // Truncate overly large log directory by deleting files older than 7 days
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

  if (fileSinkEnabled) {
    ensureLogFile();
    if (logFile) {
      logFile.print(line);
      unsigned long now = millis();
      if (lastFlushMs == 0 || (now - lastFlushMs) >= LOG_FLUSH_INTERVAL_MS || line.endsWith("\n")) {
        logFile.flush();
        lastFlushMs = now;
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
  LOG_DELETE_ON_BOOT = prefs.getBool("delOnBoot", false);
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

void logEnableFileSink() {
  if (LOG_DELETE_ON_BOOT) {
    File dir = FS_IMPL.open("/logs");
    if (dir) {
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
#ifdef ENABLE_DEBUG_LOGGING
    Serial.println("[log] Deleted all logs on boot as per settings.");
#endif
  }

  fileSinkEnabled = true;
  currentLogTag = "";
  ensureLogFile();
}

void logCloseFile() {
  closeLogFile();
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
