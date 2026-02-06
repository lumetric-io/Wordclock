#include "mqtt_client.h"

#include "led_events.h"
#include "mqtt_command_handler.h"
#include "mqtt_discovery_builder.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "display_settings.h"
#include "led_state.h"
#include "log.h"
#if OTA_ENABLED
#include "ota_updater.h"
#include "update_status.h"
#endif
#include "wordclock.h"
#include "time_mapper.h"
#include "sequence_controller.h"
#include "mqtt_settings.h"
#include <esp_system.h>
#include <Preferences.h>
#include "night_mode.h"
#include "system_utils.h"

extern DisplaySettings displaySettings;
extern bool clockEnabled;
extern StartupSequence startupSequence;

static WiFiClient espClient;
static PubSubClient mqtt(espClient);

static String uniqId;
static MqttSettings g_mqttCfg;
static String base;         // e.g., MQTT_BASE_TOPIC
static String availTopic;   // base + "/availability"
static String tBirth;
static bool g_connected = false;
static String g_lastErr;

// Topics
static String tLightState, tLightSet;
static String tClockState, tClockSet;
static String tAnimState, tAnimSet;
static String tAutoUpdState, tAutoUpdSet;
static String tHetIsState, tHetIsSet;
static String tLogLvlState, tLogLvlSet;
static String tRestartCmd, tSeqCmd, tUpdateCmd;
static String tNightEnabledState, tNightEnabledSet;
static String tNightOverrideState, tNightOverrideSet;
static String tNightActiveState;
static String tNightEffectState, tNightEffectSet;
static String tNightDimState, tNightDimSet;
static String tNightStartState, tNightStartSet;
static String tNightEndState, tNightEndSet;
static String tVersion, tUiVersion, tIp, tRssi, tUptime;
static String tHeap, tWifiChan, tBootReason, tResetCount;
static String tUpdateChannelState, tUpdateAutoAllowed, tUpdateAvailable;
static String tUpdateRunning;

static unsigned long lastReconnectAttempt = 0;
static unsigned long lastStateAt = 0;
static const unsigned long STATE_INTERVAL_MS = 30000; // 30s
static const unsigned long RECONNECT_DELAY_MIN_MS = 2000;
static const unsigned long RECONNECT_DELAY_MAX_MS = 60000;
static const unsigned long RECONNECT_PAUSED_RETRY_MS = 300000;
static unsigned long reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
static uint8_t reconnectAttempts = 0;
static bool reconnectAborted = false;
static unsigned long lastPausedRetryMs = 0;

static void buildTopics() {
  base = g_mqttCfg.baseTopic;
  availTopic = base + "/availability";
  tBirth        = base + "/birth";
  tLightState   = base + "/light/state";
  tLightSet     = base + "/light/set";
  tClockState   = base + "/clock/state";
  tClockSet     = base + "/clock/set";
  tAnimState    = base + "/animate/state";
  tAnimSet      = base + "/animate/set";
  tHetIsState   = base + "/hetis/state";
  tHetIsSet     = base + "/hetis/set";
  tNightEnabledState = base + "/nightmode/enabled/state";
  tNightEnabledSet   = base + "/nightmode/enabled/set";
  tNightOverrideState = base + "/nightmode/override/state";
  tNightOverrideSet   = base + "/nightmode/override/set";
  tNightActiveState   = base + "/nightmode/active";
  tNightEffectState = base + "/nightmode/effect/state";
  tNightEffectSet   = base + "/nightmode/effect/set";
  tNightDimState    = base + "/nightmode/dim/state";
  tNightDimSet      = base + "/nightmode/dim/set";
  tNightStartState  = base + "/nightmode/start/state";
  tNightStartSet    = base + "/nightmode/start/set";
  tNightEndState    = base + "/nightmode/end/state";
  tNightEndSet      = base + "/nightmode/end/set";
  tLogLvlState  = base + "/loglevel/state";
  tLogLvlSet    = base + "/loglevel/set";
  tRestartCmd   = base + "/restart/press";
  tSeqCmd       = base + "/sequence/press";
  tVersion      = base + "/version";
  tUiVersion    = base + "/uiversion";
  tIp           = base + "/ip";
  tRssi         = base + "/rssi";
  tUptime       = base + "/laststartup";
  tHeap         = base + "/heap";
  tWifiChan     = base + "/wifi_channel";
  tBootReason   = base + "/boot_reason";
  tResetCount   = base + "/reset_count";
#if OTA_ENABLED
  tAutoUpdState = base + "/autoupdate/state";
  tAutoUpdSet   = base + "/autoupdate/set";
  tUpdateCmd    = base + "/update/press";
  tUpdateChannelState = base + "/update/channel";
  tUpdateAutoAllowed  = base + "/update/auto_allowed";
  tUpdateAvailable    = base + "/update/available";
  tUpdateRunning      = base + "/update/running";
#endif
}

