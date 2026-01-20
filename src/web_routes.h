#pragma once
#include "network_init.h"
#include "fs_compat.h"
#include <Update.h>
#include <WebServer.h>
#include <esp_system.h>
#include <ctype.h>
#include <PubSubClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <map>
#include "secrets.h"
#include "sequence_controller.h"
#include "led_state.h"
#if defined(PRODUCT_VARIANT_LOGO)
#include "logo_leds.h"
#endif
#include "log.h"
#include "time_mapper.h"
#include "ota_updater.h"
#include "led_controller.h"
#include "config.h"
#include "display_settings.h"
#include "ui_auth.h"
#include "wordclock.h"
#include "clock_display.h"
#include "mqtt_settings.h"
#include "mqtt_client.h"
#include "night_mode.h"
#include "build_info.h"
#include "setup_state.h"
#include "system_utils.h"
#include <WiFi.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


// References to global variables
extern WebServer server;
extern String logBuffer[];
extern int logIndex;
extern bool clockEnabled;
extern bool g_wifiHadCredentialsAtBoot;

static TaskHandle_t g_otaTaskHandle = nullptr;
static volatile bool g_otaTaskRunning = false;

static void otaUpdateTask(void* params) {
  (void)params;
  logInfo("🧵 OTA update task started");
  setLedsSuspended(true);
  checkForFirmwareUpdate();
  setLedsSuspended(false);
  g_otaTaskRunning = false;
  g_otaTaskHandle = nullptr;
  logInfo("🧵 OTA update task finished");
  vTaskDelete(nullptr);
}

// Serve file, preferring a .gz variant if client accepts gzip
static void serveFile(const char* path, const char* mime) {
  // Gzip temporarily disabled; always serve plain files
  bool acceptGzip = false;
  String gzPath = String(path) + ".gz";
  if (acceptGzip) {
    File gz = FS_IMPL.open(gzPath, "r");
    if (gz) {
      server.sendHeader("Content-Encoding", "gzip");
      server.streamFile(gz, mime);
      gz.close();
      return;
    }
  }
  File f = FS_IMPL.open(path, "r");
  if (!f) {
    File gz = FS_IMPL.open(gzPath, "r");
    if (gz) {
      server.sendHeader("Content-Encoding", "gzip");
      server.streamFile(gz, mime);
      gz.close();
      return;
    }
    server.send(404, "text/plain", String(path) + " not found");
    return;
  }
  server.streamFile(f, mime);
  f.close();
}
// Simple Basic-Auth guard for admin resources
static bool ensureAdminAuth() {
  if (!server.authenticate(ADMIN_USER, ADMIN_PASS)) {
    server.requestAuthentication(BASIC_AUTH, ADMIN_REALM);
    return false;
  }
  return true;
}

// UI is now open; only admin pages are protected
static bool ensureUiAuth() {
  return true;
}

static void sendSetupStatus() {
  JsonDocument doc;
  doc["completed"] = setupState.isComplete();
  doc["version"] = setupState.getVersion();
  doc["migrated"] = setupState.wasMigrated();
  bool staConnected = (WiFi.status() == WL_CONNECTED) || isWiFiConnected() || WiFi.isConnected();
  String ssid = staConnected ? WiFi.SSID() : WiFi.softAPSSID();
  IPAddress ip = staConnected ? WiFi.localIP() : WiFi.softAPIP();
  bool hasSavedSsid = WiFi.SSID().length() > 0;
  bool hasIp = ip != IPAddress(0, 0, 0, 0);
  doc["wifi_connected"] = staConnected || hasIp;
  doc["wifi_configured"] = staConnected || g_wifiHadCredentialsAtBoot || hasSavedSsid || hasIp;
  doc["wifi_ssid"] = ssid.length() ? ssid : (staConnected ? "unknown" : "AP/Portal");
  doc["wifi_ip"] = hasIp ? ip.toString() : "";
  GridVariant active = displaySettings.getGridVariant();
  doc["grid_variant_id"] = gridVariantToId(active);
  if (const auto* info = getGridVariantInfo(active)) {
    doc["grid_variant_key"] = info->key;
    doc["grid_variant_label"] = info->label;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static const char* nightEffectToStr(NightModeEffect effect) {
  return (effect == NightModeEffect::Off) ? "off" : "dim";
}

static const char* nightOverrideToStr(NightModeOverride mode) {
  switch (mode) {
    case NightModeOverride::ForceOn:  return "force_on";
    case NightModeOverride::ForceOff: return "force_off";
    case NightModeOverride::Auto:
    default:                          return "auto";
  }
}

static void sendNightModeConfig() {
  JsonDocument doc;
  doc["enabled"] = nightMode.isEnabled();
  doc["effect"] = nightEffectToStr(nightMode.getEffect());
  doc["dim_percent"] = nightMode.getDimPercent();
  doc["start"] = nightMode.formatMinutes(nightMode.getStartMinutes());
  doc["end"] = nightMode.formatMinutes(nightMode.getEndMinutes());
  doc["start_minutes"] = nightMode.getStartMinutes();
  doc["end_minutes"] = nightMode.getEndMinutes();
  doc["override"] = nightOverrideToStr(nightMode.getOverride());
  doc["active"] = nightMode.isActive();
  doc["schedule_active"] = nightMode.isScheduleActive();
  doc["time_synced"] = nightMode.hasTime();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

#if defined(PRODUCT_VARIANT_LOGO)
static bool parseHexColor(String hex, uint8_t& r, uint8_t& g, uint8_t& b) {
  String filtered;
  filtered.reserve(hex.length());
  for (size_t i = 0; i < hex.length(); ++i) {
    char c = hex.charAt(i);
    if (isxdigit(static_cast<unsigned char>(c))) {
      filtered += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
  }
  if (filtered.length() != 6) return false;
  long val = strtol(filtered.c_str(), nullptr, 16);
  r = (val >> 16) & 0xFF;
  g = (val >> 8) & 0xFF;
  b =  val       & 0xFF;
  return true;
}

static void refreshCurrentTimeDisplay() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    std::vector<uint16_t> indices = get_led_indices_for_time(&timeinfo);
    showLeds(indices);
  }
}

static void sendLogoState() {
  JsonDocument doc;
  doc["brightness"] = logoLeds.getBrightness();
  doc["count"] = LOGO_LED_COUNT;
  doc["start"] = getLogoStartIndex();
  JsonArray arr = doc["colors"].to<JsonArray>();
  const LogoLedColor* colors = logoLeds.getColors();
  for (uint16_t i = 0; i < LOGO_LED_COUNT; ++i) {
    char buf[7];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", colors[i].r, colors[i].g, colors[i].b);
    arr.add(String(buf));
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}
#endif

// Clear persistent settings (factory reset helper)
static void performFactoryReset() {
  Preferences p;
  const char* keys[] = { "ui_auth", "display", "led", "log", "setup" };
  for (auto ns : keys) {
    p.begin(ns, false);
    p.clear();
    p.end();
  }
}

// Clear all log files (helper function)
static void clearAllLogFiles() {
  logFlushFile();
  logCloseFile();
  size_t deleted = 0;
  size_t failed = 0;
  File dir = FS_IMPL.open("/logs");
  if (dir) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String name = entry.name();
        // Normalize path: remove leading "/" and "logs/" prefix if present
        if (name.startsWith("/")) name = name.substring(1);
        if (name.startsWith("logs/")) name = name.substring(5);
        String full = String("/logs/") + name;
        entry.close();
        if (FS_IMPL.remove(full)) {
          deleted++;
        } else {
          failed++;
          logWarn("Failed to remove log file: " + full);
        }
      } else {
        entry.close();
      }
    }
    dir.close();
    logInfo("🗑️ Cleared " + String(deleted) + " log files" + (failed > 0 ? " (" + String(failed) + " failed)" : ""));
  } else {
    logWarn("Failed to open /logs directory for clearing");
  }
  // Re-enable file sink (will recreate today's file if needed)
  logEnableFileSink();
}

