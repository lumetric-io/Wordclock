#include "heartbeat.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "device_identity.h"
#include "device_registration.h"
#include "display_settings.h"
#include "grid_layout.h"
#include "led_state.h"
#include "log.h"
#include "night_mode.h"
#include "ota_updater.h"
#include "secrets.h"
#include "setup_state.h"

// Retry interval after failure (5 minutes)
#define HEARTBEAT_RETRY_INTERVAL_MS (5 * 60 * 1000UL)

// State
static unsigned long s_lastHeartbeatMs = 0;
static unsigned long s_lastFailureMs = 0;
static bool s_initialized = false;
static bool s_triggerPending = false;
static bool s_startupDelayComplete = false;
static unsigned long s_startupMs = 0;
/** When true, heartbeat is permanently stopped after re-register failed following 401 */
static bool s_heartbeatStopped = false;
/** Last HTTP status from sendHeartbeat (0 if no response or not yet sent) */
static int s_lastHeartbeatHttpCode = 0;

// Forward declarations
static bool isAtHalfMinute();
static bool shouldSendHeartbeat(unsigned long nowMs);

void initHeartbeat() {
  s_lastHeartbeatMs = 0;
  s_lastFailureMs = 0;
  s_initialized = true;
  s_triggerPending = false;
  s_startupDelayComplete = false;
  s_startupMs = millis();
  s_heartbeatStopped = false;
  s_lastHeartbeatHttpCode = 0;
  logInfo("💓 Heartbeat module initialized");
}

void triggerHeartbeat() {
  s_triggerPending = true;
  logDebug("💓 Heartbeat triggered");
}

void processHeartbeat(unsigned long nowMs) {
  if (!s_initialized) return;
  if (s_heartbeatStopped) return;

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) return;
  
  // Check if device is registered
  String deviceId = get_device_id();
  String deviceToken = get_device_token();
  if (deviceId.isEmpty() || deviceToken.isEmpty()) return;
  
  // Handle startup delay
  if (!s_startupDelayComplete) {
    if (nowMs - s_startupMs < HEARTBEAT_STARTUP_DELAY_MS) {
      return;
    }
    s_startupDelayComplete = true;
    s_triggerPending = true;  // Send first heartbeat after startup
    logDebug("💓 Startup delay complete, will send first heartbeat");
  }
  
  // Check if we're in retry cooldown after a failure
  if (s_lastFailureMs > 0 && nowMs - s_lastFailureMs < HEARTBEAT_RETRY_INTERVAL_MS) {
    return;
  }
  
  // Check if we should send heartbeat
  if (!shouldSendHeartbeat(nowMs)) return;
  
  // Send heartbeat
  if (sendHeartbeat()) {
    s_lastHeartbeatMs = nowMs;
    s_lastFailureMs = 0;  // Reset failure state on success
    s_triggerPending = false;
  } else if (s_lastHeartbeatHttpCode == 401) {
    // Unauthorized: re-register to refresh credentials, then send first heartbeat
    logWarn("💓 Heartbeat 401: re-registering to refresh credentials");
    String outId, outToken, outError;
    if (register_device_with_fleet(outId, outToken, outError)) {
      logInfo("💓 Re-registered successfully, sending first heartbeat");
      s_lastFailureMs = 0;
      s_triggerPending = true;
      if (sendHeartbeat()) {
        s_lastHeartbeatMs = nowMs;
        s_triggerPending = false;
      } else {
        s_lastFailureMs = nowMs;
      }
    } else {
      logError("💓 Re-register failed: " + outError + " – stopping heartbeat");
      s_heartbeatStopped = true;
    }
  } else {
    s_lastFailureMs = nowMs;  // Start retry cooldown
  }
}

static bool shouldSendHeartbeat(unsigned long nowMs) {
  // Triggered heartbeat (e.g., after WiFi reconnect)
  if (s_triggerPending && isAtHalfMinute()) {
    return true;
  }
  
  // Regular interval check
  if (s_lastHeartbeatMs == 0) {
    // First heartbeat - wait for trigger or half minute
    return s_triggerPending && isAtHalfMinute();
  }
  
  // Check if interval has passed
  if (nowMs - s_lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) {
    return false;
  }
  
  // Only send at :30 seconds to avoid LED update conflicts
  return isAtHalfMinute();
}

static bool isAtHalfMinute() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    // If time not available, allow heartbeat anyway
    return true;
  }
  // Send between :28 and :32 seconds (4-second window)
  return timeinfo.tm_sec >= 28 && timeinfo.tm_sec <= 32;
}

bool sendHeartbeat() {
  s_lastHeartbeatHttpCode = 0;

  String deviceId = get_device_id();
  String deviceToken = get_device_token();
  
  if (deviceId.isEmpty() || deviceToken.isEmpty()) {
    logWarn("💓 Cannot send heartbeat: device not registered");
    return false;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    logWarn("💓 Cannot send heartbeat: WiFi not connected");
    return false;
  }
  
  const String url = String(API_BASE_URL) + "/api/v1/devices/heartbeat";
  
  WiFiClientSecure client;
  client.setInsecure();  // Skip certificate validation (same as registration)
  
  HTTPClient http;
  if (!http.begin(client, url)) {
    logWarn("💓 http.begin failed");
    return false;
  }
  
  http.addHeader("Content-Type", "application/json");
  http.addHeader(DEVICE_API_HEADER, deviceToken);
  http.setTimeout(15000);  // 15 second timeout
  
  // Build payload
  JsonDocument req;
  req["deviceId"] = deviceId;
  req["firmware"] = FIRMWARE_VERSION;
  req["ui"] = getUiVersion();
  req["channel"] = displaySettings.getUpdateChannel();
  req["uptime"] = (long)(millis() / 1000);
  req["freeHeap"] = (long)ESP.getFreeHeap();
  req["rssi"] = WiFi.RSSI();
  
  // Grid variant
  const GridVariantInfo* gridInfo = getGridVariantInfo(getActiveGridVariant());
  if (gridInfo && gridInfo->key) {
    req["gridVariant"] = gridInfo->key;
  }
  
  // Extended system diagnostics
  req["minFreeHeap"] = (long)ESP.getMinFreeHeap();
  req["heapSize"] = (long)ESP.getHeapSize();
  req["cpuFreqMhz"] = ESP.getCpuFreqMHz();
  req["chipTemp"] = temperatureRead();
  // resetReason: esp_reset_reason_t as int. 0=UNKNOWN, 1=POWERON, 2=EXT, 3=SW, 4=PANIC,
  // 5=INT_WDT, 6=TASK_WDT, 7=WDT, 8=DEEPSLEEP, 9=BROWNOUT, 10=SDIO. See docs/HEARTBEAT_RESET_REASON.md
  req["resetReason"] = (int)esp_reset_reason();
  
  // Wordclock state
  req["brightness"] = ledState.getBrightness();
  req["nightModeActive"] = nightMode.isActive();
  req["setupComplete"] = setupState.isComplete();
  
  String payload;
  serializeJson(req, payload);
  
  logDebug("💓 Sending heartbeat to " + url);
  
  int code = http.POST(payload);
  s_lastHeartbeatHttpCode = (code > 0) ? code : 0;
  
  if (code <= 0) {
    logWarn("💓 HTTP error: " + http.errorToString(code));
    http.end();
    return false;
  }
  
  String body = http.getString();
  http.end();
  
  if (code < 200 || code >= 300) {
    logWarn("💓 Heartbeat failed: HTTP " + String(code) + " - " + body);
    return false;
  }
  
  logInfo("💓 Heartbeat sent successfully");
  return true;
}
