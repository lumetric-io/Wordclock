#include "bootstrap_provision.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "log.h"
#include "ota_updater.h"
#include "fs_compat.h"

// Defined in bootstrap_main.cpp. Route lambdas reference this global
// directly so their captures don't dangle after registerBootstrapRoutes()
// returns (the parameter binding goes out of scope; the global doesn't).
extern WebServer server;

// ───────────────────────────────────────────────────────────────────────
// Product table — the operator picks one of these in the bootstrap UI.
// Each entry maps to an OTA channel published at:
//   <OTA_BASE_URL>/<id>/channels/{stable,early,develop}.json
// Descriptions are short hints to help match the physical device; English
// only since this is a factory tool.
// ───────────────────────────────────────────────────────────────────────
// All current per-device firmwares share the same WiFiManager portal SSID
// (AP_NAME in src/config.h). Kept as a per-product field so a future product
// can override it without touching the bootstrap UI.
const BootstrapProduct kBootstrapProducts[] = {
  {"nextgen-mini",         "Wordclock Mini",          "Compact 11x11 grid",       "Wordclock_AP"},
  {"nextgen-30x30",        "Wordclock 30x30",         "11x11 grid, 30x30 cm",     "Wordclock_AP"},
  {"nextgen-50x50",        "Wordclock 50x50",         "11x11 grid, 50x50 cm",     "Wordclock_AP"},
  {"nextgen-logo-55x50",   "Wordclock Logo 55x50",    "11x11 grid + logo strip",  "Wordclock_AP"},
  {"nextgen-logo-100x100", "Wordclock Logo 100x100",  "20x20 grid + logo",        "Wordclock_AP"},
};
const size_t kBootstrapProductCount =
  sizeof(kBootstrapProducts) / sizeof(kBootstrapProducts[0]);

