#include "config.h"

#include <WiFi.h>
#include <esp_wifi.h>
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

// True once this boot joined the factory network from the compiled-in
// credentials rather than from anything stored. Two places need to know:
// isInitialSetupMode() (which would otherwise stop the render loop the moment
// the factory AP blinks) and the reconnect path (which has no stored
// credentials to fall back on).
bool g_usingFactoryWifi = false;

// Set by storeWifiCredentials(). Only getStoredWifiSsid() reads it, to tell a
// driver config that still holds the factory network from one an operator has
// just overwritten with this clock's own.
bool g_credentialsStoredThisBoot = false;

// Shorter than bootstrap's 20 s budget on purpose. In the workshop the AP is
// there and a healthy join takes a few seconds; anywhere else this is pure
// dead time in front of the config portal, so it should be cheap to lose.
static const unsigned long FACTORY_WIFI_TIMEOUT_MS = 10000;

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

// Until now only nextgen-bootstrap referenced these, so a secrets.h without
// them still built every product. Keep that true: an empty SSID is the
// documented "no factory network" case and connectFactoryWifi() skips on it.
#ifndef BOOTSTRAP_WIFI_SSID
#define BOOTSTRAP_WIFI_SSID ""
#define BOOTSTRAP_WIFI_PASSWORD ""
#endif

// Fast path for a chip that has just been provisioned by nextgen-bootstrap.
//
// Bootstrap joins the workshop network from BOOTSTRAP_WIFI_SSID/PASSWORD, then
// erases nvs.net80211 on its way out (safeRestart() under WORDCLOCK_BOOTSTRAP)
// so the product firmware always enrolls fresh. Correct for a customer device,
// but it also meant every unit coming off the OTA landed in the config portal
// and an operator had to hand-enter the same workshop network to do five
// minutes of hardware testing. Trying those same credentials first turns that
// into a page refresh: mDNS re-registers `wordclock.local`, so the browser tab
// the operator already has open simply comes back.
//
// Called only when no credentials are stored, so this never delays a
// customer's clock and dies for good on a unit the moment one is enrolled.
//
// Deliberately RAM-only. Persisting would ship a clock that knows the factory
// network and rejoins it if it ever hears it again; the isolation rule in
// bootstrap_main.cpp is about the credentials never coming to rest, not about
// never using them.
static bool connectFactoryWifi() {
  if (sizeof(BOOTSTRAP_WIFI_SSID) <= 1) {
    return false;  // no factory network compiled in
  }
  logInfo(String("🏭 Trying factory network: ") + BOOTSTRAP_WIFI_SSID);
  WiFi.persistent(false);
  WiFi.begin(BOOTSTRAP_WIFI_SSID, BOOTSTRAP_WIFI_PASSWORD);
  const unsigned long deadline = millis() + FACTORY_WIFI_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    // Persistence back on for the rest of the boot: whatever the operator
    // enters in the portal later must still be saved normally.
    WiFi.persistent(true);
    g_usingFactoryWifi = true;
    return true;
  }
  // Drop the half-open attempt before falling through. Without this the
  // driver's in-memory config stays pointed at the factory SSID and the
  // argless WiFi.begin() that WiFiManager uses to mean "this clock's own
  // network" would keep retrying it. bootstrap_main.cpp:connectWifiHardcoded()
  // hands off to the portal in the same boot this way; eraseAP has nothing to
  // erase here because we only run with no stored credentials.
  WiFi.disconnect(true /* wifioff */, true /* eraseAP */);
  WiFi.persistent(true);
  // disconnect(wifioff=true) powers the radio down; put it back the way
  // initNetwork() left it so the paths below start from the same state they
  // would have without this attempt.
  WiFi.mode(WIFI_STA);
  logInfo("🏭 Factory network not found; continuing with normal provisioning.");
  return false;
}

