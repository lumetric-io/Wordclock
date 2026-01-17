#include "webserver_init.h"
#include "mqtt_init.h"
#include "display_init.h"
#include "startup_sequence_init.h"
#include "wordclock_main.h"
#include "time_sync.h"
#include "wordclock_system_init.h"

// Wordclock hoofdprogramma
// - Setup: initialiseert hardware, netwerk, OTA, filesystem en start services
// - Loop: verwerkt webrequests, OTA, MQTT en kloklogica

#include <Arduino.h>
#include <ESPmDNS.h>
#include "fs_compat.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>
#include <WiFiServer.h>
#include <time.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include "wordclock.h"
#include "web_routes.h"
#include "network_init.h"
#include "log.h"
#include "config.h"
#include "ota_init.h"
#include "ota_updater.h"
#include "sequence_controller.h"
#include "display_settings.h"
#include "ui_auth.h"
#include "mqtt_client.h"
#include "night_mode.h"
#include "setup_state.h"
#include "led_state.h"
#include "settings_migration.h"
#include "system_utils.h"


bool clockEnabled = true;
StartupSequence startupSequence;
DisplaySettings displaySettings;
UiAuth uiAuth;
bool g_wifiHadCredentialsAtBoot = false;
static bool g_mqttInitialized = false;
static bool g_autoUpdateHandled = false;
static bool g_uiSyncHandled = false;
static bool g_serverInitialized = false;


// Webserver
WebServer server(80);

// Tracking (handled inside loop as statics)

// Flush all settings to persistent storage
void flushAllSettings() {
  logDebug("Flushing all settings to persistent storage...");
  ledState.flush();
  displaySettings.flush();
  nightMode.flush();
  setupState.flush();
  logDebug("Settings flush complete");
}

// Call before any ESP.restart()
void safeRestart() {
  flushAllSettings();
  delay(100);  // Allow flash write to complete
  ESP.restart();
}

// Setup: initialiseert hardware, netwerk, OTA, filesystem en start de hoofdservices
void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  delay(MDNS_START_DELAY_MS);
  initLogSettings();

  // IMPORTANT: Migrate settings before initializing them
  SettingsMigration::migrateIfNeeded();

  initNetwork();              // WiFiManager (WiFi-instellingen en verbinding)
  initOTA();                  // OTA (Over-the-air updates)
  
  // Register flush handler for OTA start
  ArduinoOTA.onStart([]() {
    flushAllSettings();
  });

  // Start mDNS voor lokale netwerknaam
  if (MDNS.begin(MDNS_HOSTNAME)) {
    logInfo("🌐 mDNS active at http://" MDNS_HOSTNAME ".local");
  } else {
    logError("❌ mDNS start failed");
  }

  // Load persisted display settings (e.g. auto-update preference) before running dependent flows
  displaySettings.begin();
  const bool hasLegacyConfig = g_wifiHadCredentialsAtBoot || displaySettings.hasPersistedGridVariant();
  setupState.begin(hasLegacyConfig);
  nightMode.begin();

  // Mount filesystem (LittleFS)
  if (!FS_IMPL.begin(true)) {
    logError("LittleFS mount failed.");
  } else {
    logDebug("LittleFS loaded successfully.");
    logEnableFileSink();
  }

  bool wifiConnected = isWiFiConnected();
  if (wifiConnected) {
    initWebServer(server);
    g_serverInitialized = true;
    initMqtt();
    g_mqttInitialized = true;
#if SUPPORT_OTA_V2 == 0
  syncUiFilesFromConfiguredVersion();
#endif
    g_uiSyncHandled = true;
    bool autoAllowed = displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
    if (autoAllowed) {
      logInfo("✅ Connected to WiFi. Starting firmware check...");
      checkForFirmwareUpdate();
    } else {
      if (displaySettings.getUpdateChannel() == "develop") {
        logInfo("ℹ️ Automatic updates disabled on develop channel. Skipping check.");
      } else {
        logInfo("ℹ️ Automatic firmware updates disabled. Skipping check.");
      }
    }
    g_autoUpdateHandled = true;
  } else {
    logInfo("⚠️ No WiFi. Waiting for connection or config portal.");
    bool autoAllowed = displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
    g_autoUpdateHandled = !autoAllowed;
    g_serverInitialized = false;
  }

  // Synchroniseer tijd via NTP
  initTimeSync(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
  initDisplay();
  initWordclockSystem(uiAuth);
  initStartupSequence(startupSequence);
}

// Loop: hoofdprogramma, verwerkt webrequests, OTA, MQTT en kloklogica
void loop() {
  processNetwork();
  if (isWiFiConnected() && !g_serverInitialized) {
    initWebServer(server);
    g_serverInitialized = true;
  }
  if (isWiFiConnected() && !g_mqttInitialized) {
    initMqtt();
    g_mqttInitialized = true;
  }
  if (isWiFiConnected() && !g_uiSyncHandled) {
#if SUPPORT_OTA_V2 == 0
  syncUiFilesFromConfiguredVersion();
#endif
    g_uiSyncHandled = true;
  }
  if (isWiFiConnected() && !g_autoUpdateHandled) {
    bool autoAllowed = displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
    if (autoAllowed) {
      logInfo("✅ Connected to WiFi. Starting firmware check...");
      checkForFirmwareUpdate();
    } else {
      if (displaySettings.getUpdateChannel() == "develop") {
        logInfo("ℹ️ Automatic updates disabled on develop channel. Skipping check.");
      } else {
        logInfo("ℹ️ Automatic firmware updates disabled. Skipping check.");
      }
    }
    g_autoUpdateHandled = true;
  }
  if (g_serverInitialized) {
    server.handleClient();
  }
  ArduinoOTA.handle();
  mqttEventLoop();

  // Periodic settings flush (every ~1 second)
  static unsigned long lastSettingsFlush = 0;
  if (millis() - lastSettingsFlush >= 1000) {
    ledState.loop();
    displaySettings.loop();
    nightMode.loop();
    setupState.loop();
    lastSettingsFlush = millis();
  }

  // Startup animatie: blokkeert klok tot animatie klaar is
  if (updateStartupSequence(startupSequence)) {
    return;  // Voorkomt dat klok al tijd toont
  }

  // Tijd- en animatie-update (wordclock_loop regelt zelf per-minuut/animatie)
  static unsigned long lastLoop = 0;
  unsigned long now = millis();
  if (now - lastLoop >= 50) {
    lastLoop = now;
    runWordclockLoop();

    // Dagelijkse firmwarecheck om 02:00
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      time_t nowEpoch = time(nullptr);
      static time_t lastFirmwareCheck = 0;
      if (timeinfo.tm_hour == 2 && timeinfo.tm_min == 0 && nowEpoch - lastFirmwareCheck > 3600) {
        bool autoAllowed = displaySettings.getAutoUpdate() && displaySettings.getUpdateChannel() != "develop";
        if (autoAllowed) {
          logInfo("🛠️ Daily firmware check started...");
          checkForFirmwareUpdate();
        } else {
          if (displaySettings.getUpdateChannel() == "develop") {
            logInfo("ℹ️ Automatic updates disabled on develop channel (02:00 check skipped)");
          } else {
            logInfo("ℹ️ Automatic firmware updates disabled (02:00 check skipped)");
          }
        }
        lastFirmwareCheck = nowEpoch;
      }
    }
  }
}