static void publishDiscovery() {
  String nodeId = uniqId;
  
  MqttDiscoveryBuilder builder(mqtt, g_mqttCfg.discoveryPrefix, 
                               nodeId, base, availTopic);
  
  // Set device information
  builder.setDeviceInfo(CLOCK_NAME, "Chronolett Wordclock", "Lumetric", FIRMWARE_VERSION);
  
  // Light entity
  builder.addLight(tLightState, tLightSet);
  
  // Switches
  builder.addSwitch("Animate words", nodeId + "_anim", tAnimState, tAnimSet);
#if OTA_ENABLED
  builder.addSwitch("Auto update", nodeId + "_autoupd", tAutoUpdState, tAutoUpdSet);
#endif
  builder.addSwitch("Night mode enabled", nodeId + "_night_enabled", 
                   tNightEnabledState, tNightEnabledSet);
  
  // Select entities
  builder.addSelect("Night mode effect", nodeId + "_night_effect",
                   tNightEffectState, tNightEffectSet,
                   {"DIM", "OFF"});
  builder.addSelect("Night mode override", nodeId + "_night_override",
                   tNightOverrideState, tNightOverrideSet,
                   {"AUTO", "ON", "OFF"});
  builder.addSelect("Log level", nodeId + "_loglevel",
                   tLogLvlState, tLogLvlSet,
                   {"DEBUG", "INFO", "WARN", "ERROR"});
  
  // Number entities
  builder.addNumber("Night mode dim %", nodeId + "_night_dim",
                   tNightDimState, tNightDimSet,
                   0, 100, 1, "%");
#if !defined(PRODUCT_VARIANT_MINI)
  builder.addNumber("'HET IS' seconds", nodeId + "_hetis",
                   tHetIsState, tHetIsSet,
                   0, 360, 1, "s");
#endif
  
  // Binary sensor
  builder.addBinarySensor("Night mode active", nodeId + "_night_active",
                         tNightActiveState);
#if OTA_ENABLED
  builder.addBinarySensor("Update running", nodeId + "_update_running",
                         tUpdateRunning);
#endif
  
  // Buttons
  builder.addButton("Restart", nodeId + "_restart", tRestartCmd, "restart");
  builder.addButton("Start sequence", nodeId + "_sequence", tSeqCmd);
#if OTA_ENABLED
  builder.addButton("Check for update", nodeId + "_update", tUpdateCmd, "update");
#endif
  
  // Sensors
  builder.addSensor("Firmware Version", nodeId + "_version", tVersion);
  builder.addSensor("UI Version", nodeId + "_uiversion", tUiVersion);
  builder.addSensor("IP Address", nodeId + "_ip", tIp);
  builder.addSensor("WiFi RSSI", nodeId + "_rssi", tRssi, "dBm", "signal_strength");
  builder.addSensor("Last Startup", nodeId + "_uptime", tUptime, "", "timestamp");
  builder.addSensor("Free Heap (bytes)", nodeId + "_heap", tHeap, "bytes");
  builder.addSensor("WiFi Channel", nodeId + "_wifichan", tWifiChan);
  builder.addSensor("Boot Reason", nodeId + "_bootreason", tBootReason);
  builder.addSensor("Reset Count", nodeId + "_resetcount", tResetCount);
  
  // Text entities (time inputs)
  builder.addText("Night mode start", nodeId + "_night_start",
                 tNightStartState, tNightStartSet,
                 5, 5, "^([01][0-9]|2[0-3]):[0-5][0-9]$");
  builder.addText("Night mode end", nodeId + "_night_end",
                 tNightEndState, tNightEndSet,
                 5, 5, "^([01][0-9]|2[0-3]):[0-5][0-9]$");
  
  // Publish all entities
  builder.publish();
}

