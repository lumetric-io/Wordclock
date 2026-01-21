#include "device_identity.h"

#include <Preferences.h>
#include <esp_system.h>

static const char* NS = "wc_system";
static const char* KEY_DEVICE_ID = "device_id";
static const char* KEY_DEVICE_TOKEN = "device_token";

String get_device_id() {
  Preferences prefs;
  prefs.begin(NS, true);
  String id = prefs.getString(KEY_DEVICE_ID, "");
  prefs.end();
  return id;
}

bool set_device_id(const String& id) {
  Preferences prefs;
  if (!prefs.begin(NS, false)) return false;
  prefs.putString(KEY_DEVICE_ID, id);
  prefs.end();
  return true;
}

String get_hardware_id() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[13];
  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
           (uint8_t)(mac >> 40),
           (uint8_t)(mac >> 32),
           (uint8_t)(mac >> 24),
           (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 8),
           (uint8_t)(mac));
  return String(buf);
}

String get_device_token() {
  Preferences prefs;
  prefs.begin(NS, true);
  String token = prefs.getString(KEY_DEVICE_TOKEN, "");
  prefs.end();
  return token;
}

bool set_device_token(const String& token) {
  Preferences prefs;
  if (!prefs.begin(NS, false)) return false;
  prefs.putString(KEY_DEVICE_TOKEN, token);
  prefs.end();
  return true;
}
