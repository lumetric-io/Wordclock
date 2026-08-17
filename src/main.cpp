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
#include "language_settings.h"
#include "settings_migration.h"
#include "system_utils.h"
#include "ble_provisioning.h"
#include "led_controller.h"


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
  
  // Clear LEDs immediately to prevent garbage flash during boot
  earlyLedClear();
  
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

  // mDNS is registered by runtimeInitOnSetup() below, together with the web
  // server and the other services that need a live STA interface — and
  // re-registered from the loop after a reconnect. It used to be started here,
  // which meant a boot without WiFi left the clock nameless until someone
  // power-cycled it. See runtime_services.cpp:startMdns().

  // Load persisted display settings (e.g. auto-update preference) before running dependent flows
  displaySettings.begin();

  // Selects the grid variant and phrase table. Must run before anything reads
  // ACTIVE_WORDS or the LED counts, i.e. before initDisplay() below.
  LanguageSettings::begin();

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
    // Skip the wordclock render loop during initial setup so the AP and
    // portal stay responsive. Adafruit_NeoPixel::show() disables interrupts
    // for ~30µs/LED — at 20fps for a 121-LED grid that's ~72ms/sec of
    // interrupt-disabled time, which chokes WiFi/lwIP. No useful clock
    // face to render anyway (no NTP yet). The LedEvents tick still runs so
    // the WifiManagerPortal heartbeat indicator stays visible.
    if (!isBleProvisioningActive() && !isInitialSetupMode()) {
      runtimeHandleWordclockLoop(nowMs);
    }
    runtimeHandleLedEvents(nowMs);
    return;
  }

  runtimeEnsureOnlineServices(server);
  runtimeHandleOnlineServices(server, nowMs);
  runtimeHandlePeriodicSettings(nowMs, 1000);

  // Always run LED events (e.g. BLE provisioning blink) so they show even when BLE is active
  runtimeHandleLedEvents(nowMs);

  if (isBleProvisioningActive()) {
    return;
  }

  if (runtimeHandleStartupSequence(startupSequence)) {
    return;
  }

  runtimeHandleWordclockLoop(nowMs);
}
