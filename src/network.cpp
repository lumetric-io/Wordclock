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
#include "photo_session.h"
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

#if PHOTO_SESSION_WIFI
// True only while this boot is actually using the compiled-in studio network.
// Everything photo-specific keys off this rather than off PHOTO_SESSION_WIFI,
// because a photo-firmware clock away from the studio has to behave like an
// ordinary clock — see photoShouldTryHardcoded().
static bool g_photoHardcodedActive = false;
#endif

void stopWiFiForBleProvisioning() {
  // BLE provisioning can fail on ESP32-S3 if WiFi is running during BT init.
  // Stop WiFi before starting BLE to avoid coex/controller enable errors.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
}

#if defined(PRODUCT_VARIANT_MINI)
void applyMiniWifiTweaks() {
  // Apply only when WiFi is in use; not before BLE provisioning (avoids radio conflict).
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setSleep(false);
}
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

// Enrolment from the admin page: the operator has the clock in front of them
// and types the network it should actually live on, without ever opening the
// config portal.
//
// Deliberately does not call WiFi.begin(ssid, pass). begin() associates
// immediately, which tears down the connection carrying the request, so the
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
  // Never the password, not even at debug level: /log is served unauthenticated
  // and /log/download survives on the filesystem.
  logInfo(String("💾 WiFi credentials stored for '") + ssid + "', active after restart.");
  return true;
}

String getStoredWifiSsid() {
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return String();
  char ssid[sizeof(conf.sta.ssid) + 1];
  memcpy(ssid, conf.sta.ssid, sizeof(conf.sta.ssid));
  ssid[sizeof(conf.sta.ssid)] = '\0';
  return String(ssid);
}

