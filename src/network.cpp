#include "config.h"

#include <WiFi.h>
#if WIFI_MANAGER_ENABLED
#ifndef ESP32
#define ESP32
#endif
#include <WiFiManager.h>
#endif

#include "network_init.h"
#include "ble_provisioning.h"
#include "led_events.h"
#include "log.h"
#include "led_controller.h"
#include "secrets.h"
#include "system_utils.h"

extern bool clockEnabled;
extern bool g_wifiHadCredentialsAtBoot;

namespace {

#if WIFI_MANAGER_ENABLED
WiFiManager& getManager() {
  static WiFiManager manager;
  return manager;
}
#endif

bool g_wifiConnected = false;
static unsigned long lastReconnectAttemptMs = 0;
static unsigned long reconnectWindowStartMs = 0;
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 60000; // 60s between manual reconnect attempts
static const unsigned long WIFI_RECONNECT_WINDOW_MS   = 10000; // 10s active scan window per attempt
static unsigned long disconnectedSinceMs = 0; // millis() when WiFi first became unavailable
#if WIFI_MANAGER_ENABLED
static bool g_wifiManagerStarted = false;
#endif

void stopWiFiForBleProvisioning() {
  // BLE provisioning can fail on ESP32-S3 if WiFi is running during BT init.
  // Stop WiFi before starting BLE to avoid coex/controller enable errors.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
}

} // namespace

static bool connectWithStoredCredentials() {
  WiFi.begin();
  for (int attempt = 0; attempt < WIFI_CONNECT_MAX_RETRIES; ++attempt) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(WIFI_CONNECT_RETRY_DELAY_MS);
  }
  return WiFi.status() == WL_CONNECTED;
}

static void startWiFiManagerPortal() {
#if WIFI_MANAGER_ENABLED
  if (g_wifiManagerStarted) return;
  ledEventStart(LedEvent::WifiManagerPortal);
  auto& wm = getManager();
  wm.setConfigPortalBlocking(false);
  wm.startConfigPortal(AP_NAME, AP_PASSWORD);
  g_wifiManagerStarted = true;
  logWarn(String("📶 WiFi config portal active. Connect to '") + AP_NAME + "' to configure WiFi.");
#endif
}

void initNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
#if WIFI_MANAGER_ENABLED
  auto& wm = getManager();

  wm.setConfigPortalBlocking(false);
  wm.setAPClientCheck(false);  // allow AP even when STA disconnected
  wm.setCaptivePortalEnable(true);
  wm.setWebPortalClientCheck(false); // keep portal alive; Android captive checks can be chatty
  wm.setCleanConnect(true);    // ensure fresh STA connect attempts
  wm.setSTAStaticIPConfig(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  #if defined(WM_DEBUG_DEV)
  wm.setDebugOutput(true, WM_DEBUG_DEV);
  #else
  wm.setDebugOutput(true);
  #endif

  g_wifiHadCredentialsAtBoot = wm.getWiFiIsSaved();
  logInfo(String("WiFiManager starting connection (credentials present: ") + (g_wifiHadCredentialsAtBoot ? "yes" : "no") + ")");
#else
  g_wifiHadCredentialsAtBoot = WiFi.SSID().length() > 0;
  logInfo(String("WiFiManager disabled (credentials present: ") + (g_wifiHadCredentialsAtBoot ? "yes" : "no") + ")");
#endif

#if BLE_PROVISIONING_ENABLED
#if WIFI_MANAGER_ENABLED
  if (!g_wifiHadCredentialsAtBoot) {
    stopWiFiForBleProvisioning();
    startBleProvisioning(BleProvisioningReason::FirstBootNoCreds);
    g_wifiConnected = false;
    return;
  }
  if (connectWithStoredCredentials()) {
    g_wifiConnected = true;
    logInfo("✅ WiFi connected to stored network: " + String(WiFi.SSID()));
    logInfo("📡 IP address: " + WiFi.localIP().toString());
    return;
  }

  stopWiFiForBleProvisioning();
  startBleProvisioning(BleProvisioningReason::WiFiUnavailableAtBoot);
  g_wifiConnected = false;
  return;
#else
  if (connectWithStoredCredentials()) {
    g_wifiConnected = true;
    logInfo("✅ WiFi connected to stored network: " + String(WiFi.SSID()));
    logInfo("📡 IP address: " + WiFi.localIP().toString());
    return;
  }
  stopWiFiForBleProvisioning();
  startBleProvisioning(BleProvisioningReason::WiFiUnavailableAtBoot);
  g_wifiConnected = false;
  return;
#endif
#endif

#if WIFI_MANAGER_ENABLED
  bool autoResult = wm.autoConnect(AP_NAME, AP_PASSWORD);
  (void)autoResult;
  g_wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (g_wifiConnected) {
    logInfo("✅ WiFi connected to network: " + String(WiFi.SSID()));
    logInfo("📡 IP address: " + WiFi.localIP().toString());
  } else {
    if (!wm.getConfigPortalActive()) {
      startWiFiManagerPortal();
    } else {
      ledEventStart(LedEvent::WifiManagerPortal);
      g_wifiManagerStarted = true;
      logWarn(String("📶 WiFi config portal active. Connect to '") + AP_NAME + "' to configure WiFi.");
    }
  }
#else
  g_wifiConnected = connectWithStoredCredentials();
  if (g_wifiConnected) {
    logInfo("✅ WiFi connected to network: " + String(WiFi.SSID()));
    logInfo("📡 IP address: " + WiFi.localIP().toString());
  } else {
    logWarn("⚠️ WiFi not connected. WiFiManager portal disabled.");
  }
#endif
}

