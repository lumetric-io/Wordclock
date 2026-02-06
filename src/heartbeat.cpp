#include "heartbeat.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "config.h"
#include "device_identity.h"
#include "display_settings.h"
#include "grid_layout.h"
#include "log.h"
#include "ota_updater.h"
#include "secrets.h"

// State
static unsigned long s_lastHeartbeatMs = 0;
static bool s_initialized = false;
static bool s_triggerPending = false;
static bool s_startupDelayComplete = false;
static unsigned long s_startupMs = 0;

// Forward declarations
static bool isAtHalfMinute();
static bool shouldSendHeartbeat(unsigned long nowMs);

void initHeartbeat() {
  s_lastHeartbeatMs = 0;
  s_initialized = true;
  s_triggerPending = false;
  s_startupDelayComplete = false;
  s_startupMs = millis();
  logInfo("💓 Heartbeat module initialized");
}

void triggerHeartbeat() {
  s_triggerPending = true;
  logDebug("💓 Heartbeat triggered");
}

void processHeartbeat(unsigned long nowMs) {
  if (!s_initialized) return;
  
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
  
  // Check if we should send heartbeat
  if (!shouldSendHeartbeat(nowMs)) return;
  
  // Send heartbeat
  if (sendHeartbeat()) {
    s_lastHeartbeatMs = nowMs;
    s_triggerPending = false;
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
  req["ip"] = WiFi.localIP().toString();
  
  // Grid variant
  const GridVariantInfo* gridInfo = getGridVariantInfo(getActiveGridVariant());
  if (gridInfo && gridInfo->key) {
    req["gridVariant"] = gridInfo->key;
  }
  
  String payload;
  serializeJson(req, payload);
  
  logDebug("💓 Sending heartbeat to " + url);
  
  int code = http.POST(payload);
  
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
