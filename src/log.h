#pragma once
#include <Arduino.h>
// #include "network.h"  // For access to telnetClient
#include "config.h"

#ifndef LOG_LEVEL_ENUM_DEFINED
#define LOG_LEVEL_ENUM_DEFINED
enum LogLevel {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR
};
#endif // LOG_LEVEL_ENUM_DEFINED
extern LogLevel LOG_LEVEL;

// Basic log function
void log(String msg, int level = LOG_LEVEL_INFO);
void logln(String msg, int level = LOG_LEVEL_INFO);

// Convenience functions
#define logDebug(msg) logln(msg, LOG_LEVEL_DEBUG)
#define logInfo(msg)  logln(msg, LOG_LEVEL_INFO)
#define logWarn(msg)  logln(msg, LOG_LEVEL_WARN)
#define logError(msg) logln(msg, LOG_LEVEL_ERROR)

void setLogLevel(LogLevel level);

// Current threshold as a stable lowercase code: "debug" | "info" | "warn" |
// "error". Reported on the heartbeat, so it must not follow the enum's
// numbering — renumbering LogLevel would otherwise rewrite fleet history.
const char* logLevelName();
void setLogRetentionDays(uint32_t days);
uint32_t getLogRetentionDays();
void setLogDeleteOnBoot(bool enabled);
bool getLogDeleteOnBoot();

void initLogSettings();
void logEnableFileSink();
void logCloseFile();
void logFlushFile();
String logLatestFilePath();
void logRewriteUnsynced();

// File sink health, reported on the heartbeat. See src/log_sink_health.h for
// why a sink failure is now recoverable and counted rather than final: the old
// behaviour cost a clock its log file until the next reboot and showed up
// nowhere at all.
bool logSinkHealthy();
uint32_t logSinkFailureCount();
uint32_t logBytesOnDisk();
