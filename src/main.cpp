#include "mqtt_init.h"
#include "display_init.h"
#include "startup_sequence_init.h"
#include "wordclock_main.h"
#include "time_sync.h"
#include "photo_sell_time.h"
#include "wordclock_system_init.h"
#include "runtime_services.h"
#include "device_commands.h"

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
#include "setup_state.h"
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

#if !PHOTO_SESSION_WIFI
  // Synchroniseer tijd via NTP
  initTimeSync(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
#endif
  initDisplay();
#if PHOTO_SESSION_WIFI
  // No NTP on the photo build — the face is driven by sell mode instead.
  //
  // Must run *after* initDisplay(), not in place of initTimeSync() above:
  // initDisplay() calls displaySettings.begin() a second time, which re-reads
  // sell_on from NVS (false) and silently undoes setSellModeVolatile().
  photoInitSellTime();
#endif
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

  // Above the no-Wi-Fi branch on purpose. A reboot accepted from the portal at
  // 22:00 must still fire at 04:00 even if the network dropped in between, so
  // this cannot live in runtimeHandleOnlineServices() - that returns early
  // when Wi-Fi is down.
  deviceCommandsTick(nowMs);

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