static void publishAvailability(const char* st) {
  mqtt.publish(availTopic.c_str(), st, true);
}

// Publisher functions (now non-static so they can be accessed by command handlers)
void publishLightState() {
  JsonDocument doc;
  uint8_t r, g, b, w; ledState.getRGBW(r,g,b,w);
  doc["state"] = clockEnabled ? "ON" : "OFF";
  doc["brightness"] = ledState.getBrightness();
  doc["color_mode"] = "rgbw";
  JsonObject col = doc["color"].to<JsonObject>();
  col["r"] = r; col["g"] = g; col["b"] = b; col["w"] = w;
  String out; serializeJson(doc, out);
  mqtt.publish(tLightState.c_str(), out.c_str(), true);
}

void publishSwitch(const String& topic, bool on) {
  mqtt.publish(topic.c_str(), on ? "ON" : "OFF", true);
}

void publishNumber(const String& topic, int v) {
  char buf[16]; snprintf(buf, sizeof(buf), "%d", v);
  mqtt.publish(topic.c_str(), buf, true);
}

void publishSelect(const String& topic) {
  extern LogLevel LOG_LEVEL;
  const char* s = "INFO";
  switch (LOG_LEVEL) {
    case LOG_LEVEL_DEBUG: s = "DEBUG"; break;
    case LOG_LEVEL_INFO:  s = "INFO";  break;
    case LOG_LEVEL_WARN:  s = "WARN";  break;
    case LOG_LEVEL_ERROR: s = "ERROR"; break;
  }
  mqtt.publish(topic.c_str(), s, true);
}

void publishNightOverrideState() {
  const char* s = "AUTO";
  switch (nightMode.getOverride()) {
    case NightModeOverride::ForceOn:  s = "ON"; break;
    case NightModeOverride::ForceOff: s = "OFF"; break;
    case NightModeOverride::Auto:
    default:                         s = "AUTO"; break;
  }
  mqtt.publish(tNightOverrideState.c_str(), s, true);
}

void publishNightActiveState() {
  mqtt.publish(tNightActiveState.c_str(), nightMode.isActive() ? "ON" : "OFF", true);
}

void publishNightEffectState() {
  const char* s = (nightMode.getEffect() == NightModeEffect::Off) ? "OFF" : "DIM";
  mqtt.publish(tNightEffectState.c_str(), s, true);
}


void publishNightDimState() {
  publishNumber(tNightDimState, nightMode.getDimPercent());
}

void publishNightScheduleState() {
  String start = nightMode.formatMinutes(nightMode.getStartMinutes());
  String end = nightMode.formatMinutes(nightMode.getEndMinutes());
  mqtt.publish(tNightStartState.c_str(), start.c_str(), true);
  mqtt.publish(tNightEndState.c_str(), end.c_str(), true);
}

// Cache computed boot time string once NTP is synced
static String g_bootTimeStr;
static bool g_bootTimeSet = false;
static String g_bootReasonStr;
static uint32_t g_resetCount = 0;
static bool mqttConfiguredLogged = false;

static bool mqtt_has_configuration() {
  if (g_mqttCfg.port == 0) return false;
  String host = g_mqttCfg.host;
  host.trim();
  return host.length() > 0;
}

static const char* reset_reason_to_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXTERNAL";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

