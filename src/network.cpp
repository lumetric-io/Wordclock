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
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000; // 15s between manual reconnect attempts
#if WIFI_MANAGER_ENABLED
static bool g_wifiManagerStarted = false;
#endif

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
  auto& wm = getManager();
  if (wm.getConfigPortalActive()) {
    // Give the config portal web server more CPU time so 192.168.4.1 responds faster.
    for (int i = 0; i < 5; ++i) {
      wm.process();
      delay(0);
    }
  } else {
    wm.process();
  }
#endif

  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected && !g_wifiConnected) {
    logInfo("✅ WiFi connection established: " + String(WiFi.SSID()));
    logInfo("📡 IP address: " + WiFi.localIP().toString());
    lastReconnectAttemptMs = millis();
#if WIFI_MANAGER_ENABLED
    auto& wm = getManager();
    if (wm.getConfigPortalActive()) {
      wm.stopConfigPortal();
      logInfo("📶 WiFiManager portal stopped after STA connect");
    }
#endif
    ledEventStop(LedEvent::WifiManagerPortal);
    g_wifiManagerStarted = false;
  } else if (!connected && g_wifiConnected) {
    logWarn("⚠️ WiFi connection lost.");
    lastReconnectAttemptMs = 0; // allow immediate manual reconnect attempt
  }

  // When disconnected, kick off periodic reconnects to avoid needing a full device reboot
  if (!connected) {
    unsigned long now = millis();
    if (lastReconnectAttemptMs == 0 || now - lastReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
      logInfo("🔄 Attempting WiFi reconnect...");
      WiFi.reconnect();
      lastReconnectAttemptMs = now;
    }
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
