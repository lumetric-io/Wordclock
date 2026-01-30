#include "ble_provisioning.h"

#include "config.h"
#include "device_identity.h"
#include "grid_layout.h"
#include "led_controller.h"
#include "log.h"
#include "time_mapper.h"

#include <WiFi.h>
#include <esp_system.h>

#if BLE_PROVISIONING_ENABLED

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

namespace {

enum class BleState : uint8_t {
  Idle = 0,
  Active,
  WifiConnecting,
};

static const char* kServiceUuid = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* kSsidUuid = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static const char* kPassUuid = "beb5483e-36e1-4688-b7f5-ea07361b26a9";
static const char* kStatusUuid = "beb5483e-36e1-4688-b7f5-ea07361b26aa";
static const char* kCmdUuid = "beb5483e-36e1-4688-b7f5-ea07361b26ab";

BLEServer* g_server = nullptr;
BLEService* g_service = nullptr;
BLECharacteristic* g_ssidChar = nullptr;
BLECharacteristic* g_passChar = nullptr;
BLECharacteristic* g_statusChar = nullptr;
BLECharacteristic* g_cmdChar = nullptr;

BleState g_state = BleState::Idle;
bool g_bleActive = false;
bool g_bleTimedOut = false;
bool g_hasClient = false;
bool g_connectRequested = false;
unsigned long g_lastStatusNotifyMs = 0;

String g_ssid;
String g_pass;
unsigned long g_bleStartMs = 0;
unsigned long g_wifiConnectStartMs = 0;
uint32_t g_wifiAttempt = 0;
String g_bleReason = "";

uint32_t g_passkey = 0;
uint8_t g_passkeyDigits[6] = {0};
uint8_t g_passkeyIndex = 0;
bool g_passkeyShowing = false;
unsigned long g_passkeyLastToggleMs = 0;

const char* bleReasonToString(BleProvisioningReason reason);
String wifiStatusToReason(wl_status_t status);

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

void notifyStatus(const String& status) {
  if (!g_statusChar) return;
  g_statusChar->setValue(status.c_str());
  if (g_hasClient) {
    g_statusChar->notify();
  }
}

void notifyStatusJson(const String& state, const String& detailKey = "", const String& detailValue = "") {
  String payload = String("{\"state\":\"") + jsonEscape(state) + "\"";
  payload += String(",\"hardware_id\":\"") + jsonEscape(get_hardware_id()) + "\"";
  payload += String(",\"uptime_ms\":\"") + String(millis()) + "\"";
  payload += String(",\"wifi_status\":\"") + String(WiFi.status()) + "\"";
  payload += String(",\"rssi\":\"") + String(WiFi.RSSI()) + "\"";
  payload += String(",\"attempt\":\"") + String(g_wifiAttempt) + "\"";
  if (g_bleReason.length() > 0) {
    payload += String(",\"ble_reason\":\"") + jsonEscape(g_bleReason) + "\"";
  }
  if (g_ssid.length() > 0) {
    payload += String(",\"ssid\":\"") + jsonEscape(g_ssid) + "\"";
  }
  if (detailKey.length() > 0) {
    payload += String(",\"") + jsonEscape(detailKey) + "\":\"" + jsonEscape(detailValue) + "\"";
  }
  payload += "}";
  notifyStatus(payload);
}

String buildDeviceName() {
  String hw = get_hardware_id();
  if (hw.length() < 5) return String(BLE_DEVICE_NAME_PREFIX) + hw;
  return String(BLE_DEVICE_NAME_PREFIX) + hw.substring(hw.length() - 5);
}

void generatePasskey() {
  // Avoid digit 0 for word-style display (keep digits 1-9 only).
  const int maxAttempts = 64;
  for (int attempt = 0; attempt < maxAttempts; ++attempt) {
    uint32_t candidate = esp_random() % 1000000;
    uint16_t digitMask = 0;
    bool hasZero = false;
    bool hasRepeat = false;
    for (int i = 5; i >= 0; --i) {
      g_passkeyDigits[i] = candidate % 10;
      if (g_passkeyDigits[i] == 0) {
        hasZero = true;
      }
      uint16_t bit = static_cast<uint16_t>(1U << g_passkeyDigits[i]);
      if (digitMask & bit) {
        hasRepeat = true;
      } else {
        digitMask |= bit;
      }
      candidate /= 10;
    }
    if (hasZero || hasRepeat) continue;
    g_passkey = 0;
    for (int i = 0; i < 6; ++i) {
      g_passkey = g_passkey * 10 + g_passkeyDigits[i];
    }
    return;
  }

  // Fallback: deterministic non-zero passkey.
  uint8_t pool[9] = {1,2,3,4,5,6,7,8,9};
  for (int i = 8; i > 0; --i) {
    int j = static_cast<int>(esp_random() % (i + 1));
    uint8_t tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
  }
  for (int i = 0; i < 6; ++i) {
    g_passkeyDigits[i] = pool[i];
  }
  g_passkey = 0;
  for (int i = 0; i < 6; ++i) {
    g_passkey = g_passkey * 10 + g_passkeyDigits[i];
  }
}

std::vector<uint16_t> ledsForDigit(uint8_t digit) {
  static const char* kDigitWords[] = {
    "NUL", "EEN", "TWEE", "DRIE", "VIER",
    "VIJF", "ZES", "ZEVEN", "ACHT", "NEGEN"
  };
  if (digit > 9) return {};
  auto leds = get_leds_for_word(kDigitWords[digit]);
  if (leds.empty()) {
    static bool warned = false;
    if (!warned) {
      logWarn(String("🔵 No LED mapping for digit word: ") + kDigitWords[digit]);
      warned = true;
    }
  }
  return leds;
}

std::vector<uint16_t> cornerLeds() {
  std::vector<uint16_t> leds;
#if SUPPORT_MINUTE_LEDS
  if (EXTRA_MINUTE_LED_COUNT >= 4) {
    leds.assign(EXTRA_MINUTE_LEDS, EXTRA_MINUTE_LEDS + 4);
  }
#endif
  return leds;
}

void showPasskeyFrame() {
  if (!BLE_PASSKEY_DISPLAY_ENABLED) return;
  if (g_passkeyIndex >= 6) g_passkeyIndex = 0;
  if (!g_passkeyShowing) {
    showLeds({});
    return;
  }
  std::vector<uint16_t> leds = ledsForDigit(g_passkeyDigits[g_passkeyIndex]);
  if (g_passkeyIndex == 0) {
    auto corners = cornerLeds();
    leds.insert(leds.end(), corners.begin(), corners.end());
  }
  showLeds(leds);
}

void updatePasskeyDisplay(unsigned long nowMs) {
  if (!BLE_PASSKEY_DISPLAY_ENABLED || !g_bleActive) return;
  const unsigned long onMs = BLE_PASSKEY_ON_MS;
  const unsigned long offMs = BLE_PASSKEY_OFF_MS;
  const unsigned long interval = g_passkeyShowing ? onMs : offMs;
  if (nowMs - g_passkeyLastToggleMs < interval) return;
  g_passkeyLastToggleMs = nowMs;
  g_passkeyShowing = !g_passkeyShowing;
  if (g_passkeyShowing) {
    showPasskeyFrame();
  } else {
    showLeds({});
    g_passkeyIndex = static_cast<uint8_t>((g_passkeyIndex + 1) % 6);
  }
}

void startWifiConnect() {
  if (g_ssid.length() == 0 || g_pass.length() == 0) return;
  g_state = BleState::WifiConnecting;
  g_wifiConnectStartMs = millis();
  g_wifiAttempt += 1;
  logInfo(String("🔵 BLE WiFi connect starting (SSID=") + g_ssid + ")");
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());
  notifyStatusJson("wifi_connecting");
  g_lastStatusNotifyMs = g_wifiConnectStartMs;
  g_connectRequested = false;
}

