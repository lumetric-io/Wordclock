#pragma once

#include <time.h>
#include "log.h"
#include "config.h"
#include "led_events.h"

extern bool g_initialTimeSyncSucceeded;

// Initialize time synchronization via NTP
// This function sets the timezone and NTP servers, and waits until the time is successfully retrieved.
// Provides status messages via logging.
inline void initTimeSync(const char* tzInfo, const char* ntp1, const char* ntp2) {
    g_initialTimeSyncSucceeded = false;
    configTzTime(tzInfo, ntp1, ntp2); // Set timezone and NTP servers
    logInfo("⌛ Waiting for NTP...");
    struct tm timeinfo;
    unsigned long start = millis();
    bool synced = false;
    while (millis() - start < TIME_SYNC_TIMEOUT_MS) {
        if (getLocalTime(&timeinfo)) {
            synced = true;
            break;
        }
        logDebug(".");
        delay(500);
    }
    if (!synced) {
        logWarn("⌛ NTP timeout; proceeding without synced time");
        ledEventStart(LedEvent::NtpFailed);
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d",
             timeinfo.tm_mday,
             timeinfo.tm_mon + 1,
             timeinfo.tm_hour,
             timeinfo.tm_min);
    logInfo(String("🕒 Time synchronized: ") + buf);
    g_initialTimeSyncSucceeded = true;
    logRewriteUnsynced(); // rewrite uptime-based logs with real timestamps now that time is synced
    ledEventStop(LedEvent::NtpFailed);
}
