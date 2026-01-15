#ifndef MOCK_LOG_H
#define MOCK_LOG_H

#include "mock_arduino.h"
#include <iostream>

// Mock log level enum - only define if not already defined
#ifndef LOG_LEVEL_ENUM_DEFINED
#define LOG_LEVEL_ENUM_DEFINED
enum LogLevel {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR
};
#endif

// Global log level
extern LogLevel LOG_LEVEL;

// Mock logging functions for testing - matching production signatures
void log(String msg, int level);
void logln(String msg, int level);

// Convenience macros - matching production code
#define logDebug(msg) logln(msg, LOG_LEVEL_DEBUG)
#define logInfo(msg)  logln(msg, LOG_LEVEL_INFO)
#define logWarn(msg)  logln(msg, LOG_LEVEL_WARN)
#define logError(msg) logln(msg, LOG_LEVEL_ERROR)

// Mock other log functions
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

#endif // MOCK_LOG_H