class ProvisioningCallbacks : public BLECharacteristicCallbacks {
public:
  explicit ProvisioningCallbacks(const char* label) : label_(label) {}

  void onWrite(BLECharacteristic* characteristic) override {
    std::string val = characteristic->getValue();
    String value = String(val.c_str());
    value.trim(); // Avoid hidden whitespace/newlines from BLE clients
    logInfo(String("🔵 BLE write: ") + label_);
    if (strcmp(label_, "ssid") == 0) {
      g_ssid = value;
    } else if (strcmp(label_, "pass") == 0) {
      g_pass = value;
    } else if (strcmp(label_, "cmd") == 0) {
      if (value == "apply" || value == "APPLY") {
        g_connectRequested = true;
        return;
      }
      if (value == "stop" || value == "STOP") {
        notifyStatusJson("ble_stop_ack");
        stopBleProvisioning();
        return;
      }
    }
    if (g_ssid.length() > 0 || g_pass.length() > 0) {
      notifyStatusJson("creds_partial");
    }
    if (g_ssid.length() > 0 && g_pass.length() > 0) {
      notifyStatusJson("creds_received");
      g_connectRequested = true;
    }
  }

private:
  const char* label_;
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    g_hasClient = true;
    notifyStatus("ble_connected");
  }
  void onDisconnect(BLEServer* server) override {
    g_hasClient = false;
    notifyStatus("ble_disconnected");
    if (g_bleActive) {
      server->getAdvertising()->start();
    }
  }
};

void startAdvertising() {
  if (!g_server) return;
  BLEAdvertising* advertising = g_server->getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  advertising->start();
}

const char* bleReasonToString(BleProvisioningReason reason) {
  switch (reason) {
    case BleProvisioningReason::FirstBootNoCreds:
      return "first_boot_no_creds";
    case BleProvisioningReason::WiFiUnavailableAtBoot:
      return "wifi_unavailable_at_boot";
    case BleProvisioningReason::ManualTrigger:
      return "manual_trigger";
    default:
      return "unknown";
  }
}

String wifiStatusToReason(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:
      return "no_ssid";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
#ifdef WL_WRONG_PASSWORD
    case WL_WRONG_PASSWORD:
      return "wrong_password";
#endif
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "timeout";
  }
}

} // namespace