void processNetwork() {
#if WIFI_MANAGER_ENABLED
  // Only service the portal web server — do NOT call process() when idle, as
  // WiFiManager's internal state machine would otherwise auto-start the portal
  // on disconnect without waiting for our fallback timer.
  auto& wm = getManager();
  if (wm.getConfigPortalActive()) {
    // Give the config portal web server more CPU time so 192.168.4.1 responds faster.
    for (int i = 0; i < 5; ++i) {
      wm.process();
      delay(0);
    }
  }
#endif

  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected && !g_wifiConnected) {
    logInfo("✅ WiFi connection established: " + String(WiFi.SSID()));
    logInfo("📡 IP address: " + WiFi.localIP().toString());
    lastReconnectAttemptMs = millis();
    reconnectWindowStartMs = 0;
    disconnectedSinceMs = 0;
#if WIFI_MANAGER_ENABLED
    auto& wm = getManager();
    if (wm.getConfigPortalActive()) {
      wm.stopConfigPortal();
      logInfo("📶 WiFiManager portal stopped after STA connect");
    }
#endif
    ledEventStop(LedEvent::WifiManagerPortal);
    ledEventStop(LedEvent::WifiDisconnected);
    g_wifiManagerStarted = false;
  } else if (!connected && g_wifiConnected) {
    logWarn("⚠️ WiFi connection lost.");
    ledEventStart(LedEvent::WifiDisconnected);
    lastReconnectAttemptMs = 0; // allow immediate manual reconnect attempt
    disconnectedSinceMs = millis();
  }

  // When disconnected, kick off periodic reconnects to avoid needing a full device reboot.
  // The portal (if started) runs in AP+STA mode so reconnect attempts continue alongside it.
  //
  // Exception: during initial setup (no saved creds at boot) the periodic
  // STA scan can never succeed — there's nothing to reconnect to — and the
  // scan disrupts the AP that the portal is serving. Skip the whole block;
  // once the operator submits creds via the portal, WiFiManager handles
  // the connection itself.
  if (!connected && !isInitialSetupMode()) {
    unsigned long now = millis();

    // Track how long we have been without a connection (handles boot-time failures too).
    if (disconnectedSinceMs == 0) disconnectedSinceMs = now;

    // Periodically attempt to reconnect using stored credentials.
    //
    // Two cases depending on whether the WiFiManager config portal is active:
    //
    // Portal NOT active: WiFi is in STA mode. WiFi.begin() (no args) is sufficient — it
    //   calls esp_wifi_start() which, combined with setAutoReconnect(true), lets the ESP32's
    //   internal reconnect machinery do the work. Avoid WiFi.reconnect() here: it calls
    //   esp_wifi_disconnect() which resets the 15s internal retry timer, causing a
    //   reconnect-storm when auto-reconnect is also running.
    //
    // Portal IS active (AP+STA mode): WiFiManager suppresses setAutoReconnect, and
    //   WiFi.begin() (no args) is a no-op — WiFi is already started (esp_wifi_start() does
    //   nothing when the driver is running). WiFi.reconnect() must be used instead: it
    //   explicitly calls esp_wifi_disconnect() + esp_wifi_connect() on the STA interface
    //   without affecting the AP. No reconnect-storm here because WiFiManager has suppressed
    //   auto-reconnect.
    //
    // After each attempt, stop the active scan after WIFI_RECONNECT_WINDOW_MS to prevent
    // the WiFi stack from scanning indefinitely, which causes LED flickering via DMA contention.
    if (reconnectWindowStartMs != 0 && now - reconnectWindowStartMs >= WIFI_RECONNECT_WINDOW_MS) {
      WiFi.disconnect(false); // stop active scan; credentials are preserved
      reconnectWindowStartMs = 0;
    }
    if (lastReconnectAttemptMs == 0 || now - lastReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
      logInfo("🔄 Attempting WiFi reconnect...");
#if WIFI_MANAGER_ENABLED
      if (g_wifiManagerStarted) {
        // Portal active in AP+STA mode: WiFi.begin() is a no-op; use reconnect() instead.
        WiFi.reconnect();
      } else {
        WiFi.begin(); // begin() reuses stored credentials without disconnecting first
      }
#else
      WiFi.begin();
#endif
      lastReconnectAttemptMs = now;
      reconnectWindowStartMs = now;
    }

#if WIFI_MANAGER_ENABLED
    // After the fallback period open the config portal so the user can intervene,
    // while reconnect attempts continue in the background.
    if (!g_wifiManagerStarted && now - disconnectedSinceMs >= WIFI_PORTAL_FALLBACK_MS) {
      logWarn("⏱️ No WiFi for " + String(WIFI_CONFIG_PORTAL_TIMEOUT) + "s — opening config portal.");
      startWiFiManagerPortal();
    }
#endif
  }
  g_wifiConnected = connected;

#if BLE_PROVISIONING_ENABLED
#if WIFI_MANAGER_ENABLED
  if (takeBleProvisioningTimeout()) {
    startWiFiManagerPortal();
  }
#endif
#endif
}

bool isWiFiConnected() {
  return g_wifiConnected;
}

bool isInitialSetupMode() {
  // Initial setup = no saved credentials AND not currently connected. The
  // portal is up (initNetwork starts it on autoConnect failure) and the
  // operator is in front of the device picking an SSID. There is nothing
  // useful to render on the clock face (no NTP yet) and no point in
  // periodic STA reconnect scans (no credentials to retry).
  return !g_wifiConnected && !g_wifiHadCredentialsAtBoot;
}

void resetWiFiSettings() {
#if WIFI_MANAGER_ENABLED
  logInfo("🔁 WiFiManager settings are being cleared...");
  auto& wm = getManager();
  wm.resetSettings();
#else
  logInfo("🔁 WiFi settings are being cleared...");
  WiFi.disconnect(true, true);
#endif
  clockEnabled = false;
  showLeds({});
  delay(EEPROM_WRITE_DELAY_MS);
  safeRestart();
}
