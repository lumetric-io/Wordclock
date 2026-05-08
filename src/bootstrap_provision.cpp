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
const BootstrapProduct kBootstrapProducts[] = {
  {"nextgen-mini",         "Wordclock Mini",          "Compact 11x11 grid"},
  {"nextgen-30x30",        "Wordclock 30x30",         "11x11 grid, 30x30 cm"},
  {"nextgen-50x50",        "Wordclock 50x50",         "11x11 grid, 50x50 cm"},
  {"nextgen-logo-55x50",   "Wordclock Logo 55x50",    "11x11 grid + logo strip"},
  {"nextgen-logo-100x100", "Wordclock Logo 100x100",  "20x20 grid + logo"},
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
// reboots — it never returns on success. So this task either reboots the
// chip or marks the state as Failed and exits.
void provisionTaskFn(void* params) {
  String* args = static_cast<String*>(params);
  String productId = args[0];
  String channel = args[1];
  delete[] args;

  // Coarse-grained state updates; installProductFirmware doesn't expose
  // sub-step progress, so we transition through the states linearly and
  // let the UI poll see DownloadingFs → DownloadingFirmware → Applying.
  setState(BootstrapState::DownloadingFs, "Downloading filesystem image…");

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

  std::vector<String> channels;
  listAvailableChannels(productId, channels);

  setState(BootstrapState::Idle, "");

  JsonDocument doc;
  doc["product"] = productId;
  JsonArray arr = doc["channels"].to<JsonArray>();
  for (const auto& ch : channels) {
    arr.add(ch);
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
  setState(BootstrapState::DownloadingFs, "Starting provisioning…");

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

void handleProvisionStatus() {
  JsonDocument doc;
  doc["state"] = stateName(g_state);
  doc["message"] = g_statusMessage;
  doc["product"] = g_selectedProduct;
  doc["channel"] = g_selectedChannel;
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
  server.onNotFound([]() {
    server.send(404, "text/plain", String("not found: ") + server.uri());
  });
}
