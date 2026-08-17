// nextgen-bootstrap — first-flash provisioning firmware.
//
// One job: bring up Wi-Fi, host a product picker at http://wordclock.local,
// and OTA-install the chosen product firmware + fs.bin. Once the OTA
// reboots into the chosen product, this firmware is replaced by the next
// OTA cycle. No clock features, no MQTT, no LED output.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

#if WIFI_MANAGER_ENABLED
#include <WiFiManager.h>
#endif

#include "config.h"
#include "fs_compat.h"
#include "log.h"
#include "secrets.h"
#include "bootstrap_provision.h"

// Global WebServer instance — referenced via `extern WebServer server;`
// in bootstrap_provision.cpp. (Mirrors the convention used by the per-device
// firmware in main.cpp.)
WebServer server(80);

// Bootstrap doesn't drive LEDs; provide a stub so any code that may
// reference `clockEnabled` (currently none in the bootstrap link, but the
// per-device build expects it as extern) still compiles.
bool clockEnabled = false;
bool g_wifiHadCredentialsAtBoot = false;

namespace {

constexpr const char* kPortalSsid = "Wordclock-Bootstrap";

void mountFs() {
  if (!FS_IMPL.begin(true)) {
    logError("[bootstrap] LittleFS mount failed");
    return;
  }
  logInfo("[bootstrap] LittleFS mounted");
}

// Try hardcoded factory credentials (BOOTSTRAP_WIFI_SSID/PASSWORD from
// include/secrets.h). Returns true on connect, false on timeout. If the
// SSID is empty we don't try and just return false so the caller can fall
// back to the portal.
bool connectWifiHardcoded() {
  if (sizeof(BOOTSTRAP_WIFI_SSID) <= 1) {
    return false;  // empty string at compile time
  }
  logInfo(String("[bootstrap] Trying hardcoded SSID: ") + BOOTSTRAP_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(BOOTSTRAP_WIFI_SSID, BOOTSTRAP_WIFI_PASSWORD);
  // 20s budget — enough for a healthy AP, short enough that operators
  // notice and reach for the portal fallback when the workshop AP is down.
  const unsigned long deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    logInfo(String("[bootstrap] Hardcoded Wi-Fi connected: ") + WiFi.SSID() +
            " (IP " + WiFi.localIP().toString() + ")");
    return true;
  }
  logWarn("[bootstrap] Hardcoded Wi-Fi failed; falling back to portal");
  WiFi.disconnect(true, true);
  return false;
}

void connectWifi() {
  // Path 1: try hardcoded factory creds first (if defined).
  if (connectWifiHardcoded()) return;

#if WIFI_MANAGER_ENABLED
  // Path 2: blocking portal. Factory operator joins the AP, configures
  // Wi-Fi, then returns to the device's STA URL.
  WiFiManager wm;
  wm.setConfigPortalTimeout(0);   // never timeout — operator is in front of device
  wm.setBreakAfterConfig(true);
  logInfo(String("[bootstrap] Starting Wi-Fi portal (SSID: ") + kPortalSsid + ")…");
  if (!wm.autoConnect(kPortalSsid)) {
    logError("[bootstrap] Wi-Fi portal failed; restarting");
    delay(1500);
    ESP.restart();
  }
  logInfo(String("[bootstrap] Wi-Fi connected: ") + WiFi.SSID() +
          " (IP " + WiFi.localIP().toString() + ")");
#else
  // Path 3: portal disabled and hardcoded creds didn't work. Stop.
  logError("[bootstrap] No Wi-Fi: hardcoded creds missing/wrong and portal disabled");
  delay(1500);
  ESP.restart();
#endif
}

// mDNS is the only way an operator reaches this firmware: bootstrap joins a
// factory AP whose DHCP lease they never see, and the picker lives at
// http://wordclock.local/. So a responder that dies with the Wi-Fi link — it
// is bound to the STA netif — has to come back on its own. Registration is
// therefore a loop-serviced state, not a one-shot setup step.
bool g_mdnsRegistered = false;
unsigned long g_mdnsNextAttemptMs = 0;

// How often to look at the link. The loop spins every 2 ms; polling
// WiFi.status() that often during a firmware download is pointless work.
constexpr unsigned long kMdnsCheckIntervalMs = 1000;

void startMdns() {
  // end() only when re-registering. Unlike the per-device firmware nothing
  // else here shares the mDNS stack (no ArduinoOTA), but tearing down a
  // responder that was never begun is still noise we don't need.
  static bool registeredBefore = false;
  if (registeredBefore) {
    MDNS.end();
  }
  if (!MDNS.begin(MDNS_HOSTNAME)) {
    logWarn("[bootstrap] mDNS responder failed to start");
    g_mdnsNextAttemptMs = millis() + MDNS_RETRY_INTERVAL_MS;
    return;
  }
  MDNS.addService("http", "tcp", 80);
  registeredBefore = true;
  g_mdnsRegistered = true;
  g_mdnsNextAttemptMs = 0;
  logInfo("[bootstrap] mDNS: http://" MDNS_HOSTNAME ".local");
}

// Keep the responder matched to the link state. Called from loop().
void serviceMdns() {
  static unsigned long nextCheckMs = 0;
  static bool wasConnected = true;  // setup() only gets here once connected

  const unsigned long now = millis();
  if (now < nextCheckMs) return;
  nextCheckMs = now + kMdnsCheckIntervalMs;

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    if (wasConnected) {
      logWarn("[bootstrap] Wi-Fi lost — mDNS will re-register on reconnect");
    }
    wasConnected = false;
    g_mdnsRegistered = false;
    g_mdnsNextAttemptMs = 0;
    return;
  }
  wasConnected = true;

  if (g_mdnsRegistered) return;
  if (g_mdnsNextAttemptMs != 0 && now < g_mdnsNextAttemptMs) return;
  startMdns();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  logInfo("");
  logInfo(String("=== nextgen-bootstrap ") + FIRMWARE_VERSION + " ===");

  // Bootstrap Wi-Fi credentials must NOT outlive this firmware. Disabling
  // persistence stops ESP-IDF from writing SSID/password to nvs.net80211 on
  // any subsequent WiFi.begin() (including those WiFiManager makes
  // internally). The per-device firmware that takes over after OTA must
  // always do its own enrollment from scratch — see also the explicit
  // disconnect(eraseAP=true) in safeRestart() under WORDCLOCK_BOOTSTRAP.
  WiFi.persistent(false);

  mountFs();
  connectWifi();
  startMdns();

  registerBootstrapRoutes(server);
  server.begin();
  logInfo("[bootstrap] HTTP server listening on :80");
  logInfo("[bootstrap] Open http://wordclock.local/ to pick a product.");
}

void loop() {
  serviceMdns();
  server.handleClient();
  // Yield so the provisioning task gets CPU during the long downloads.
  delay(2);
}
