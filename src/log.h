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