// Token for allowing factory reset from Forgot Password page
static String g_factoryToken;
static unsigned long g_factoryTokenExp = 0; // millis deadline

static String generateFactoryToken(unsigned long ttl_ms = 60000) {
  char buf[24];
  uint32_t r = esp_random();
  snprintf(buf, sizeof(buf), "%08X%08lX", (unsigned)r, (unsigned long)millis());
  g_factoryToken = String(buf);
  g_factoryTokenExp = millis() + ttl_ms;
  return g_factoryToken;
}

// Function to register all routes
void setupWebRoutes() {
  // Capture Accept-Encoding so we can serve gzip if available
  static const char* headerKeys[] = { "Accept-Encoding" };
  server.collectHeaders(headerKeys, 1);

  // Helper defined at file scope: serveFile()
  // Main pages
  // Dashboard (protected)
  server.on("/dashboard.html", HTTP_GET, []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /dashboard.html: Auth failed");
      return;
    }
    if (!setupState.isComplete()) {
      server.sendHeader("Location", "/setup.html", true);
      server.send(302, "text/plain", "");
      return;
    }
    serveFile("/dashboard.html", "text/html");
  });

  // Favicon placeholder to avoid 404 noise
  server.on("/favicon.ico", HTTP_GET, []() {
    server.send(204);
  });

  // Factory reset token endpoint (public): returns a short-lived token for reset
  server.on("/factorytoken", HTTP_GET, []() {
    // Issue new token valid for 60s
    String tok = generateFactoryToken(60000);
    server.send(200, "text/plain", tok);
  });

  // Factory reset (admin or valid token, requires POST). Resets preferences + WiFi and restarts.
  server.on("/factoryreset", HTTP_POST, []() {
    bool allowed = false;
    // Admin credentials allow reset unconditionally
    if (server.authenticate(ADMIN_USER, ADMIN_PASS)) {
      allowed = true;
    } else {
      // Check for valid short-lived token
      if (server.hasArg("token")) {
        String tok = server.arg("token");
        if (tok.length() > 0 && tok == g_factoryToken) {
          unsigned long now = millis();
          if (g_factoryTokenExp != 0 && (long)(g_factoryTokenExp - now) > 0) {
            allowed = true;
          }
        }
      }
    }
    if (!allowed) {
      // Admin/token required; return 403 without triggering browser auth popups
      server.send(403, "text/plain", "Forbidden (admin or valid token required)");
      return;
    }
    server.send(200, "text/html", R"rawliteral(
      <html>
        <head><meta http-equiv='refresh' content='8;url=/' /></head>
        <body>
          <h1>Factory reset started...</h1>
          <p>The device will reset to factory defaults and reboot shortly.</p>
        </body>
      </html>
    )rawliteral");
    delay(200);
    performFactoryReset();
    resetWiFiSettings(); // will restart
  });

  // Change password page (protected, but accessible during forced-change flow)
  server.on("/changepw.html", HTTP_GET, []() {
    if (!ensureAdminAuth()) return;
    serveFile("/changepw.html", "text/html");
  });

  // Handle password change
  server.on("/setUIPassword", HTTP_POST, []() {
    if (!ensureAdminAuth()) return;
    if (!server.hasArg("new") || !server.hasArg("confirm")) {
      server.send(400, "text/plain", "Missing fields");
      return;
    }
    String n = server.arg("new");
    String c = server.arg("confirm");
    if (n != c) { server.send(400, "text/plain", "Passwords do not match"); return; }
    if (n.length() < 6) { server.send(400, "text/plain", "Minimum 6 characters"); return; }
    if (!uiAuth.setPassword(n)) { server.send(500, "text/plain", "Save failed"); return; }
    server.send(200, "text/plain", "OK");
  });

  // Protected admin page (Admin auth only)
  server.on("/admin.html", HTTP_GET, []() {
    if (!ensureAdminAuth()) return;
    serveFile("/admin.html", "text/html");
  });
  server.on("/logs.html", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    serveFile("/logs.html", "text/html");
  });

  // Setup page (public). Used when the wizard has not completed yet.
  server.on("/setup.html", HTTP_GET, []() {
    File f = FS_IMPL.open("/setup.html", "r");
    if (!f) {
      server.send(404, "text/plain", "setup.html not found");
      return;
    }
    f.close();
    serveFile("/setup.html", "text/html");
  });

  // Public landing page: go straight to dashboard (or setup if incomplete)
  server.on("/", HTTP_GET, []() {
    if (!setupState.isComplete()) {
      File sf = FS_IMPL.open("/setup.html", "r");
      if (sf) {
        sf.close();
        serveFile("/setup.html", "text/html");
        return;
      }
      logWarn("[API] /: setup.html not found");
    }
    File f = FS_IMPL.open("/dashboard.html", "r");
    if (!f) {
      logError("[API] /: dashboard.html not found");
      server.send(404, "text/plain", "dashboard.html not found");
      return;
    }
    f.close();
    serveFile("/dashboard.html", "text/html");
  });

  server.on("/api/setup/status", HTTP_GET, []() {
    sendSetupStatus();
  });

  server.on("/api/setup/complete", HTTP_POST, []() {
    setupState.markComplete();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      wordclock_force_animation_for_time(&timeinfo);
    }
    server.send(200, "text/plain", "OK");
  });

  // Grid endpoints for the setup flow (open when setup is pending; require auth afterwards)
  server.on("/api/setup/grid", HTTP_GET, []() {
    JsonDocument doc;
    JsonArray arr = doc["variants"].to<JsonArray>();
    size_t count = 0;
    const GridVariantInfo* infos = getGridVariantInfos(count);
    GridVariant active = displaySettings.getGridVariant();
    for (size_t i = 0; i < count; ++i) {
      JsonObject o = arr.add<JsonObject>();
      o["id"] = gridVariantToId(infos[i].variant);
      o["key"] = infos[i].key;
      o["label"] = infos[i].label;
      o["language"] = infos[i].language;
      o["version"] = infos[i].version;
      o["active"] = (infos[i].variant == active);
    }
    doc["completed"] = setupState.isComplete();
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/setup/grid", HTTP_POST, []() {
    if (setupState.isComplete()) {
      if (!ensureUiAuth()) return;
    }

    bool updated = false;
    if (server.hasArg("id")) {
      uint8_t id = static_cast<uint8_t>(server.arg("id").toInt());
      size_t count = 0;
      getGridVariantInfos(count);
      if (id < count) {
        GridVariant variant = gridVariantFromId(id);
        displaySettings.setGridVariant(variant);
        updated = true;
      }
    } else if (server.hasArg("key")) {
      String key = server.arg("key");
      GridVariant variant = gridVariantFromKey(key.c_str());
      const GridVariantInfo* info = getGridVariantInfo(variant);
      if (info && key == info->key) {
        displaySettings.setGridVariant(variant);
        updated = true;
      }
    }

    if (!updated) {
      server.send(400, "text/plain", "Invalid grid variant");
      return;
    }

    if (const GridVariantInfo* info = getGridVariantInfo(displaySettings.getGridVariant())) {
      logInfo(String("🧩 Grid variant updated (setup) to ") + info->label + " (" + info->key + ")");
    }

    JsonDocument doc;
    GridVariant variant = displaySettings.getGridVariant();
    doc["id"] = gridVariantToId(variant);
    if (const GridVariantInfo* info = getGridVariantInfo(variant)) {
      doc["key"] = info->key;
      doc["label"] = info->label;
      doc["language"] = info->language;
      doc["version"] = info->version;
    }
    doc["completed"] = setupState.isComplete();
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // Update page (protected)
  server.on("/update.html", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    if (!setupState.isComplete()) {
      server.sendHeader("Location", "/setup.html", true);
      server.send(302, "text/plain", "");
      return;
    }
    serveFile("/update.html", "text/html");
  });

  // MQTT settings page (protected)
  server.on("/mqtt.html", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    if (!setupState.isComplete()) {
      server.sendHeader("Location", "/setup.html", true);
      server.send(302, "text/plain", "");
      return;
    }
    serveFile("/mqtt.html", "text/html");
  });

  // MQTT config API
  server.on("/api/mqtt/config", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    MqttSettings cfg;
    mqtt_settings_load(cfg);
    // Do not expose password; indicate if set
    String json = "{";
    json += "\"host\":\"" + cfg.host + "\",";
    json += "\"port\":" + String(cfg.port) + ",";
    json += "\"user\":\"" + cfg.user + "\",";
    json += "\"has_pass\":" + String(cfg.pass.length() > 0 ? "true" : "false") + ",";
    json += "\"allow_unauth\":" + String(cfg.allowAnonymous ? "true" : "false") + ",";
    json += "\"discovery\":\"" + cfg.discoveryPrefix + "\",";
    json += "\"base\":\"" + cfg.baseTopic + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/mqtt/config", HTTP_POST, []() {
    if (!ensureUiAuth()) return;
    MqttSettings current;
    mqtt_settings_load(current);
    MqttSettings next = current;

    if (server.hasArg("host")) next.host = server.arg("host");
    if (server.hasArg("port")) next.port = (uint16_t) server.arg("port").toInt();
    if (server.hasArg("user")) next.user = server.arg("user");
    if (server.hasArg("allow_unauth")) next.allowAnonymous = server.arg("allow_unauth") == "1" || server.arg("allow_unauth") == "true" || server.arg("allow_unauth") == "on";
    if (server.hasArg("pass")) {
      String p = server.arg("pass");
      if (p.length() > 0) next.pass = p; // empty means keep existing
    }
    if (server.hasArg("discovery")) next.discoveryPrefix = server.arg("discovery");
    if (server.hasArg("base")) next.baseTopic = server.arg("base");

    // Basic validation
    if (next.host.length() == 0 || next.port == 0) {
      server.send(400, "text/plain", "host/port required");
      return;
    }
    if (next.allowAnonymous) {
      // explicit opt-out from auth -> clear any existing credentials
      next.user = "";
      next.pass = "";
    } else {
      // Require user+pass when auth is enabled. Allow keeping existing password if already set.
      bool hasUser = next.user.length() > 0;
      bool hasPass = next.pass.length() > 0;
      if (!hasUser || !hasPass) {
        server.send(400, "text/plain", "user/password required unless 'no auth' is checked");
        return;
      }
    }

    mqtt_apply_settings(next);
    server.send(200, "text/plain", "OK");
  });

  // MQTT runtime status
  server.on("/api/mqtt/status", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    bool c = mqtt_is_connected();
    String json = String("{\"connected\":") + (c ? "true" : "false") + 
                  ",\"last_error\":\"" + mqtt_last_error() + "\"}";
    server.send(200, "application/json", json);
  });

  // Force MQTT reconnection (clears abort state)
  server.on("/api/mqtt/reconnect", HTTP_POST, []() {
    if (!ensureUiAuth()) return;
    logInfo("🔄 Force MQTT reconnect requested via web UI");
    mqtt_force_reconnect();
    server.send(200, "text/plain", "MQTT reconnection triggered");
  });

  // MQTT connection test (does not save). Accepts form-encoded: host, port, user?, pass?
  server.on("/api/mqtt/test", HTTP_POST, []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("host") || !server.hasArg("port")) {
      server.send(400, "text/plain", "host/port required");
      return;
    }
    String host = server.arg("host");
    uint16_t port = (uint16_t) server.arg("port").toInt();
    String user = server.arg("user");
    String pass = server.arg("pass");
    bool allowUnauth = server.hasArg("allow_unauth") && (server.arg("allow_unauth") == "1" || server.arg("allow_unauth") == "true" || server.arg("allow_unauth") == "on");
    if (!allowUnauth) {
      if (user.length() == 0 || pass.length() == 0) {
        server.send(400, "text/plain", "user/password required unless 'no auth' is checked");
        return;
      }
    }

    // Quick TCP reachability test
    WiFiClient testClient;
    testClient.setTimeout(3000);
    if (!testClient.connect(host.c_str(), port)) {
      server.send(502, "text/plain", "TCP connect failed");
      return;
    }
    testClient.stop();

    // Optional MQTT handshake if auth requested
    if (!allowUnauth) {
      WiFiClient mc;
      PubSubClient tmp(mc);
      tmp.setServer(host.c_str(), port);
      String cid = String("wordclock_test_") + String(millis());
      bool ok = tmp.connect(cid.c_str(), user.c_str(), pass.c_str());
      if (!ok) {
        int st = tmp.state();
        server.send(401, "text/plain", String("MQTT auth failed (state ") + st + ")");
        return;
      }
      tmp.disconnect();
    }

    server.send(200, "text/plain", "OK");
  });

  // Auto update toggle
  server.on("/getAutoUpdate", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /getAutoUpdate: Auth failed");
      return;
    }
    bool autoUpdate = displaySettings.getAutoUpdate();
    String result = autoUpdate ? "on" : "off";
    server.send(200, "text/plain", result);
  });
  server.on("/setAutoUpdate", []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("state")) {
      server.send(400, "text/plain", "Missing state");
      return;
    }
    String st = server.arg("state");
    bool on = (st == "on" || st == "1" || st == "true");
    if (on && displaySettings.getUpdateChannel() == "develop") {
      server.send(400, "text/plain", "Automatic updates are disabled on the develop channel");
      return;
    }
    displaySettings.setAutoUpdate(on);
  logInfo(String("🔁 Auto firmware updates ") + (on ? "ON" : "OFF"));
    server.send(200, "text/plain", "OK");
  });

  // Update channel (stable/early)
  server.on("/api/update/channel", HTTP_GET, []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /api/update/channel: Auth failed");
      return;
    }
    JsonDocument doc;
    String channel = displaySettings.getUpdateChannel();
    doc["channel"] = channel;
    doc["default"] = "stable";
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/update/channel", HTTP_POST, []() {
    if (!ensureUiAuth()) return;
    String ch;
    if (server.hasArg("channel")) {
      ch = server.arg("channel");
    } else if (server.hasArg("plain")) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (!err && doc["channel"].is<const char*>()) {
        ch = String(doc["channel"].as<const char*>());
      }
    }
    ch.toLowerCase();
    if (ch != "stable" && ch != "early" && ch != "develop") {
      server.send(400, "text/plain", "channel must be 'stable', 'early', or 'develop'");
      return;
    }
    displaySettings.setUpdateChannel(ch);
    mqtt_publish_state(true);
    JsonDocument doc;
    doc["channel"] = displaySettings.getUpdateChannel();
    doc["default"] = "stable";
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // Grid variant endpoints
  server.on("/getGridVariant", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /getGridVariant: Auth failed");
      return;
    }
    JsonDocument doc;
    GridVariant variant = displaySettings.getGridVariant();
    doc["id"] = gridVariantToId(variant);
    const GridVariantInfo* info = getGridVariantInfo(variant);
    if (info) {
      doc["key"] = info->key;
      doc["label"] = info->label;
      doc["language"] = info->language;
      doc["version"] = info->version;
    } else {
      logWarn("[API] /getGridVariant: No info found for variant ID " + String(gridVariantToId(variant)));
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/listGridVariants", []() {
    if (!ensureUiAuth()) return;
    size_t count = 0;
    const GridVariantInfo* infos = getGridVariantInfos(count);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    GridVariant active = displaySettings.getGridVariant();
    for (size_t i = 0; i < count; ++i) {
      JsonObject o = arr.add<JsonObject>();
      o["id"] = gridVariantToId(infos[i].variant);
      o["key"] = infos[i].key;
      o["label"] = infos[i].label;
      o["language"] = infos[i].language;
      o["version"] = infos[i].version;
      o["active"] = (infos[i].variant == active);
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/setGridVariant", []() {
    if (!ensureUiAuth()) return;
    bool updated = false;

    if (server.hasArg("id")) {
      uint8_t id = static_cast<uint8_t>(server.arg("id").toInt());
      size_t count = 0;
      getGridVariantInfos(count);
      if (id < count) {
        GridVariant variant = gridVariantFromId(id);
        displaySettings.setGridVariant(variant);
        updated = true;
      }
    } else if (server.hasArg("key")) {
      String key = server.arg("key");
      GridVariant variant = gridVariantFromKey(key.c_str());
      const GridVariantInfo* info = getGridVariantInfo(variant);
      if (info && key == info->key) {
        displaySettings.setGridVariant(variant);
        updated = true;
      }
    }

    if (!updated) {
      server.send(400, "text/plain", "Invalid grid variant");
      return;
    }

    if (const GridVariantInfo* info = getGridVariantInfo(displaySettings.getGridVariant())) {
      logInfo(String("🧩 Grid variant updated to ") + info->label + " (" + info->key + ")");
    }

    JsonDocument doc;
    GridVariant variant = displaySettings.getGridVariant();
    doc["id"] = gridVariantToId(variant);
    const GridVariantInfo* info = getGridVariantInfo(variant);
    if (info) {
      doc["key"] = info->key;
      doc["label"] = info->label;
      doc["language"] = info->language;
      doc["version"] = info->version;
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // Fetch log
  server.on("/log", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /log: Auth failed");
      return;
    }
    String logContent = "";
    int i = logIndex;
    int lineCount = 0;
    for (int count = 0; count < LOG_BUFFER_SIZE; count++) {
      String line = logBuffer[i];
      if (line.length() > 0) {
        logContent += line + "\n";
        lineCount++;
      }
      i = (i + 1) % LOG_BUFFER_SIZE;
    }
    server.send(200, "text/plain", logContent);
  });

  server.on("/api/logs", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    logFlushFile();
    struct LogItem {
      String name;
      size_t size;
      String date;
    };
    // Deduplicate by date: keep the largest file per date
    std::vector<LogItem> items;
    std::map<String, LogItem, std::greater<String>> bestByDate;
    File dir = FS_IMPL.open("/logs");
    if (dir) {
      while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
          String name = entry.name();
          size_t size = entry.size();
          String shortName = name;
          if (shortName.startsWith("/")) shortName = shortName.substring(1);
          if (shortName.startsWith("logs/")) shortName = shortName.substring(5);
          String date = shortName;
          int dot = date.lastIndexOf('.');
          if (dot > 0) date = date.substring(0, dot);
          LogItem item;
          item.name = shortName.length() ? shortName : name;
          item.size = size;
          item.date = date;
          auto it = bestByDate.find(date);
          if (it == bestByDate.end() || size > it->second.size) {
            bestByDate[date] = item;
          }
        }
        entry.close();
      }
      dir.close();
    }
    for (const auto& kv : bestByDate) {
      items.push_back(kv.second);
    }
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& item : items) {
      JsonObject o = arr.add<JsonObject>();
      o["name"] = item.name;
      o["size"] = item.size;
      o["date"] = item.date;
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // Logs summary
  server.on("/api/logs/summary", HTTP_GET, []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /api/logs/summary: Auth failed");
      return;
    }
    logFlushFile();
    // Deduplicate per date: keep the largest file for each date to match the list view
    size_t total = 0;
    size_t count = 0;
    std::map<String, size_t, std::greater<String>> bestByDate;
    File dir = FS_IMPL.open("/logs");
    if (dir) {
      while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
          String shortName = entry.name();
          if (shortName.startsWith("/")) shortName = shortName.substring(1);
          if (shortName.startsWith("logs/")) shortName = shortName.substring(5);
          String date = shortName;
          int dot = date.lastIndexOf('.');
          if (dot > 0) date = date.substring(0, dot);
          size_t sz = entry.size();
          auto it = bestByDate.find(date);
          if (it == bestByDate.end() || sz > it->second) {
            bestByDate[date] = sz;
          }
        }
        entry.close();
      }
      dir.close();
    } else {
      logWarn("[API] /api/logs/summary: Failed to open /logs directory");
    }
    for (const auto& kv : bestByDate) {
      total += kv.second;
      count++;
    }
    JsonDocument doc;
    doc["total_bytes"] = (uint32_t)total;
    doc["count"] = (uint32_t)count;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/logs/settings", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    JsonDocument doc;
    doc["retention_days"] = getLogRetentionDays();
    doc["delete_on_boot"] = getLogDeleteOnBoot();
    doc["level"] = (uint8_t)LOG_LEVEL;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/logs/settings", HTTP_POST, []() {
    if (!ensureUiAuth()) return;
    if (server.hasArg("retention_days")) {
      setLogRetentionDays(server.arg("retention_days").toInt());
    }
    if (server.hasArg("delete_on_boot")) {
      setLogDeleteOnBoot(server.arg("delete_on_boot") == "true" || server.arg("delete_on_boot") == "1");
    }
    if (server.hasArg("level")) {
      setLogLevel((LogLevel)server.arg("level").toInt());
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/buildinfo", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    JsonDocument doc;
    doc["firmware"] = FIRMWARE_VERSION;
    doc["ui"] = getUiVersion();
    doc["git_sha"] = BUILD_GIT_SHA;
    doc["git_branch"] = BUILD_GIT_BRANCH;
    doc["build_time_utc"] = BUILD_TIME_UTC;
    doc["environment"] = BUILD_ENV_NAME;
    doc["ui_sync_supported"] = (SUPPORT_OTA_V2 == 0);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/device/info", HTTP_GET, []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /api/device/info: Auth failed");
      return;
    }
    unsigned long upMs = millis();
    unsigned long upSec = upMs / 1000UL;
    unsigned long days = upSec / 86400UL;
    upSec %= 86400UL;
    unsigned long hours = upSec / 3600UL;
    upSec %= 3600UL;
    unsigned long mins = upSec / 60UL;
    unsigned long secs = upSec % 60UL;
    char upBuf[32];
    snprintf(upBuf, sizeof(upBuf), "%lud %02lu:%02lu:%02lu", days, hours, mins, secs);

    JsonDocument doc;
    doc["uptime_ms"] = millis();
    doc["uptime_human"] = upBuf;
    doc["heap_free"] = ESP.getFreeHeap();
    doc["heap_min_free"] = ESP.getMinFreeHeap();
    doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    doc["chip_model"] = ESP.getChipModel();
    doc["chip_rev"] = ESP.getChipRevision();
    doc["sdk"] = ESP.getSdkVersion();
    doc["rssi"] = WiFi.RSSI();
