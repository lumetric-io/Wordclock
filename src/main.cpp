#include "mqtt_init.h"
#include "display_init.h"
#include "startup_sequence_init.h"
#include "wordclock_main.h"
#include "time_sync.h"
#include "wordclock_system_init.h"
#include "runtime_services.h"

// Wordclock hoofdprogramma
// - Setup: initialiseert hardware, netwerk, OTA, filesystem en start services
// - Loop: verwerkt webrequests, OTA, MQTT en kloklogica

#include <Arduino.h>
#include <ESPmDNS.h>
#include "fs_compat.h"
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <time.h>
#if OTA_ENABLED
#include <ArduinoOTA.h>
#endif
#include <WebServer.h>
#include "wordclock.h"
#include "network_init.h"
#include "log.h"
#include "config.h"
#include "ota_init.h"
#include "display_settings.h"
#include "ui_auth.h"
#include "night_mode.h"
#include "setup_state.h"
#include "settings_migration.h"
#include "system_utils.h"
#include "ble_provisioning.h"


bool clockEnabled = true;
StartupSequence startupSequence;
DisplaySettings displaySettings;
UiAuth uiAuth;
bool g_wifiHadCredentialsAtBoot = false;


// Webserver
WebServer server(80);

// Setup: initialiseert hardware, netwerk, OTA, filesystem en start de hoofdservices
void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  delay(MDNS_START_DELAY_MS);
  initLogSettings();

  // IMPORTANT: Migrate settings before initializing them
  SettingsMigration::migrateIfNeeded();

  initBleProvisioning();
  initNetwork();              // WiFiManager (WiFi-instellingen en verbinding)
#if OTA_ENABLED
  initOTA();                  // OTA (Over-the-air updates)
  
  // Register flush handler for OTA start
  ArduinoOTA.onStart([]() {
    flushAllSettings();
  });
#endif

  // Start mDNS voor lokale netwerknaam
  if (MDNS.begin(MDNS_HOSTNAME)) {
    logInfo("🌐 mDNS active at http://" MDNS_HOSTNAME ".local");
  } else {
    logError("❌ mDNS start failed");
  }

  // Load persisted display settings (e.g. auto-update preference) before running dependent flows
  displaySettings.begin();
  const bool hasLegacyConfig = SETUP_ASSUME_DONE_IF_LEGACY_CONFIG &&
                               displaySettings.hasPersistedGridVariant();
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
  runtimeInitOnSetup(wifiConnected, server);

  // Synchroniseer tijd via NTP
  initTimeSync(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
  initDisplay();
  initWordclockSystem(uiAuth);
  initStartupSequence(startupSequence);
}

// Loop: hoofdprogramma, verwerkt webrequests, OTA, MQTT en kloklogica
void loop() {
  processNetwork();
  processBleProvisioning();
  const bool wifiConnected = isWiFiConnected();
  runtimeHandleWifiTransitionLogs(wifiConnected);

  unsigned long nowMs = millis();
  if (runtimeHandleNoWifiLoop(nowMs)) {
    return;
  }

  runtimeEnsureOnlineServices(server);
  runtimeHandleOnlineServices(server, nowMs);
  runtimeHandlePeriodicSettings(nowMs, 1000);

  if (runtimeHandleLedEvents(nowMs)) {
    return;
  }

  if (isBleProvisioningActive()) {
    return;
  }

  if (runtimeHandleStartupSequence(startupSequence)) {
    return;
  }

  runtimeHandleWordclockLoop(nowMs);
}
