#include "runtime_services.h"

#include "config.h"

#include <time.h>

#include <WebServer.h>

#if OTA_ENABLED
#include <ArduinoOTA.h>
#endif

#include "ble_provisioning.h"
#include "device_identity.h"
#include "device_registration.h"
#include "display_settings.h"
#include "heartbeat.h"
#include "led_events.h"
#include "led_state.h"
#include "log.h"
#include "mqtt_client.h"
#include "mqtt_init.h"
#include "network_init.h"
#include "night_mode.h"
#include "ota_updater.h"
#include "setup_state.h"
#include "startup_sequence_init.h"
#include "time_sync.h"
#include "webserver_init.h"
#include "wordclock_main.h"

namespace {

bool g_mqttInitialized = false;
bool g_autoUpdateHandled = false;
bool g_uiSyncHandled = false;
bool g_serverInitialized = false;
bool g_autoRegistrationHandled = false;
bool g_heartbeatInitialized = false;

bool g_lastWifiConnected = false;
unsigned long g_lastSettingsFlushPortalMs = 0;
unsigned long g_lastSettingsFlushMs = 0;
unsigned long g_lastLoopMs = 0;
time_t g_lastFirmwareCheck = 0;

void attemptAutoRegistration() {
  if (g_autoRegistrationHandled || !isWiFiConnected()) return;
  
  // Skip if already registered (credentials exist)
  String existingId = get_device_id();
  String existingToken = get_device_token();
  if (!existingId.isEmpty() && !existingToken.isEmpty()) {
    logDebug("ℹ️ Device already has credentials, skipping registration");
    g_autoRegistrationHandled = true;
    return;
  }
  
  String deviceId;
  String token;
  String err;
  if (register_device_with_fleet(deviceId, token, err)) {
    logInfo("✅ Auto-registered device on startup.");
  } else {
    // "Device already registered" is expected, only log as debug
    if (err.indexOf("already registered") >= 0) {
      logDebug(String("ℹ️ ") + err);
    } else {
      logWarn(String("⚠️ Auto-registration failed: ") + err);
    }
  }
  g_autoRegistrationHandled = true;
}

} // namespace

void runtimeInitOnSetup(bool wifiConnected, WebServer& server) {
  if (wifiConnected) {
    initWebServer(server);
    g_serverInitialized = true;
    initMqtt();
    g_mqttInitialized = true;
#if SUPPORT_OTA_V2 == 0
    syncFilesFromManifest();
#endif
    g_uiSyncHandled = true;
#if OTA_ENABLED
    bool autoAllowed = displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
    if (autoAllowed) {
      logInfo("✅ Connected to WiFi. Starting firmware check...");
      checkForFirmwareUpdate();
    } else {
      logInfo("ℹ️ Automatic firmware updates disabled. Skipping check.");
    }
    g_autoUpdateHandled = true;
#else
    g_autoUpdateHandled = true;
#endif
    attemptAutoRegistration();
  } else {
    logInfo("⚠️ No WiFi. Waiting for connection or config portal.");
#if OTA_ENABLED
    bool autoAllowed = displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
    g_autoUpdateHandled = !autoAllowed;
#else
    g_autoUpdateHandled = true;
#endif
    g_serverInitialized = false;
  }
}

void runtimeHandleWifiTransitionLogs(bool wifiConnected) {
  if (wifiConnected != g_lastWifiConnected) {
    if (wifiConnected) {
      logInfo("✅ WiFi connected. Exiting provisioning mode.");
      // Trigger heartbeat on WiFi reconnect
      if (g_heartbeatInitialized) {
        triggerHeartbeat();
      }
    } else {
#if WIFI_MANAGER_ENABLED
      logWarn("📶 WiFi not connected. Entering portal mode (WiFiManager active).");
#else
      logWarn("📶 WiFi not connected. Entering provisioning mode (BLE only).");
#endif
    }
    g_lastWifiConnected = wifiConnected;
  }
}

bool runtimeHandleNoWifiLoop(unsigned long nowMs) {
  if (!isWiFiConnected()) {
    if (nowMs - g_lastSettingsFlushPortalMs >= 5000) {
      ledState.loop();
      displaySettings.loop();
      nightMode.loop();
      setupState.loop();
      g_lastSettingsFlushPortalMs = nowMs;
    }
    ledEventsTick(nowMs);
    return true;
  }
  return false;
}

void runtimeEnsureOnlineServices(WebServer& server) {
  if (!isWiFiConnected()) return;
  if (!g_serverInitialized) {
    initWebServer(server);
    g_serverInitialized = true;
  }
  if (!g_mqttInitialized) {
    initMqtt();
    g_mqttInitialized = true;
  }
  if (!g_uiSyncHandled) {
#if SUPPORT_OTA_V2 == 0
    syncFilesFromManifest();
#endif
    g_uiSyncHandled = true;
  }
  if (!g_autoUpdateHandled) {
#if OTA_ENABLED
    bool autoAllowed = displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
    if (autoAllowed) {
      logInfo("✅ Connected to WiFi. Starting firmware check...");
      checkForFirmwareUpdate();
    } else {
      logInfo("ℹ️ Automatic firmware updates disabled. Skipping check.");
    }
#endif
    g_autoUpdateHandled = true;
  }
  if (!g_autoRegistrationHandled) {
    attemptAutoRegistration();
  }
  if (!g_heartbeatInitialized) {
    initHeartbeat();
    g_heartbeatInitialized = true;
  }
}

void runtimeHandleOnlineServices(WebServer& server, unsigned long nowMs) {
  if (!isWiFiConnected()) return;
  if (g_serverInitialized) {
    server.handleClient();
  }
#if OTA_ENABLED
  ArduinoOTA.handle();
#endif
  mqttEventLoop();
  processHeartbeat(nowMs);
}

void runtimeHandlePeriodicSettings(unsigned long nowMs, unsigned long intervalMs) {
  if (nowMs - g_lastSettingsFlushMs >= intervalMs) {
    ledState.loop();
    displaySettings.loop();
    nightMode.loop();
    setupState.loop();
    g_lastSettingsFlushMs = nowMs;
  }
}

bool runtimeHandleLedEvents(unsigned long nowMs) {
  return ledEventsTick(nowMs);
}

bool runtimeHandleStartupSequence(StartupSequence& startupSequence) {
  return updateStartupSequence(startupSequence);
}

void runtimeHandleWordclockLoop(unsigned long nowMs) {
  if (nowMs - g_lastLoopMs >= 50) {
    g_lastLoopMs = nowMs;
    runWordclockLoop();

#if OTA_ENABLED
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      time_t nowEpoch = time(nullptr);
      if (timeinfo.tm_hour == 2 && timeinfo.tm_min == 0 && nowEpoch - g_lastFirmwareCheck > 3600) {
        bool autoAllowed = displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
        if (autoAllowed) {
          logInfo("🛠️ Daily firmware check started...");
          checkForFirmwareUpdate();
        } else {
          logInfo("ℹ️ Automatic firmware updates disabled (02:00 check skipped)");
        }
        g_lastFirmwareCheck = nowEpoch;
      }
    }
#endif
  }
}