// ───────────────────────────────────────────────────────────────────────
// State
// ───────────────────────────────────────────────────────────────────────
namespace {
volatile BootstrapState g_state = BootstrapState::Idle;
String g_statusMessage;
String g_selectedProduct;
String g_selectedChannel;
TaskHandle_t g_provisionTask = nullptr;
// Byte-level progress within the current phase. Both reset to 0 on phase
// transition; total stays 0 outside download phases. ESP32 size_t is
// 32-bit and same-task writes/cross-task reads are atomic enough for a
// progress display (stale reads are fine).
volatile size_t g_bytesDone = 0;
volatile size_t g_bytesTotal = 0;

void setState(BootstrapState s, const String& msg = String()) {
  g_state = s;
  g_statusMessage = msg;
  logInfo(String("[bootstrap] state=") + (int)s + " msg=" + msg);
}

const char* stateName(BootstrapState s) {
  switch (s) {
    case BootstrapState::Idle:                return "idle";
    case BootstrapState::FetchingChannels:    return "fetching-channels";
    case BootstrapState::DownloadingFirmware: return "downloading-firmware";
    case BootstrapState::DownloadingFs:       return "downloading-fs";
    case BootstrapState::Applying:            return "applying";
    case BootstrapState::Done:                return "done";
    case BootstrapState::Failed:              return "failed";
  }
  return "unknown";
}

bool isValidProductId(const String& id) {
  for (size_t i = 0; i < kBootstrapProductCount; ++i) {
    if (id == kBootstrapProducts[i].id) return true;
  }
  return false;
}

bool isValidChannel(const String& ch) {
  return ch == "stable" || ch == "early" || ch == "develop";
}

// FreeRTOS task: run the actual OTA install. installProductFirmware blocks
// for the duration of two HTTP downloads (fs.bin + firmware.bin) and then
// reboots — it never returns on success. installProductFirmware itself
// emits phase transitions and byte-level progress via bootstrapEmitPhase /
// bootstrapEmitProgress, so this task only handles failure bookkeeping.
void provisionTaskFn(void* params) {
  String* args = static_cast<String*>(params);
  String productId = args[0];
  String channel = args[1];
  delete[] args;

  if (!installProductFirmware(productId, channel)) {
    setState(BootstrapState::Failed,
             String("Provisioning failed for ") + productId + " (" + channel + ")");
    g_provisionTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  // installProductFirmware reboots on success — we shouldn't reach here.
  setState(BootstrapState::Done, "Provisioned. Rebooting…");
  g_provisionTask = nullptr;
  vTaskDelete(nullptr);
}

// FreeRTOS task: bootstrap-self-update. Hands off to installProductFirmware
// (which reboots on success) when a newer build exists; otherwise reports
// "up to date" through the same state channel the UI already polls.
void selfUpdateTaskFn(void* /*params*/) {
  bool upToDate = false;
  String remoteVersion;
  if (checkForBootstrapSelfUpdate(upToDate, remoteVersion)) {
    // installProductFirmware reboots on success — unreachable.
    setState(BootstrapState::Done, "Updated. Rebooting…");
  } else if (upToDate) {
    setState(BootstrapState::Done,
             String("Already running latest bootstrap (") + FIRMWARE_VERSION + ")");
  } else {
    setState(BootstrapState::Failed,
             remoteVersion.length()
               ? String("Self-update failed (remote ") + remoteVersion + ")"
               : String("Self-update failed (could not reach OTA server)"));
  }
  g_provisionTask = nullptr;
  vTaskDelete(nullptr);
}

// ───────────────────────────────────────────────────────────────────────
// Route handlers
// ───────────────────────────────────────────────────────────────────────
void handleRoot() {
  File f = FS_IMPL.open("/bootstrap.html", "r");
  if (!f) {
    server.send(404, "text/plain",
                "bootstrap.html not found — fs may not be flashed");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleProducts() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < kBootstrapProductCount; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]          = kBootstrapProducts[i].id;
    o["label"]       = kBootstrapProducts[i].label;
    o["description"] = kBootstrapProducts[i].description;
    o["ap_name"]     = kBootstrapProducts[i].apName;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleChannels() {
  if (!server.hasArg("product")) {
    server.send(400, "text/plain", "missing 'product' argument");
    return;
  }
  String productId = server.arg("product");
  if (!isValidProductId(productId)) {
    server.send(400, "text/plain", "unknown product id");
    return;
  }

  setState(BootstrapState::FetchingChannels,
           String("Probing channels for ") + productId + "…");

  std::vector<ChannelTarget> channels;
  listAvailableChannels(productId, channels);

  setState(BootstrapState::Idle, "");

  JsonDocument doc;
  doc["product"] = productId;
  JsonArray arr = doc["channels"].to<JsonArray>();
  for (const auto& ch : channels) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = ch.name;
    o["fw_version"] = ch.fwVersion;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleProvisionStart() {
  if (g_state == BootstrapState::DownloadingFs ||
      g_state == BootstrapState::DownloadingFirmware ||
      g_state == BootstrapState::Applying) {
    server.send(409, "text/plain", "provisioning already in progress");
    return;
  }
  if (!server.hasArg("product") || !server.hasArg("channel")) {
    server.send(400, "text/plain", "missing 'product' or 'channel' argument");
    return;
  }
  String productId = server.arg("product");
  String channel = server.arg("channel");
  if (!isValidProductId(productId)) {
    server.send(400, "text/plain", "unknown product id");
    return;
  }
  if (!isValidChannel(channel)) {
    server.send(400, "text/plain", "invalid channel (stable/early/develop)");
    return;
  }

  g_selectedProduct = productId;
  g_selectedChannel = channel;
  // Pre-roll state shown until installProductFirmware emits its first
  // phase (fetching-channels → downloading-fs → downloading-firmware →
  // applying). Reusing FetchingChannels here is honest about what happens
  // first inside installProductFirmware (channel manifest fetch).
  g_bytesDone = 0;
  g_bytesTotal = 0;
  setState(BootstrapState::FetchingChannels, "Starting provisioning…");

  // Kick off the OTA work in a separate FreeRTOS task so the HTTP server
  // stays responsive for /api/provision/status polling. installProductFirmware
  // takes 30+ seconds on a slow link.
  String* args = new String[2];
  args[0] = productId;
  args[1] = channel;
  BaseType_t ok = xTaskCreate(provisionTaskFn,
                              "bootstrap_provision",
                              8192,
                              args,
                              1,
                              &g_provisionTask);
  if (ok != pdPASS) {
    delete[] args;
    setState(BootstrapState::Failed, "Failed to spawn provisioning task");
    server.send(500, "text/plain", "task spawn failed");
    return;
  }

  JsonDocument doc;
  doc["state"] = stateName(g_state);
  doc["product"] = productId;
  doc["channel"] = channel;
  String out;
  serializeJson(doc, out);
  server.send(202, "application/json", out);
}

void handleSelfUpdateStart() {
  if (g_state == BootstrapState::DownloadingFs ||
      g_state == BootstrapState::DownloadingFirmware ||
      g_state == BootstrapState::Applying) {
    server.send(409, "text/plain", "provisioning already in progress");
    return;
  }

  // Self-update always uses bootstrap/stable; there's no operator-facing
  // channel selector here on purpose (matches the "factory tool, stable
  // only" character of the bootstrap firmware).
  g_selectedProduct = "nextgen-bootstrap";
  g_selectedChannel = "stable";
  g_bytesDone = 0;
  g_bytesTotal = 0;
  setState(BootstrapState::FetchingChannels, "Checking for bootstrap update…");

  BaseType_t ok = xTaskCreate(selfUpdateTaskFn,
                              "bootstrap_selfupdate",
                              8192,
                              nullptr,
                              1,
                              &g_provisionTask);
  if (ok != pdPASS) {
    setState(BootstrapState::Failed, "Failed to spawn self-update task");
    server.send(500, "text/plain", "task spawn failed");
    return;
  }

  JsonDocument doc;
  doc["state"] = stateName(g_state);
  doc["product"] = g_selectedProduct;
  doc["channel"] = g_selectedChannel;
  String out;
  serializeJson(doc, out);
  server.send(202, "application/json", out);
}

void handleProvisionStatus() {
  JsonDocument doc;
  doc["state"] = stateName(g_state);
  doc["message"] = g_statusMessage;
  doc["product"] = g_selectedProduct;
  doc["channel"] = g_selectedChannel;
  doc["bytes_done"] = (uint32_t)g_bytesDone;
  doc["bytes_total"] = (uint32_t)g_bytesTotal;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

}  // namespace

// ───────────────────────────────────────────────────────────────────────
// Public API
// ───────────────────────────────────────────────────────────────────────
BootstrapState bootstrapState() { return g_state; }
String bootstrapStatusMessage() { return g_statusMessage; }
String bootstrapSelectedProduct() { return g_selectedProduct; }
String bootstrapSelectedChannel() { return g_selectedChannel; }
size_t bootstrapBytesDone() { return g_bytesDone; }
size_t bootstrapBytesTotal() { return g_bytesTotal; }

void bootstrapEmitPhase(const char* phaseId, const char* message) {
  BootstrapState s = g_state;
  if      (phaseId && !strcmp(phaseId, "fetching-channels"))    s = BootstrapState::FetchingChannels;
  else if (phaseId && !strcmp(phaseId, "downloading-fs"))       s = BootstrapState::DownloadingFs;
  else if (phaseId && !strcmp(phaseId, "downloading-firmware")) s = BootstrapState::DownloadingFirmware;
  else if (phaseId && !strcmp(phaseId, "applying"))             s = BootstrapState::Applying;
  // Reset byte counters on every phase transition so the UI's progress bar
  // restarts at 0% for each download. They get repopulated by
  // bootstrapEmitProgress as Update.writeStream advances.
  g_bytesDone = 0;
  g_bytesTotal = 0;
  setState(s, String(message ? message : ""));
}

void bootstrapEmitProgress(size_t bytesDone, size_t bytesTotal) {
  g_bytesDone = bytesDone;
  g_bytesTotal = bytesTotal;
}

void registerBootstrapRoutes(WebServer& srv) {
  // Parameter is unused — handlers reference the global `server` directly
  // (see top of file). The parameter is kept so the call-site reads as
  // "register on this server", and to leave room for swapping to a
  // non-global server instance later.
  (void)srv;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/bootstrap.html", HTTP_GET, handleRoot);
  server.on("/api/products", HTTP_GET, handleProducts);
  server.on("/api/provision/channels", HTTP_GET, handleChannels);
  server.on("/api/provision/start", HTTP_POST, handleProvisionStart);
  server.on("/api/provision/status", HTTP_GET, handleProvisionStatus);
  server.on("/api/bootstrap/self-update", HTTP_POST, handleSelfUpdateStart);
  // GET fallback so the UI can trigger via plain link/click without CORS
  // preflight gymnastics. Idempotent semantically (the task itself enforces
  // the in-progress guard).
  server.on("/api/bootstrap/self-update", HTTP_GET, handleSelfUpdateStart);

  // Cross-firmware identity probe. Mirror of the per-device endpoint —
  // admin UIs poll this after triggering /api/installBootstrap to detect
  // when the device has come back as bootstrap. Same shape as per-device
  // (role + firmware + ui + product_id) so the JS branches on `role` only.
  server.on("/api/firmware/identity", HTTP_GET, []() {
    JsonDocument doc;
    doc["role"] = "bootstrap";
    doc["firmware"] = FIRMWARE_VERSION;
    doc["ui"] = UI_VERSION;
    doc["product_id"] = "nextgen-bootstrap";
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });
  server.onNotFound([]() {
    server.send(404, "text/plain", String("not found: ") + server.uri());
  });
}
