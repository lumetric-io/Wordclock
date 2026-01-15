#include "mock_log.h"

// Implementation of mock log functions
void log(String msg, int level) {
    (void)level;
    (void)msg;
    // Silent in tests
}

void logln(String msg, int level) {
    (void)level;
    (void)msg;
    // Silent in tests
}

void setLogLevel(LogLevel level) { (void)level; }
void setLogRetentionDays(uint32_t days) { (void)days; }
uint32_t getLogRetentionDays() { return 7; }
void setLogDeleteOnBoot(bool enabled) { (void)enabled; }
bool getLogDeleteOnBoot() { return false; }
void initLogSettings() { }
void logEnableFileSink() { }
void logCloseFile() { }
void logFlushFile() { }
String logLatestFilePath() { return String(""); }
void logRewriteUnsynced() { }

// Global log level variable
LogLevel LOG_LEVEL = LOG_LEVEL_INFO;