void initBleProvisioning() {
  logInfo("🔵 BLE provisioning init (enabled)");
}

void processBleProvisioning() {
  if (!g_bleActive) return;
  unsigned long now = millis();
  updatePasskeyDisplay(now);

  if (g_state == BleState::WifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      notifyStatusJson("wifi_ok", "ip", WiFi.localIP().toString());
      g_state = BleState::Active;
      return;
    }
    if (now - g_lastStatusNotifyMs >= 1000) {
      notifyStatusJson("wifi_connecting");
      g_lastStatusNotifyMs = now;
    }
    const unsigned long timeoutMs = WIFI_CONNECT_MAX_RETRIES * WIFI_CONNECT_RETRY_DELAY_MS;
    if (now - g_wifiConnectStartMs > timeoutMs) {
      const wl_status_t status = WiFi.status();
      const char* state = "wifi_fail";
#ifdef WL_WRONG_PASSWORD
      if (status == WL_WRONG_PASSWORD) {
        state = "wifi_auth_fail";
      }
#endif
      notifyStatusJson(state, "reason", wifiStatusToReason(status));
      g_state = BleState::Active;
      WiFi.disconnect(true);
    }
  }

  if (g_connectRequested && g_state != BleState::WifiConnecting) {
    startWifiConnect();
  }

#if !BLE_PROVISIONING_DISABLE_TIMEOUT
  if (now - g_bleStartMs > (BLE_PROVISIONING_TIMEOUT_SEC * 1000UL)) {
    logWarn("🔵 BLE provisioning timeout reached");
    g_bleTimedOut = true;
    stopBleProvisioning();
  }
#endif
}

void startBleProvisioning(BleProvisioningReason reason) {
  if (g_bleActive) return;
  g_bleActive = true;
  g_bleTimedOut = false;
  g_state = BleState::Active;
  g_bleStartMs = millis();
  g_passkeyIndex = 0;
  g_passkeyShowing = true;
  g_passkeyLastToggleMs = g_bleStartMs;
  g_bleReason = String(bleReasonToString(reason));
  g_wifiAttempt = 0;
  g_connectRequested = false;
  g_lastStatusNotifyMs = 0;

  logInfo(String("🔵 BLE provisioning start, reason=") + static_cast<int>(reason));
  logInfo(String("🔵 BLE timeout (sec): ") + BLE_PROVISIONING_TIMEOUT_SEC);
  if (BLE_PASSKEY_DISPLAY_ENABLED) {
    generatePasskey();
    logInfo(String("🔵 BLE passkey: ") + g_passkey);
    showPasskeyFrame();
  }

  String deviceName = buildDeviceName();
  BLEDevice::init(deviceName.c_str());
  // Provisioning should be usable without OS pairing/bonding; do not enable BLE security here.

  g_server = BLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());
  g_service = g_server->createService(kServiceUuid);

  g_ssidChar = g_service->createCharacteristic(kSsidUuid, BLECharacteristic::PROPERTY_WRITE);
  g_passChar = g_service->createCharacteristic(kPassUuid, BLECharacteristic::PROPERTY_WRITE);
  g_statusChar = g_service->createCharacteristic(kStatusUuid, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  g_cmdChar = g_service->createCharacteristic(kCmdUuid, BLECharacteristic::PROPERTY_WRITE);

  g_statusChar->addDescriptor(new BLE2902());
  g_ssidChar->setCallbacks(new ProvisioningCallbacks("ssid"));
  g_passChar->setCallbacks(new ProvisioningCallbacks("pass"));
  g_cmdChar->setCallbacks(new ProvisioningCallbacks("cmd"));

  g_service->start();
  notifyStatusJson("ble_ready");
  startAdvertising();
}

void stopBleProvisioning() {
  if (!g_bleActive) return;
  g_bleActive = false;
  g_state = BleState::Idle;
  g_ssid = "";
  g_pass = "";
  g_connectRequested = false;
  if (g_server) {
    g_server->getAdvertising()->stop();
  }
  if (g_statusChar) {
    notifyStatusJson("ble_stopped");
  }
  showLeds({});
  logInfo("🔵 BLE provisioning stop");
}

bool isBleProvisioningActive() {
  return g_bleActive;
}

bool takeBleProvisioningTimeout() {
  if (!g_bleTimedOut) return false;
  g_bleTimedOut = false;
  return true;
}

const char* getBleProvisioningState() {
  switch (g_state) {
    case BleState::Idle: return "idle";
    case BleState::Active: return "active";
    case BleState::WifiConnecting: return "wifi_connecting";
    default: return "unknown";
  }
}

#else

void initBleProvisioning() {}
void processBleProvisioning() {}
void startBleProvisioning(BleProvisioningReason) {}
void stopBleProvisioning() {}
bool isBleProvisioningActive() { return false; }
bool takeBleProvisioningTimeout() { return false; }
const char* getBleProvisioningState() { return "disabled"; }

#endif
