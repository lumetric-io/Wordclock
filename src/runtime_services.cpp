#include "runtime_services.h"

#include "config.h"

#include <time.h>

#include <ESPmDNS.h>
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
#include "photo_session.h"
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
bool g_mdnsRegistered = false;
unsigned long g_mdnsNextAttemptMs = 0;

bool g_lastWifiConnected = false;
unsigned long g_lastSettingsFlushPortalMs = 0;
unsigned long g_lastSettingsFlushMs = 0;
unsigned long g_lastLoopMs = 0;
time_t g_lastFirmwareCheck = 0;

// Registers `wordclock.local`. This lives with the other online services
// rather than in setup() on purpose: the responder binds to the STA interface,
// so a boot that comes up without Wi-Fi has nothing to bind to. Registering
// once at boot meant such a device served the dashboard on its IP while its
// name stayed dead for the rest of its uptime — every other service recovered
// on reconnect, the name did not, and the only fix a customer has is the plug.
//
// Only called with Wi-Fi up. Leaves g_mdnsRegistered false on failure so the
// loop retries, throttled by MDNS_RETRY_INTERVAL_MS.
void startMdns() {
  // ArduinoOTA has already brought the mDNS stack up and registered
  // _arduino._tcp by the time this first runs, so end() is right only when
  // re-registering — on the first pass it would take OTA discovery with it.
  static bool registeredBefore = false;
  if (registeredBefore) {
    MDNS.end();
  }
  if (!MDNS.begin(MDNS_HOSTNAME)) {
    logError("❌ mDNS start failed");
    g_mdnsNextAttemptMs = millis() + MDNS_RETRY_INTERVAL_MS;
    return;
  }
  // The per-device firmware advertised no service at all while
  // bootstrap_main.cpp did. Hostname lookups work either way; browsing
  // _http._tcp does not, and there was never a reason for the two to differ.
  MDNS.addService("http", "tcp", 80);
  registeredBefore = true;
  g_mdnsRegistered = true;
  g_mdnsNextAttemptMs = 0;
  logInfo("🌐 mDNS active at http://" MDNS_HOSTNAME ".local");
}

void ensureMdns() {
  if (g_mdnsRegistered) return;
  if (g_mdnsNextAttemptMs != 0 && millis() < g_mdnsNextAttemptMs) return;
  startMdns();
}

// Single predicate for "may this build update itself *unattended*". Only the
// automatic checks consult it — the boot check and the 02:00 daily one. The
// admin UI's "check for updates" calls checkForFirmwareUpdate() straight from
// web_routes.h and is deliberately untouched, so a photo clock can still be
// installed from and returned to any channel by hand.
//
// Off for the whole photo build, including fallback boots. These are already
// provisioned clocks, so their stored channel is usually "stable" — leaving the
// automatic path on would mean the 02:00 check quietly reinstalls stable and
// wipes the photo firmware off half the set the night before the shoot.
bool firmwareAutoUpdateAllowed() {
#if PHOTO_SESSION_WIFI
  return false;
#else
  return displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
#endif
}

void attemptAutoRegistration() {
#if PHOTO_SESSION_WIFI
  // Photo clocks stay out of the fleet entirely — no device row, no token, no
  // telemetry from twenty units that will be reflashed the same week.
  g_autoRegistrationHandled = true;
  return;
#else
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
#endif
}

} // namespace

void runtimeInitOnSetup(bool wifiConnected, WebServer& server) {
  if (wifiConnected) {
    ensureMdns();
    initWebServer(server);
    g_serverInitialized = true;
    initMqtt();
    g_mqttInitialized = true;
#if SUPPORT_OTA_V2 == 0
    syncFilesFromManifest();
#endif
    g_uiSyncHandled = true;
#if OTA_ENABLED
    bool autoAllowed = firmwareAutoUpdateAllowed();
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
    bool autoAllowed = firmwareAutoUpdateAllowed();
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
      // The responder is bound to the STA interface and the address can differ
      // when we come back, so re-register rather than assume it survived. This
      // edge is the only place the drop is visible: runtimeEnsureOnlineServices
      // returns early while offline and can't tell a first boot from a return.
      g_mdnsRegistered = false;
      g_mdnsNextAttemptMs = 0;
#if WIFI_MANAGER_ENABLED
      logWarn("📶 WiFi not connected. Retrying; portal opens after timeout.");
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
      g_lastSettingsFlushPortalMs = nowMs;
    }
    return true;
  }
  return false;
}

void runtimeEnsureOnlineServices(WebServer& server) {
  if (!isWiFiConnected()) return;
  ensureMdns();
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
    bool autoAllowed = firmwareAutoUpdateAllowed();
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
#if !PHOTO_SESSION_WIFI
  // Left uninitialised on the photo build, which also keeps the reconnect-edge
  // triggerHeartbeat() in runtimeHandleWifiTransitionLogs() silent.
  if (!g_heartbeatInitialized) {
    initHeartbeat();
    g_heartbeatInitialized = true;
  }
#endif
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
#if !PHOTO_SESSION_WIFI
  processHeartbeat(nowMs);
#else
  (void)nowMs;
#endif
}

void runtimeHandlePeriodicSettings(unsigned long nowMs, unsigned long intervalMs) {
  if (nowMs - g_lastSettingsFlushMs >= intervalMs) {
    ledState.loop();
    displaySettings.loop();
    nightMode.loop();
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
        bool autoAllowed = firmwareAutoUpdateAllowed();
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