void mqtt_publish_state(bool force) {
  unsigned long now = millis();
  if (!force && (now - lastStateAt) < STATE_INTERVAL_MS) return;
  lastStateAt = now;
  if (!mqtt.connected()) return;

  publishLightState();
  publishSwitch(tAnimState, displaySettings.getAnimateWords());
#if OTA_ENABLED
  publishSwitch(tAutoUpdState, displaySettings.getAutoUpdate());
#endif
#if !defined(PRODUCT_VARIANT_MINI)
  publishNumber(tHetIsState, displaySettings.getHetIsDurationSec());
#endif
  publishSwitch(tNightEnabledState, nightMode.isEnabled());
  publishNightEffectState();
  publishNightDimState();
  publishNightScheduleState();
  publishNightOverrideState();
  publishNightActiveState();
  publishSelect(tLogLvlState);

#if OTA_ENABLED
  // Update channel / auto-update status
  String updCh = displaySettings.getUpdateChannel();
  mqtt.publish(tUpdateChannelState.c_str(), updCh.c_str(), true);
  bool autoAllowed = displaySettings.getAutoUpdate() && updCh != "develop";
  mqtt.publish(tUpdateAutoAllowed.c_str(), autoAllowed ? "ON" : "OFF", true);
  mqtt.publish(tUpdateAvailable.c_str(), "unknown", true); // placeholder until a remote check runs
  mqtt.publish(tUpdateRunning.c_str(), is_update_running() ? "ON" : "OFF", true);
#endif

  mqtt.publish(tVersion.c_str(), FIRMWARE_VERSION, true);
#if OTA_ENABLED
  String uiVersion = getUiVersion();
  mqtt.publish(tUiVersion.c_str(), uiVersion.c_str(), true);
#else
  mqtt.publish(tUiVersion.c_str(), UI_VERSION, true);
#endif
  mqtt.publish(tIp.c_str(), WiFi.localIP().toString().c_str(), true);
  char rssi[16]; snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI()); mqtt.publish(tRssi.c_str(), rssi, true);
  char heap[24]; snprintf(heap, sizeof(heap), "%u", (unsigned)esp_get_free_heap_size()); mqtt.publish(tHeap.c_str(), heap, true);
  char ch[8]; snprintf(ch, sizeof(ch), "%d", WiFi.channel()); mqtt.publish(tWifiChan.c_str(), ch, true);
  if (g_bootReasonStr.length() == 0) {
    g_bootReasonStr = reset_reason_to_str(esp_reset_reason());
  }
  mqtt.publish(tBootReason.c_str(), g_bootReasonStr.c_str(), true);
  char rc[16]; snprintf(rc, sizeof(rc), "%lu", (unsigned long)g_resetCount); mqtt.publish(tResetCount.c_str(), rc, true);

  // Publish last startup timestamp (local time) once NTP is synced
  time_t nowEpoch = time(nullptr);
  if (!g_bootTimeSet && nowEpoch >= 1640995200) { // 2022-01-01 as "time is valid" threshold
    time_t boot = nowEpoch - (time_t)(millis() / 1000UL);
    struct tm lt = {};
    localtime_r(&boot, &lt);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &lt);
    g_bootTimeStr = String(buf);
    g_bootTimeSet = true;
  }
  const char* bootOut = g_bootTimeSet ? g_bootTimeStr.c_str() : "unknown";
  mqtt.publish(tUptime.c_str(), bootOut, true);
}

void mqtt_publish_update_status(bool running) {
#if OTA_ENABLED
  if (!mqtt.connected()) return;
  mqtt.publish(tUpdateRunning.c_str(), running ? "ON" : "OFF", true);
#else
  (void)running;
#endif
}

static void publishBirth() {
  // Publish a small JSON birth message with time and reason
  if (!mqtt.connected()) return;
  String out = String("{\"time\":\"") + (g_bootTimeSet ? g_bootTimeStr : String("unknown")) +
               "\",\"reason\":\"" + (g_bootReasonStr.length() ? g_bootReasonStr : String(reset_reason_to_str(esp_reset_reason()))) + "\"}";
  mqtt.publish(tBirth.c_str(), out.c_str(), true);
}

/**
 * @brief Initialize MQTT command handlers
 * 
 * Registers all command handlers with the command registry.
 * This replaces the large if-else chain that was in handleMessage().
 */