static void startWiFiManagerPortal() {
#if PHOTO_SESSION_WIFI
  if (g_photoHardcodedActive) {
    // No portal on the shoot floor: it would raise an AP, light the portal LED
    // event, and put a "configure me" animation in somebody's photograph. The
    // studio SSID is compiled in, so there is nothing to configure anyway.
    // Only suppressed while the studio network is actually in use — on a
    // fallback boot the portal is the whole recovery path and must work.
    return;
  }
#endif
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

#if PHOTO_SESSION_WIFI

// Survives esp_restart(), garbage after a power cycle — which is exactly the
// lifetime this needs. See photoShouldTryHardcoded().
RTC_NOINIT_ATTR static uint32_t g_photoFallbackMark;
static const uint32_t PHOTO_FALLBACK_MARK = 0x50484F31UL;  // 'PHO1'

// Should this boot try the studio network at all?
//
// The problem being solved: once WiFi.begin(ssid, pass) has run with storage
// forced to RAM, the driver's in-memory config *is* the studio network, and an
// argless WiFi.begin() — which is what WiFiManager and the reconnect loop use
// to mean "the network this clock is provisioned for" — would keep retrying the
// studio SSID instead. There is no clean way to put the stored credentials back
// without reinitialising the Wi-Fi driver mid-boot.
//
// So don't try. If the studio network isn't there, set a mark and reboot: the
// next boot never touches persistent(false), never begins with hardcoded
// credentials, and runs the ordinary main-branch path with pristine driver
// state — stored credentials, portal, OTA, all of it.
//
// The mark lives in RTC RAM rather than NVS deliberately. A soft reset keeps it
// (so the fallback boot doesn't loop), a power cycle loses it (so unplugging a
// clock at the studio makes it try the studio network again). Neither behaviour
// needs anyone to remember to clear a flag.
static bool photoShouldTryHardcoded() {
  if (g_photoFallbackMark == PHOTO_FALLBACK_MARK) {
    g_photoFallbackMark = 0;  // consume it; a later power cycle starts over
    return false;
  }
  return true;
}

bool connectPhotoWifi() {
  // Caught at compile time rather than as a device that boots and quietly
  // never joins anything — the failure would otherwise surface as twenty
  // clocks sitting dark on a table with the photographer already there.
  static_assert(sizeof(PHOTO_WIFI_SSID) > 1,
                "PHOTO_WIFI_SSID is empty — set the studio Wi-Fi in include/secrets.h.");

  if (WiFi.status() == WL_CONNECTED) return true;

  // persistent(false) before the first begin(): ESP-IDF otherwise writes the
  // SSID/password into nvs.net80211, and the studio network would then survive
  // into whatever firmware is flashed next. Same reasoning as bootstrap_main.
  //
  // Order-sensitive. Arduino applies WIFI_STORAGE_RAM inside wifiLowLevelInit(),
  // which runs once and is latched — so this has to land before anything else
  // touches the radio, or storage stays FLASH and the write happens anyway. It
  // does today only because initBleProvisioning() compiles to a stub on every
  // legacy product (BLE_PROVISIONING_ENABLED 0 on all of them), leaving this
  // the first WiFi call in setup(). Enabling BLE on a photo build would break
  // it.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  logInfo(String("[photo] Connecting to hardcoded SSID: ") + PHOTO_WIFI_SSID);
  WiFi.begin(PHOTO_WIFI_SSID, PHOTO_WIFI_PASSWORD);

  const unsigned long deadline = millis() + PHOTO_WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}
#endif

void initNetwork() {
#if PHOTO_SESSION_WIFI
  if (photoShouldTryHardcoded()) {
    g_photoHardcodedActive = true;
    // "Credentials at boot" is true by construction on the studio path; it is
    // what keeps isInitialSetupMode() false, so the face renders the time from
    // the first second rather than sitting in provisioning mode.
    g_wifiHadCredentialsAtBoot = true;
    if (connectPhotoWifi()) {
      g_wifiConnected = true;
      logInfo("✅ [photo] WiFi connected: " + String(WiFi.SSID()));
      logInfo("📡 IP address: " + WiFi.localIP().toString());
      return;
    }
    // Not at the studio. Reboot into the ordinary path rather than retry a
    // network that isn't there — otherwise this clock has no route back to the
    // OTA server and the only way to recover it is a USB cable.
    g_photoHardcodedActive = false;
    logWarn("⚠️ [photo] Studio WiFi not found — restarting into normal Wi-Fi mode.");
    g_photoFallbackMark = PHOTO_FALLBACK_MARK;
    safeRestart();  // does not return
  }
  logInfo("ℹ️ [photo] Studio network unavailable at power-on; using stored "
          "credentials / config portal. Power-cycle to try the studio again.");
#endif

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
#if defined(PRODUCT_VARIANT_MINI)
  applyMiniWifiTweaks();  // only when using WiFi; not applied before BLE so BLE provisioning is reliable
#endif
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
#if defined(PRODUCT_VARIANT_MINI)
  applyMiniWifiTweaks();
#endif
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
#if defined(PRODUCT_VARIANT_MINI)
  applyMiniWifiTweaks();
#endif
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
#if defined(PRODUCT_VARIANT_MINI)
    // Apply tweaks only when not in BLE provisioning, so BLE can send wifi_ok first (WiFi+BLE coexistence)
    if (!isBleProvisioningActive()) {
      applyMiniWifiTweaks();
    }
#endif
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
    unsigned long retryInterval = WIFI_RECONNECT_INTERVAL_MS;
#if PHOTO_SESSION_WIFI
    // Faster and more insistent than the shipping cadence while at the studio:
    // a clock that drops off mid-shoot should be back before anyone reaches
    // for it, and the portal is suppressed so there is nothing else to try.
    if (g_photoHardcodedActive) retryInterval = PHOTO_WIFI_RETRY_INTERVAL_MS;
#endif

    if (lastReconnectAttemptMs == 0 || now - lastReconnectAttemptMs >= retryInterval) {
      bool handled = false;
#if PHOTO_SESSION_WIFI
      if (g_photoHardcodedActive) {
        logInfo("🔄 [photo] Reconnecting to studio WiFi...");
        // Explicit SSID/password: persistent(false) means there is nothing in
        // flash for an argless begin() to fall back on.
        WiFi.begin(PHOTO_WIFI_SSID, PHOTO_WIFI_PASSWORD);
        handled = true;
      }
#endif
      if (!handled) {
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
      }
      lastReconnectAttemptMs = now;
      reconnectWindowStartMs = now;
    }

#if WIFI_MANAGER_ENABLED
    // After the fallback period open the config portal so the user can intervene,
    // (a no-op while the studio network is in use — see startWiFiManagerPortal),
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
