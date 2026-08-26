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
#include "language_settings.h"
#include "led_state.h"
#include "log.h"
#include "night_mode.h"
#include "ota_updater.h"
#include "secrets.h"

// Retry interval after failure (5 minutes)
#define HEARTBEAT_RETRY_INTERVAL_MS (5 * 60 * 1000UL)

// sendHeartbeat() runs synchronously inside loop(), so every millisecond it
// spends waiting is a millisecond the display does not animate. A successful
// beacon already costs ~1 s; the old single 15 s timeout meant one dropped
// response froze the clock for fifteen seconds — visible on a wall.
//
// Split in two, because the two waits are not the same risk:
//   - connect covers DNS + TCP + the TLS handshake, which is the slow part on
//     an ESP32 and the part that legitimately needs room on a weak link.
//   - read is the wait for the response to a request already sent. The portal
//     answers this endpoint in well under a second, so anything still absent
//     after 5 s is not coming. Capping it costs nothing and is what keeps a
//     radio dip from stalling the animation.
// Observed 2026-08-17: a beacon at RSSI -91 dBm was written server-side and
// still timed out client-side. See ROADMAP.md.
#define HEARTBEAT_CONNECT_TIMEOUT_MS 10000
#define HEARTBEAT_READ_TIMEOUT_MS 5000

// State
// Active beat rhythm. Starts at the compiled default and follows whatever
// the last successful beat's response said (see applyNextBeatSeconds).
static unsigned long s_heartbeatIntervalMs = HEARTBEAT_INTERVAL_MS;
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
static void applyNextBeatSeconds(const String& body);

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
  if (nowMs - s_lastHeartbeatMs < s_heartbeatIntervalMs) {
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

// Server-driven beat rhythm (portal sql/028). The response may carry
// `nextBeatSeconds`; a valid value (60..86400) becomes the interval for the
// NEXT beat, anything else - absent key, junk, out of range, unparseable
// body - means the compiled default. Re-deriving from scratch on every
// successful beat is the point: the portal states the desired rhythm each
// time, so clearing it server-side rolls the fleet back within one beat and
// no state lingers here. RAM only, filtered parse so an unexpected field
// costs no heap; this is the first consumer of the response body on this
// firmware line (the legacy line also pulls its command downlink from it).
static void applyNextBeatSeconds(const String& body) {
  unsigned long next = HEARTBEAT_INTERVAL_MS;
  if (body.length() > 0 && body.length() <= 4096) {
    JsonDocument filter;
    filter["nextBeatSeconds"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body.c_str(), body.length(),
                        DeserializationOption::Filter(filter)) ==
        DeserializationError::Ok) {
      long secs = doc["nextBeatSeconds"] | 0L;
      if (secs >= 60 && secs <= 86400) {
        next = (unsigned long)secs * 1000UL;
      }
    }
  }
  if (next != s_heartbeatIntervalMs) {
    logInfo(String("💓 Beat interval now ") + String(next / 1000UL) +
            " s (portal-directed)");
    s_heartbeatIntervalMs = next;
  }
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
  http.setConnectTimeout(HEARTBEAT_CONNECT_TIMEOUT_MS);
  http.setTimeout(HEARTBEAT_READ_TIMEOUT_MS);
  
  // Build payload
  JsonDocument req;
  req["deviceId"] = deviceId;
  req["firmware"] = FIRMWARE_VERSION;
  req["ui"] = getUiVersion();
  req["channel"] = displaySettings.getUpdateChannel();
  // The automatic OTA toggle, next to the channel it applies to. Off is a
  // normal owner choice (and forced while the channel is develop); the
  // registry uses this to tell "updates switched off" from "cannot reach the
  // OTA host" when a release is not landing. See portal/sql/026.
  req["autoUpdate"] = displaySettings.getAutoUpdate();
  req["uptime"] = (long)(millis() / 1000);
  req["freeHeap"] = (long)ESP.getFreeHeap();
  req["rssi"] = WiFi.RSSI();
  
  // Grid variant
  const GridVariantInfo* gridInfo = getGridVariantInfo(getActiveGridVariant());
  if (gridInfo && gridInfo->key) {
    req["gridVariant"] = gridInfo->key;
  }

  // Language, dialect and — the one that matters for the rollout — who chose
  // the language. Arming the display gate is only safe once no device in the
  // fleet still reports langSrc "default".
  req["lang"] = LanguageSettings::activeLanguage();
  req["dialect"] = LanguageSettings::activeDialect();
  req["langSrc"] = LanguageSettings::sourceName();

  // Active log threshold. Matters for reading the fleet's log feed as much as
  // for support: the firmware filters in log() before anything is stored, so
  // a device left on the default ERROR level cannot produce a WARN at all.
  // Without this field, "no warnings from that clock" and "that clock is
  // incapable of warning" are indistinguishable in the data.
  req["logLevel"] = logLevelName();

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
  // TODO: server expects this field; with the setup wizard removed, hardcoded true.
  // Drop the field once the heartbeat server tolerates its absence.
  req["setupComplete"] = true;
  
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

  // The body was read and thrown away on this line until sql/028; now it may
  // carry the rhythm the portal wants for the next beat. Cannot fail the
  // beat: the beat is done.
  applyNextBeatSeconds(body);

  logInfo("💓 Heartbeat sent successfully");
  return true;
}