#if defined(ARDUINO_ARCH_ESP32)
    doc["temp_c"] = temperatureRead();
#endif
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/update/status", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    JsonDocument doc;
    doc["running"] = g_otaTaskRunning;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/log/download", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    logFlushFile();
    String path;
    if (server.hasArg("date")) {
      String date = server.arg("date");
      bool valid = (date.length() == 10 &&
                    isdigit(date[0]) && isdigit(date[1]) && isdigit(date[2]) && isdigit(date[3]) &&
                    date[4] == '-' &&
                    isdigit(date[5]) && isdigit(date[6]) &&
                    date[7] == '-' &&
                    isdigit(date[8]) && isdigit(date[9]));
      if (!valid) {
        server.send(400, "text/plain", "Invalid date format");
        return;
      }
      path = String("/logs/") + date + ".log";
    } else {
      path = logLatestFilePath();
      if (path.length() == 0) {
        server.send(404, "text/plain", "No log files available");
        return;
      }
      if (!path.startsWith("/")) {
        path = "/" + path;
      }
      if (!path.startsWith("/logs/")) {
        path = String("/logs/") + path;
      }
    }
    File f = FS_IMPL.open(path, "r");
    if (!f) {
      server.send(404, "text/plain", "Log file not found");
      return;
    }
    String filename = path.substring(path.lastIndexOf('/') + 1);
    server.sendHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
    server.streamFile(f, "text/plain");
    f.close();
  });

  // Get status
  server.on("/status", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /status: Auth failed");
      return;
    }
    String status = clockEnabled ? "on" : "off";
    server.send(200, "text/plain", status);
  });

  // Turn on/off
  server.on("/toggle", []() {
    if (!ensureUiAuth()) return;
    String state = server.arg("state");
    clockEnabled = (state == "on");
    // Apply immediately
    if (clockEnabled) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        auto indices = get_led_indices_for_time(&timeinfo);
        showLeds(indices);
      }
    } else {
      // Clear LEDs when turning off
      showLeds({});
    }
    server.send(200, "text/plain", "OK");
  });
  
  // Device restart
  server.on("/restart", []() {
    if (!ensureUiAuth()) return;
    logInfo("⚠️ Restart requested via dashboard");
    if (getLogDeleteOnBoot()) {
      // Clear all log files before restart (if enabled in settings)
      logInfo("🗑️ Clearing all log files before restart");
      clearAllLogFiles();
      // Give filesystem time to complete the deletion operations
      delay(500);
    } else {
      logInfo("🧾 Log delete-on-boot disabled; preserving logs on restart");
    }
    server.send(200, "text/html", R"rawliteral(
      <html>
        <head>
          <meta http-equiv='refresh' content='5;url=/' />
        </head>
        <body>
          <h1>Wordclock is restarting...</h1>
          <p>Device will restart shortly. You will be redirected to the dashboard in 5 seconds.</p>
        </body>
      </html>
    )rawliteral");
    delay(100);  // Small delay to finish the HTTP response
    safeRestart();
  });

  server.on("/resetwifi", []() {
    if (!ensureUiAuth()) return;
  logInfo("⚠️ WiFi reset requested via dashboard");
    server.send(200, "text/html", R"rawliteral(
      <html>
        <head>
          <meta http-equiv='refresh' content='10;url=/' />
        </head>
        <body>
          <h1>Resetting WiFi...</h1>
          <p>WiFi settings will be cleared. You may need to reconnect to the 'Wordclock' access point.</p>
        </body>
      </html>
    )rawliteral");
    delay(100);  // Small delay to finish the HTTP response
    resetWiFiSettings();
  });
  
  server.on("/setColor", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("color")) {
      server.send(400, "text/plain", "Missing color");
      return;
    }
    String hex = server.arg("color");  // "RRGGBB"
    String filtered;
    filtered.reserve(hex.length());
    for (size_t i = 0; i < hex.length(); ++i) {
      char c = hex.charAt(i);
      if (isxdigit(static_cast<unsigned char>(c))) {
        filtered += static_cast<char>(toupper(static_cast<unsigned char>(c)));
      }
    }
    if (filtered.length() != 6) {
      server.send(400, "text/plain", "Invalid color");
      return;
    }

    long val = strtol(filtered.c_str(), nullptr, 16);
    uint8_t r = (val >> 16) & 0xFF;
    uint8_t g = (val >> 8) & 0xFF;
    uint8_t b =  val       & 0xFF;
  
    ledState.setRGB(r, g, b);
  
    // Refresh display immediately with new color
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      std::vector<uint16_t> indices = get_led_indices_for_time(&timeinfo);
      showLeds(indices);
    }
  
    server.send(200, "text/plain", "OK");
  });

  // Get current color as RRGGBB (white maps to FFFFFF)
  server.on("/getColor", HTTP_GET, []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /getColor: Auth failed");
      return;
    }
    uint8_t r, g, b, w;
    ledState.getRGBW(r, g, b, w);
    if (w > 0) { r = g = b = 255; }
    char buf[7];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", r, g, b);
    server.send(200, "text/plain", String(buf));
  });
  
  
  server.on("/startSequence", []() {
    if (!ensureUiAuth()) return;
  logInfo("✨ Startup sequence started via dashboard");
    extern StartupSequence startupSequence;
    startupSequence.start();
    server.send(200, "text/plain", "Startup sequence executed");
  });
  
  server.on(
    "/uploadFirmware",
    HTTP_POST,
    []() {
      if (!ensureUiAuth()) return;
      server.send(200, "text/plain", Update.hasError() ? "Firmware update failed" : "Firmware update successful. Rebooting...");
      if (!Update.hasError()) {
        delay(1000);
        safeRestart();
      }
    },
    []() {
      if (!ensureUiAuth()) return;
      HTTPUpload& upload = server.upload();

      if (upload.status == UPLOAD_FILE_START) {
        logInfo("📂 Upload started: " + upload.filename);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          logError("❌ Update.begin() failed");
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
          logError("❌ Error writing chunk");
          Update.printError(Serial);
        } else {
          logDebug("✏️ Written: " + String(written) + " bytes");
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        logInfo("📥 Upload completed");
        logDebug("Total " + String(Update.size()) + " bytes");
        if (!Update.end(true)) {
          logError("❌ Update.end() failed");
          Update.printError(Serial);
        }
      }
    }
  );  

  // Separate endpoint for filesystem (UI) updates
  server.on(
    "/uploadFs",
    HTTP_POST,
    []() {
      if (!ensureUiAuth()) return;
      server.send(200, "text/plain", Update.hasError() ? "Filesystem update failed" : "Filesystem update successful. Rebooting...");
      if (!Update.hasError()) {
        delay(1000);
        safeRestart();
      }
    },
    []() {
      if (!ensureUiAuth()) return;
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        logInfo("📂 Filesystem upload started: " + upload.filename);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
          logError("❌ Update.begin(U_SPIFFS) failed");
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
          logError("❌ Error writing chunk (filesystem)");
          Update.printError(Serial);
        } else {
          logDebug("✏️ Filesystem written: " + String(written) + " bytes");
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        logInfo("📥 Filesystem upload completed");
        logDebug("Filesystem total " + String(Update.size()) + " bytes");
        if (!Update.end(true)) {
          logError("❌ Update.end(U_SPIFFS) failed");
          Update.printError(Serial);
        }
      }
    }
  );

  server.on(
    "/uploadSpiffs",
    HTTP_POST,
    []() {
      if (!ensureUiAuth()) return;
      server.send(200, "text/plain", Update.hasError() ? "Filesystem update failed" : "Filesystem update successful. Rebooting...");
      if (!Update.hasError()) {
        delay(1000);
        safeRestart();
      }
    },
    []() {
      if (!ensureUiAuth()) return;
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        logInfo("📂 Filesystem upload started: " + upload.filename);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
          logError("❌ Update.begin(U_SPIFFS) failed");
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
          logError("❌ Error writing chunk (filesystem)");
          Update.printError(Serial);
        } else {
          logDebug("✏️ Filesystem written: " + String(written) + " bytes");
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        logInfo("📥 Filesystem upload completed");
        logDebug("Filesystem total " + String(Update.size()) + " bytes");
        if (!Update.end(true)) {
          logError("❌ Update.end(U_SPIFFS) failed");
          Update.printError(Serial);
        }
      }
    }
  );

  server.on("/checkForUpdate", HTTP_ANY, []() {
    if (!ensureUiAuth()) return;
    if (g_otaTaskRunning) {
      server.send(409, "text/plain", "Firmware update already running");
      return;
    }
    g_otaTaskRunning = true;
    logInfo("Firmware update manually started via UI (background task)");
    BaseType_t ok = xTaskCreatePinnedToCore(
      otaUpdateTask,
      "otaUpdate",
      12288,
      nullptr,
      1,
      &g_otaTaskHandle,
      tskNO_AFFINITY
    );
    if (ok != pdPASS) {
      g_otaTaskRunning = false;
      g_otaTaskHandle = nullptr;
      logError("Failed to start OTA update task");
      server.send(500, "text/plain", "Failed to start firmware update");
      return;
    }
    server.send(200, "text/plain", "Firmware update started");
  });