// Enrolment from the admin page, as the counterpart to connectFactoryWifi():
// the operator has the clock in front of them on the workshop network and
// types the network it should actually live on.
//
// Deliberately does not call WiFi.begin(ssid, pass). begin() associates
// immediately, which tears down the connection carrying the request — the
// operator would never learn whether the save succeeded, and on a bad password
// the clock would be stranded on neither network with nothing stored. Writing
// the driver config with FLASH storage puts the credentials in nvs.net80211
// now and leaves this session's association alone, so the promise the admin
// page makes ("active after a restart") is the literal behaviour.
//
// The existing config is read back first so only the two fields we mean to
// change are touched: zero-filling a wifi_config_t would also reset the scan
// and sort methods, and a cleared pmf_cfg.capable is enough on its own to fail
// the association against a WPA3-capable AP.
bool storeWifiCredentials(const String& ssid, const String& password) {
  if (ssid.isEmpty() || ssid.length() > 32) return false;
  if (!password.isEmpty() && (password.length() < 8 || password.length() > 63)) return false;

  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    memset(&conf, 0, sizeof(conf));
    conf.sta.pmf_cfg.capable = true;
  }
  memset(conf.sta.ssid, 0, sizeof(conf.sta.ssid));
  memset(conf.sta.password, 0, sizeof(conf.sta.password));
  memcpy(conf.sta.ssid, ssid.c_str(), ssid.length());
  memcpy(conf.sta.password, password.c_str(), password.length());
  // Both directions matter: an open network must not inherit a WPA2 floor from
  // whatever was configured before, and a protected one must not be satisfied
  // by an unencrypted AP answering to the same name.
  conf.sta.threshold.authmode = password.isEmpty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
  // The operator typed a name, not a BSSID. A bssid_set left over from an
  // earlier association would pin the clock to one access point.
  conf.sta.bssid_set = false;

  esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &conf);
  if (err != ESP_OK) {
    logError(String("❌ Storing WiFi credentials failed: ") + esp_err_to_name(err));
    return false;
  }
  g_credentialsStoredThisBoot = true;
  // Never the password, not even at debug level: /log is served unauthenticated
  // and /log/download survives on the filesystem.
  logInfo(String("💾 WiFi credentials stored for '") + ssid + "' — active after restart.");
  return true;
}

String getStoredWifiSsid() {
  // On the factory fast path the driver config holds the workshop network,
  // which is not stored (connectFactoryWifi runs RAM-only) and is not this
  // clock's own network in any sense the operator means. Reporting it would
  // both lie about the boot behaviour and put the factory SSID on screen.
  // Once real credentials are saved the driver config is the right answer.
  if (g_usingFactoryWifi && !g_credentialsStoredThisBoot) return String();
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return String();
  char ssid[sizeof(conf.sta.ssid) + 1];
  memcpy(ssid, conf.sta.ssid, sizeof(conf.sta.ssid));
  ssid[sizeof(conf.sta.ssid)] = '\0';
  return String(ssid);
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

  // Ahead of every provisioning path below — BLE and the portal both assume
  // there is no way onto a network, and on a just-bootstrapped chip there is.
  if (!g_wifiHadCredentialsAtBoot && connectFactoryWifi()) {
    g_wifiConnected = true;
    logInfo("✅ WiFi connected to factory network: " + String(WiFi.SSID()));
    logInfo("📡 IP address: " + WiFi.localIP().toString());
    return;
  }

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
    // The moment we come up on anything other than the factory network the
    // operator has enrolled this unit for real. Drop the flag so the reconnect
    // path goes back to the stored credentials — leaving it set would have the
    // clock quietly reaching for the workshop AP for the rest of its life.
    if (g_usingFactoryWifi && WiFi.SSID() != String(BOOTSTRAP_WIFI_SSID)) {
      g_usingFactoryWifi = false;
      logInfo("🏭 Left the factory network; using enrolled credentials.");
    }
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
      if (g_usingFactoryWifi) {
        // Nothing is stored, so the argless begin() below has nothing to
        // reuse. Name the network explicitly — still without persisting it.
        WiFi.persistent(false);
        WiFi.begin(BOOTSTRAP_WIFI_SSID, BOOTSTRAP_WIFI_PASSWORD);
        WiFi.persistent(true);
      } else
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
  //
  // A clock on the factory network is none of that, even though it has no
  // stored credentials: it has a time, a face worth rendering and something
  // to reconnect to. Without this it would blank and stop retrying the moment
  // the workshop AP blinked — the one place where someone is watching.
  if (g_usingFactoryWifi) return false;
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
