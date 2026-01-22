#include "device_registration.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "device_identity.h"
#include "display_settings.h"
#include "log.h"
#include "ota_updater.h"
#include "secrets.h"

bool register_device_with_fleet(String& outDeviceId, String& outToken, String& outError) {
  outDeviceId = "";
  outToken = "";
  outError = "";

  if (WiFi.status() != WL_CONNECTED) {
    outError = "WiFi not connected";
    return false;
  }

  const String url = String(API_BASE_URL) + "/api/v1/devices/register";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    outError = "http.begin failed";
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader(PROVISIONING_KEY_HEADER, REGISTER_API_TOKEN);

  JsonDocument req;
  req["hardwareId"] = get_hardware_id();
  req["productId"] = PRODUCT_ID;
  req["firmware"] = FIRMWARE_VERSION;
  req["uiFirmware"] = getUiVersion();
  req["otaChannel"] = displaySettings.getUpdateChannel();

  String payload;
  serializeJson(req, payload);

  int code = http.POST(payload);
  if (code <= 0) {
    outError = String("HTTP error: ") + http.errorToString(code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    String apiError;
    JsonDocument errDoc;
    if (deserializeJson(errDoc, body) == DeserializationError::Ok &&
        errDoc["error"].is<const char*>()) {
      apiError = errDoc["error"].as<const char*>();
    }
    if (code == 409) {
      outError = apiError.length() > 0 ? apiError : "Device already registered";
      return false;
    }
    if (apiError.length() > 0) {
      outError = apiError;
    } else {
      outError = String("HTTP ") + code + ": " + body;
    }
    return false;
  }

  JsonDocument resDoc;
  DeserializationError err = deserializeJson(resDoc, body);
  if (err) {
    outError = String("JSON parse error: ") + err.c_str();
    return false;
  }

  if (resDoc["deviceToken"].is<const char*>()) {
    outToken = resDoc["deviceToken"].as<const char*>();
  } else if (resDoc["token"].is<const char*>()) {
    outToken = resDoc["token"].as<const char*>();
  }
  if (resDoc["deviceId"].is<const char*>()) {
    outDeviceId = resDoc["deviceId"].as<const char*>();
  }

  if (outToken.isEmpty() || outDeviceId.isEmpty()) {
    outError = "Missing token or deviceId";
    return false;
  }

  if (!set_device_token(outToken)) {
    outError = "Failed to store device token";
    return false;
  }
  if (!set_device_id(outDeviceId)) {
    outError = "Failed to store device id";
    return false;
  }

  logInfo("✅ Device registered with fleet");
  return true;
}