#if SUPPORT_OTA_V2 == 0
  server.on("/syncUI", HTTP_POST, []() {
    if (!ensureAdminAuth()) return;
    logInfo("🗂️ UI sync requested by admin");
    syncFilesFromManifest();
    server.send(200, "text/plain", "UI sync started");
  });
#endif

  server.on("/getBrightness", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /getBrightness: Auth failed");
      return;
    }
    uint8_t brightness = ledState.getBrightness();
    server.send(200, "text/plain", String(brightness));
  });  

  server.on("/setBrightness", []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("level")) {
      server.send(400, "text/plain", "Missing brightness level");
      return;
    }
  
    int level = server.arg("level").toInt();
    level = constrain(level, 0, 255);
    ledState.setBrightness(level);
  
      // Apply to active LEDs
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      auto indices = get_led_indices_for_time(&timeinfo);
      showLeds(indices);  // uses current color + new brightness
    }
  
    server.send(200, "text/plain", "OK");
  });

#if defined(PRODUCT_VARIANT_LOGO)
  server.on("/logo/state", HTTP_GET, []() {
    if (!ensureUiAuth()) return;
    sendLogoState();
  });

  server.on("/logo/state", HTTP_POST, []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("plain")) {
      server.send(400, "text/plain", "Missing body");
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    bool updated = false;
    if (doc["brightness"].is<int>()) {
      int br = doc["brightness"].as<int>();
      br = constrain(br, 0, 255);
      logoLeds.setBrightness(static_cast<uint8_t>(br));
      updated = true;
    }

    if (doc["all"].is<const char*>() || doc["all"].is<String>()) {
      String hex = doc["all"].as<String>();
      uint8_t r = 0, g = 0, b = 0;
      if (!parseHexColor(hex, r, g, b)) {
        server.send(400, "text/plain", "Invalid all-color value");
        return;
      }
      logoLeds.setAll(r, g, b);
      updated = true;
    }

    if (doc["colors"].is<JsonArray>()) {
      JsonArray arr = doc["colors"].as<JsonArray>();
      if (!arr || arr.size() != LOGO_LED_COUNT) {
        server.send(400, "text/plain", String("colors array must contain ") + LOGO_LED_COUNT + " hex strings");
        return;
      }
      for (uint16_t i = 0; i < LOGO_LED_COUNT; ++i) {
        uint8_t r = 0, g = 0, b = 0;
        if (!parseHexColor(arr[i].as<String>(), r, g, b)) {
          server.send(400, "text/plain", "Invalid color entry");
          return;
        }
        logoLeds.setColor(i, r, g, b, false);
      }
      logoLeds.flushColors();
      updated = true;
    }

    if (updated) {
      refreshCurrentTimeDisplay();
    }
    sendLogoState();
  });