static void initCommandHandlers() {
  auto& registry = MqttCommandRegistry::instance();
  
  // Light (complex JSON)
  registry.registerHandler(tLightSet, new LightCommandHandler());
  
  // Simple switches
  registry.registerHandler(tClockSet, new SwitchCommandHandler(
    "clock",
    [](bool on) { clockEnabled = on; },
    []() { publishSwitch(tClockState, clockEnabled); }
  ));
  
  registry.registerHandler(tAnimSet, new SwitchCommandHandler(
    "animate",
    [](bool on) { displaySettings.setAnimateWords(on); },
    []() { publishSwitch(tAnimState, displaySettings.getAnimateWords()); }
  ));
  
  
#if OTA_ENABLED
  registry.registerHandler(tAutoUpdSet, new SwitchCommandHandler(
    "auto_update",
    [](bool on) { displaySettings.setAutoUpdate(on); },
    []() { publishSwitch(tAutoUpdState, displaySettings.getAutoUpdate()); }
  ));
#endif
  
  registry.registerHandler(tNightEnabledSet, new SwitchCommandHandler(
    "night_enabled",
    [](bool on) { nightMode.setEnabled(on); },
    []() { publishSwitch(tNightEnabledState, nightMode.isEnabled()); }
  ));
  
  // Number handlers
#if !defined(PRODUCT_VARIANT_MINI)
  registry.registerHandler(tHetIsSet, new NumberCommandHandler(
    0, 360,
    [](int v) { displaySettings.setHetIsDurationSec((uint16_t)v); },
    []() { publishNumber(tHetIsState, displaySettings.getHetIsDurationSec()); }
  ));
#endif
  
  registry.registerHandler(tNightDimSet, new NumberCommandHandler(
    0, 100,
    [](int v) { nightMode.setDimPercent((uint8_t)v); },
    []() { publishNightDimState(); }
  ));
  
  // Select handlers
  registry.registerHandler(tNightOverrideSet, new SelectCommandHandler(
    {"AUTO", "ON", "OFF"},
    [](const String& val) {
      if (val == "AUTO") nightMode.setOverride(NightModeOverride::Auto);
      else if (val == "ON") nightMode.setOverride(NightModeOverride::ForceOn);
      else if (val == "OFF") nightMode.setOverride(NightModeOverride::ForceOff);
    },
    []() { publishNightOverrideState(); publishNightActiveState(); }
  ));
  
  registry.registerHandler(tNightEffectSet, new SelectCommandHandler(
    {"DIM", "OFF"},
    [](const String& val) {
      if (val == "DIM") nightMode.setEffect(NightModeEffect::Dim);
      else if (val == "OFF") nightMode.setEffect(NightModeEffect::Off);
    },
    []() { publishNightEffectState(); }
  ));
  
  registry.registerHandler(tLogLvlSet, new SelectCommandHandler(
    {"DEBUG", "INFO", "WARN", "ERROR"},
    [](const String& val) {
      LogLevel level = LOG_LEVEL_INFO;
      if (val == "DEBUG") level = LOG_LEVEL_DEBUG;
      else if (val == "INFO") level = LOG_LEVEL_INFO;
      else if (val == "WARN") level = LOG_LEVEL_WARN;
      else if (val == "ERROR") level = LOG_LEVEL_ERROR;
      setLogLevel(level);
    },
    []() { publishSelect(tLogLvlState); }
  ));
  
  // Time string handlers
  registry.registerHandler(tNightStartSet, new TimeStringCommandHandler(
    NightMode::parseTimeString,
    [](uint16_t minutes) { nightMode.setSchedule(minutes, nightMode.getEndMinutes()); },
    []() { publishNightScheduleState(); },
    "night_start"
  ));
  
  registry.registerHandler(tNightEndSet, new TimeStringCommandHandler(
    NightMode::parseTimeString,
    [](uint16_t minutes) { nightMode.setSchedule(nightMode.getStartMinutes(), minutes); },
    []() { publishNightScheduleState(); },
    "night_end"
  ));
  
  // Simple button commands (no response needed)
  registry.registerLambda(tRestartCmd, [](const String&) {
    safeRestart();
  });
  
  registry.registerLambda(tSeqCmd, [](const String&) {
    extern StartupSequence startupSequence;
    startupSequence.start();
  });
  
#if OTA_ENABLED
  registry.registerLambda(tUpdateCmd, [](const String&) {
    set_update_running(true);
    mqtt_publish_update_status(true);
    checkForFirmwareUpdate();
    set_update_running(false);
    mqtt_publish_update_status(false);
  });
#endif
}

/**
 * @brief Handle incoming MQTT message
 * 
 * Simplified handler that delegates to the command registry.
 * Replaced the 107-line if-else chain with this clean implementation.
 */
static void handleMessage(char* topic, byte* payload, unsigned int length) {
  String topicStr(topic);
  String payloadStr;
  for (unsigned int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }
  
  MqttCommandRegistry::instance().handleMessage(topicStr, payloadStr);
}