#endif

  // Expose firmware version
  server.on("/version", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /version: Auth failed");
      return;
    }
    server.send(200, "text/plain", FIRMWARE_VERSION);
  });

  // UI version from config
  server.on("/uiversion", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /uiversion: Auth failed");
      return;
    }
    server.send(200, "text/plain", getUiVersion());
  });

  // Sell mode endpoints (force 10:47 display)
  server.on("/getSellMode", []() {
    if (!ensureUiAuth()) return;
    server.send(200, "text/plain", displaySettings.isSellMode() ? "on" : "off");
  });
  server.on("/setSellMode", []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("state")) {
      server.send(400, "text/plain", "Missing state");
      return;
    }
    String st = server.arg("state");
    bool on = (st == "on" || st == "1" || st == "true");
    displaySettings.setSellMode(on);
    // Trigger animation to new effective time
    struct tm t = {};
    if (on) {
      t.tm_hour = 10;
      t.tm_min = 47;
    } else {
      if (!getLocalTime(&t)) { server.send(200, "text/plain", "OK"); return; }
    }
    wordclock_force_animation_for_time(&t);
  logInfo(String("🛒 Sell time ") + (on ? "ON (10:47)" : "OFF"));
    server.send(200, "text/plain", "OK");
  });

  // Word-by-word animation toggle
  server.on("/getAnimate", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /getAnimate: Auth failed");
      return;
    }
    bool animate = displaySettings.getAnimateWords();
    String result = animate ? "on" : "off";
    server.send(200, "text/plain", result);
  });
  server.on("/setAnimate", []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("state")) {
      server.send(400, "text/plain", "Missing state");
      return;
    }
    String st = server.arg("state");
    bool on = (st == "on" || st == "1" || st == "true");
    displaySettings.setAnimateWords(on);
  logInfo(String("🎞️ Animation ") + (on ? "ON" : "OFF"));
    server.send(200, "text/plain", "OK");
  });


  // Night mode configuration
  server.on("/getNightModeConfig", HTTP_GET, []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /getNightModeConfig: Auth failed");
      return;
    }
    sendNightModeConfig();
  });

  server.on("/setNightModeConfig", HTTP_POST, []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("plain")) {
      server.send(400, "text/plain", "Missing body");
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    JsonVariant enabledVar = doc["enabled"];
    if (!enabledVar.isNull()) {
      if (enabledVar.is<bool>()) {
        nightMode.setEnabled(enabledVar.as<bool>());
      } else if (enabledVar.is<int>()) {
        nightMode.setEnabled(enabledVar.as<int>() != 0);
      } else if (enabledVar.is<const char*>()) {
        String st = enabledVar.as<const char*>();
        st.toLowerCase();
        nightMode.setEnabled(st == "true" || st == "on" || st == "1");
      }
    }

    JsonVariant effectVar = doc["effect"];
    if (!effectVar.isNull()) {
      String eff = effectVar.as<String>();
      eff.toLowerCase();
      if (eff == "off") {
        nightMode.setEffect(NightModeEffect::Off);
      } else if (eff == "dim") {
        nightMode.setEffect(NightModeEffect::Dim);
      } else {
        server.send(400, "text/plain", "Invalid effect");
        return;
      }
    }

    JsonVariant dimVar = doc["dim_percent"];
    if (!dimVar.isNull()) {
      int pct = dimVar.as<int>();
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      nightMode.setDimPercent((uint8_t)pct);
    }

    uint16_t startMin = nightMode.getStartMinutes();
    uint16_t endMin = nightMode.getEndMinutes();
    bool scheduleUpdate = false;

    JsonVariant startStrVar = doc["start"];
    if (!startStrVar.isNull()) {
      String startStr = startStrVar.as<String>();
      uint16_t parsed = 0;
      if (!NightMode::parseTimeString(startStr, parsed)) {
        server.send(400, "text/plain", "Invalid start time");
        return;
      }
      startMin = parsed;
      scheduleUpdate = true;
    } else {
      JsonVariant startMinVar = doc["start_minutes"];
      if (!startMinVar.isNull()) {
        int parsed = startMinVar.as<int>();
        if (parsed < 0 || parsed >= (24 * 60)) {
          server.send(400, "text/plain", "Invalid start minutes");
          return;
        }
        startMin = (uint16_t)parsed;
        scheduleUpdate = true;
      }
    }

    JsonVariant endStrVar = doc["end"];
    if (!endStrVar.isNull()) {
      String endStr = endStrVar.as<String>();
      uint16_t parsed = 0;
      if (!NightMode::parseTimeString(endStr, parsed)) {
        server.send(400, "text/plain", "Invalid end time");
        return;
      }
      endMin = parsed;
      scheduleUpdate = true;
    } else {
      JsonVariant endMinVar = doc["end_minutes"];
      if (!endMinVar.isNull()) {
        int parsed = endMinVar.as<int>();
        if (parsed < 0 || parsed >= (24 * 60)) {
          server.send(400, "text/plain", "Invalid end minutes");
          return;
        }
        endMin = (uint16_t)parsed;
        scheduleUpdate = true;
      }
    }

    if (scheduleUpdate) {
      nightMode.setSchedule(startMin, endMin);
    }

    JsonVariant overrideVar = doc["override"];
    if (!overrideVar.isNull()) {
      String ov = overrideVar.as<String>();
      ov.toLowerCase();
      if (ov == "auto") {
        nightMode.setOverride(NightModeOverride::Auto);
      } else if (ov == "force_on" || ov == "on") {
        nightMode.setOverride(NightModeOverride::ForceOn);
      } else if (ov == "force_off" || ov == "off") {
        nightMode.setOverride(NightModeOverride::ForceOff);
      } else {
        server.send(400, "text/plain", "Invalid override");
        return;
      }
    }

    sendNightModeConfig();
  });

  // Het Is duration (0..360 seconds; 0=never, 360=always)
  server.on("/getHetIsDuration", []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /getHetIsDuration: Auth failed");
      return;
    }
#if defined(PRODUCT_VARIANT_MINI)
    server.send(404, "text/plain", "HET IS not supported on this product");
    return;
#endif
    uint16_t duration = displaySettings.getHetIsDurationSec();
    server.send(200, "text/plain", String(duration));
  });

  server.on("/setHetIsDuration", []() {
    if (!ensureUiAuth()) return;
#if defined(PRODUCT_VARIANT_MINI)
    server.send(404, "text/plain", "HET IS not supported on this product");
    return;
#endif
    if (!server.hasArg("seconds")) {
      server.send(400, "text/plain", "Missing seconds");
      return;
    }
    int val = server.arg("seconds").toInt();
    if (val < 0) val = 0; if (val > 360) val = 360;
    displaySettings.setHetIsDurationSec((uint16_t)val);
  logInfo("⏱️ HET IS duration set to " + String(val) + "s");
    server.send(200, "text/plain", "OK");
  });

  server.on("/setLogLevel", HTTP_ANY, []() {
    if (!ensureUiAuth()) return;
    if (!server.hasArg("level")) {
      server.send(400, "text/plain", "Missing log level");
      return;
    }

    String levelStr = server.arg("level");
    LogLevel level;

    if (levelStr == "DEBUG") level = LOG_LEVEL_DEBUG;
    else if (levelStr == "INFO") level = LOG_LEVEL_INFO;
    else if (levelStr == "WARN") level = LOG_LEVEL_WARN;
    else if (levelStr == "ERROR") level = LOG_LEVEL_ERROR;
    else {
      server.send(400, "text/plain", "Invalid log level");
      return;
    }


    setLogLevel(level);
  logInfo("🔧 Log level changed to: " + levelStr);
    server.send(200, "text/plain", "OK");
  });

  server.on("/getLogLevel", HTTP_GET, []() {
    if (!ensureUiAuth()) {
      logWarn("[API] /getLogLevel: Auth failed");
      return;
    }
    // Return current level as string
    String s = "INFO";
    extern LogLevel LOG_LEVEL; // declared in log.cpp
    switch (LOG_LEVEL) {
      case LOG_LEVEL_DEBUG: s = "DEBUG"; break;
      case LOG_LEVEL_INFO:  s = "INFO";  break;
      case LOG_LEVEL_WARN:  s = "WARN";  break;
      case LOG_LEVEL_ERROR: s = "ERROR"; break;
    }
    server.send(200, "text/plain", s);
  });

  
}