static bool mqtt_connect() {
  if (mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) {
    g_lastErr = "WiFi not connected";
    return false;
  }
  if (!mqtt_has_configuration()) {
    g_lastErr = "MQTT not configured";
    return false; // not configured yet
  }

  // Compute unique id based on MAC
  if (uniqId.isEmpty()) {
    uint8_t mac[6]; WiFi.macAddress(mac);
    char buf[13]; snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    uniqId = String("wordclock_") + buf;
    buildTopics();
  }

  String clientId = uniqId;
  bool ok;
  if (g_mqttCfg.user.length() > 0) {
    ok = mqtt.connect(clientId.c_str(), g_mqttCfg.user.c_str(), g_mqttCfg.pass.c_str(), availTopic.c_str(), 1, true, "offline");
  } else {
    ok = mqtt.connect(clientId.c_str());
  }
  if (!ok) {
    int st = mqtt.state();
    g_connected = false;
    g_lastErr = String("connect failed (state ") + st + ")";
    return false;
  }

  publishAvailability("online");
  publishBirth();
  publishDiscovery();
  
  // Initialize command handlers (register all topics)
  initCommandHandlers();

  // Subscriptions
  mqtt.subscribe(tLightSet.c_str());
  mqtt.subscribe(tClockSet.c_str());
  mqtt.subscribe(tAnimSet.c_str());
#if OTA_ENABLED
  mqtt.subscribe(tAutoUpdSet.c_str());
#endif
#if !defined(PRODUCT_VARIANT_MINI)
  mqtt.subscribe(tHetIsSet.c_str());
#endif
  mqtt.subscribe(tNightEnabledSet.c_str());
  mqtt.subscribe(tNightOverrideSet.c_str());
  mqtt.subscribe(tNightEffectSet.c_str());
  mqtt.subscribe(tNightDimSet.c_str());
  mqtt.subscribe(tNightStartSet.c_str());
  mqtt.subscribe(tNightEndSet.c_str());
  mqtt.subscribe(tLogLvlSet.c_str());
  mqtt.subscribe(tRestartCmd.c_str());
  mqtt.subscribe(tSeqCmd.c_str());
#if OTA_ENABLED
  mqtt.subscribe(tUpdateCmd.c_str());
#endif

  mqtt_publish_state(true);
  g_connected = true;
  ledEventStop(LedEvent::MqttDisconnected);
  
  // Reset reconnection state on success
  reconnectAttempts = 0;
  reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
  reconnectAborted = false;
  
  // Log successful recovery if there was a previous error
  if (g_lastErr.length() > 0) {
    logInfo(String("✅ MQTT reconnected successfully after error: ") + g_lastErr);
  }
  g_lastErr = "";
  
  return true;
}

void mqtt_begin() {
  // Load saved settings or fall back to compile-time defaults
  mqtt_settings_load(g_mqttCfg);
  mqtt.setServer(g_mqttCfg.host.c_str(), g_mqttCfg.port);
  mqtt.setCallback(handleMessage);
  reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
  reconnectAttempts = 0;
  reconnectAborted = false;
  lastReconnectAttempt = 0;
  // Bump reset counter (persisted), and cache boot reason string
  g_bootReasonStr = reset_reason_to_str(esp_reset_reason());
  Preferences p;
  if (p.begin("sys", false)) {
    uint32_t cnt = p.getULong("resets", 0);
    cnt += 1;
    p.putULong("resets", cnt);
    p.end();
    g_resetCount = cnt;
  }

  if (!mqtt_has_configuration() && !mqttConfiguredLogged) {
    logInfo("MQTT disabled (no broker configured)");
    mqttConfiguredLogged = true;
  } else {
    mqttConfiguredLogged = false;
  }
}

void mqtt_loop() {
  if (!mqtt_has_configuration()) {
    g_connected = false;
    ledEventStop(LedEvent::MqttDisconnected);
    if (!mqttConfiguredLogged) {
      logInfo("MQTT disabled (no broker configured)");
      mqttConfiguredLogged = true;
    }
    return;
  }
  mqttConfiguredLogged = false;
  if (reconnectAborted) {
    unsigned long now = millis();
    if (lastPausedRetryMs == 0 || now - lastPausedRetryMs >= RECONNECT_PAUSED_RETRY_MS) {
      logInfo("🔄 MQTT retry after paused backoff");
      reconnectAborted = false;
      reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
      reconnectAttempts = 0;
      lastReconnectAttempt = 0;
      lastPausedRetryMs = now;
    } else {
      return;
    }
  }

  if (!mqtt.connected()) {
    ledEventStart(LedEvent::MqttDisconnected);
    unsigned long now = millis();
    if (now - lastReconnectAttempt >= reconnectDelayMs) {
      lastReconnectAttempt = now;
      if (!mqtt_connect()) {
        g_connected = false;
        if (reconnectDelayMs < RECONNECT_DELAY_MIN_MS) reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
        if (reconnectAttempts < 255) reconnectAttempts++;
        unsigned long nextDelay = reconnectDelayMs * 2;
        if (nextDelay > RECONNECT_DELAY_MAX_MS) nextDelay = RECONNECT_DELAY_MAX_MS;
        uint32_t jitter = esp_random() % RECONNECT_DELAY_MIN_MS;
        unsigned long jittered = nextDelay + jitter;
        if (jittered > RECONNECT_DELAY_MAX_MS) jittered = RECONNECT_DELAY_MAX_MS;
        reconnectDelayMs = jittered;
        if (!reconnectAborted && reconnectDelayMs >= RECONNECT_DELAY_MAX_MS) {
          String errMsg = String("⏸️ MQTT reconnect paused after reaching max backoff (") +
                          RECONNECT_DELAY_MAX_MS + " ms); last error: " +
                          (g_lastErr.length() ? g_lastErr : String("unknown")) +
                          String(". Will retry on network recovery, config change, or manual reconnect.");
          logWarn(errMsg);
          reconnectAborted = true;
          lastPausedRetryMs = millis();
          // Note: reconnectAborted will be cleared on:
          // 1. Successful connection (mqtt_connect success path)
          // 2. Configuration change (mqtt_apply_settings)
          // 3. Manual reconnect (mqtt_force_reconnect)
          return;
        } else if (g_lastErr != "MQTT not configured") {
          String warnMsg = String("MQTT reconnect failed (") + (g_lastErr.length() ? g_lastErr : String("unknown")) +
                           "); retry in " + reconnectDelayMs + " ms";
          logWarn(warnMsg);
        }
      }
    }
    return;
  }
  ledEventStop(LedEvent::MqttDisconnected);
  mqtt.loop();
  mqtt_publish_state(false);
}

bool mqtt_is_connected() {
  return g_connected && mqtt.connected();
}

const String& mqtt_last_error() {
  return g_lastErr;
}

void mqtt_apply_settings(const MqttSettings& s) {
  // Persist, then apply live
  MqttSettings toSave = s;
  if (!mqtt_settings_save(toSave)) {
    logError("❌ Failed to save MQTT settings");
    return;
  }
  // Update current config
  g_mqttCfg = toSave;

  // Disconnect and reconfigure server and topics
  if (mqtt.connected()) mqtt.disconnect();
  mqtt.setServer(g_mqttCfg.host.c_str(), g_mqttCfg.port);

  // Recompute topics based on new base/discovery
  buildTopics();
  
  // Reset reconnection state and enable reconnection attempts
  if (reconnectAborted) {
    logInfo("🔄 MQTT reconnection re-enabled after configuration change");
  }
  reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
  reconnectAttempts = 0;
  reconnectAborted = false;
  lastReconnectAttempt = 0; // trigger immediate reconnect in loop

  if (!mqtt_has_configuration()) {
    if (!mqttConfiguredLogged) {
      logInfo("MQTT disabled (no broker configured)");
      mqttConfiguredLogged = true;
    }
  } else {
    mqttConfiguredLogged = false;
  }
}

/**
 * @brief Force MQTT reconnection attempt, clearing abort state
 * 
 * Useful for manual reconnection via web UI or MQTT command after
 * prolonged network outages that triggered reconnection abort.
 */
void mqtt_force_reconnect() {
  if (mqtt.connected()) {
    logInfo("MQTT already connected");
    return;
  }
  
  if (reconnectAborted) {
    logInfo("🔄 MQTT reconnection re-enabled by force reconnect");
  }
  
  // Reset all reconnection state
  reconnectAborted = false;
  reconnectAttempts = 0;
  reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
  lastReconnectAttempt = 0;  // Trigger immediate attempt in next mqtt_loop()
  
  g_lastErr = "";
}
