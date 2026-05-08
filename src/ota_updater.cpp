#include "config.h"
#include "ota_updater.h"

#include "led_events.h"
#include "fs_compat.h"

// Bootstrap-only progress hooks. Per-device builds compile installProductFirmware
// (it's gated only on SUPPORT_OTA_V2, not WORDCLOCK_BOOTSTRAP) but never call it,
// so the macros expand to no-ops there to avoid pulling bootstrap_provision.h
// into per-device link.
#ifdef WORDCLOCK_BOOTSTRAP
#include "bootstrap_provision.h"
#define BOOT_EMIT_PHASE(id, msg) bootstrapEmitPhase((id), (msg))
#define BOOT_EMIT_PROGRESS(d, t) bootstrapEmitProgress((d), (t))
#else
#define BOOT_EMIT_PHASE(id, msg) ((void)0)
#define BOOT_EMIT_PROGRESS(d, t) ((void)0)
#endif

#if !OTA_ENABLED

String getUiVersion() {
  return UI_VERSION;
}

void checkForFirmwareUpdate() {}

bool installProductFirmware(const String&, const String&) { return false; }
bool listAvailableChannels(const String&, std::vector<String>& out) { out.clear(); return false; }

#if SUPPORT_OTA_V2 == 0
void syncFilesFromManifest() {}
void syncUiFilesFromConfiguredVersion() {}
#endif

#else

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cctype>
#include "log.h"
#include "secrets.h"
#include "display_settings.h"
#include "grid_layout.h"
#include "system_utils.h"

static const char* FS_IMAGE_VERSION_FILE = "/.fs_image_version";

#if SUPPORT_OTA_V2 == 0
static const char* FS_VERSION_FILE = "/.fs_version"; // UI sync marker
static const char* UI_FILES[] = {
  "admin.html",
  "changepw.html",
  "dashboard.html",
  "logs.html",
  "mqtt.html",
  "update.html",
};

struct FileEntry {
  String path;
  String url;
  String sha256; // optioneel
};

static bool ensureDirs(const String& path) {
  for (size_t i = 1; i < path.length(); ++i) {
    if (path[i] == '/') {
      if (!FS_IMPL.mkdir(path.substring(0, i))) {
        // ok if exists
      }
    }
  }
  return true;
}

static bool verifySha256(const String& /*expected*/, File& /*f*/) {
  return true;
}

static bool downloadToFs(const String& url, const String& path, WiFiClientSecure& client) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) {
    logError("HTTP " + String(code) + " for " + url);
    http.end();
    return false;
  }

  int len = http.getSize();
  const int expectedLen = len;
  if (len == 0) { http.end(); return false; }

  String tmp = path + ".tmp";
  ensureDirs(path);
  File f = FS_IMPL.open(tmp, "w");
  if (!f) { http.end(); return false; }

  WiFiClient& s = http.getStream();
  uint8_t buf[2048];
  int written = 0;
  bool readTimedOut = false;
  while (http.connected() && (len > 0 || len == -1)) {
    size_t n = s.readBytes(buf, sizeof(buf));
    if (n == 0) {
      if (http.connected()) {
        readTimedOut = true;
      }
      break;
    }
    f.write(buf, n);
    written += n;
    if (len > 0) len -= n;
  }
  f.flush(); f.close();
  http.end();

  if (readTimedOut) {
    logError("HTTP read timeout for " + url);
    FS_IMPL.remove(tmp);
    return false;
  }
  if (expectedLen > 0 && written != expectedLen) {
    logError("HTTP short read for " + url + " (" + String(written) + "/" + String(expectedLen) + ")");
    FS_IMPL.remove(tmp);
    return false;
  }
  if (written == 0) {
    FS_IMPL.remove(tmp);
    return false;
  }

  FS_IMPL.remove(path);
  if (!FS_IMPL.rename(tmp, path)) {
    FS_IMPL.remove(tmp);
    return false;
  }
  logDebug("Wrote " + path + " (" + String(written) + " bytes)");
  return true;
}

static String readFsVersion() {
  File f = FS_IMPL.open(FS_VERSION_FILE, "r");
  if (!f) return "";
  String v = f.readString();
  f.close();
  v.trim();
  return v;
}

static void writeFsVersion(const String& v) {
  File f = FS_IMPL.open(FS_VERSION_FILE, "w");
  if (!f) return;
  f.print(v);
  f.close();
}
#endif

static String readFsImageVersion() {
  File f = FS_IMPL.open(FS_IMAGE_VERSION_FILE, "r");
  if (!f) return "";
  String v = f.readString();
  f.close();
  v.trim();
  return v;
}

static void writeFsImageVersion(const String& v) {
  File f = FS_IMPL.open(FS_IMAGE_VERSION_FILE, "w");
  if (!f) return;
  f.print(v);
  f.close();
}

String getUiVersion() {
#if SUPPORT_OTA_V2
  if (FS_IMPL.begin(false)) {
    String v = readFsImageVersion();
    if (v.length()) return v;
  }
  return UI_VERSION;
#else
  if (FS_IMPL.begin(false)) {
    String v = readFsVersion();
    if (v.length()) return v;
  }
  return UI_VERSION;
#endif
}

static String normalizeChannel(String ch) {
  ch.toLowerCase();
  if (ch != "stable" && ch != "early" && ch != "develop") {
    ch = "stable";
  }
  return ch;
}

#if SUPPORT_OTA_V2 == 0
static String buildManifestUrl(const String& channel) {
  String url = String(VERSION_URL_BASE);
  url += (url.indexOf('?') >= 0) ? "&channel=" : "?channel=";
  url += channel;
  return url;
}

static bool fetchManifest(JsonDocument& doc, WiFiClientSecure& client, const String& channel) {
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Accept-Encoding", "identity");
  const String url = buildManifestUrl(channel);
  logDebug("OTA manifest URL: " + url);
  http.begin(client, url);
  int code = http.GET();
  if (code != 200) {
    logError("Failed to GET manifest: HTTP " + String(code));
    logError("Manifest URL: " + url);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  if (payload.length() == 0) {
    logError("Manifest body is empty");
    return false;
  }
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    logError(String("JSON parse error: ") + err.c_str());
    logError("Manifest size: " + String(payload.length()));
    // Log first 200 chars of payload for debugging
    String preview = payload.substring(0, 200);
    logError("Payload preview: " + preview);
    return false;
  }
  return true;
}

#endif

static String buildOta2ChannelUrl(const String& productId, const String& channel) {
  String url = String(OTA_BASE_URL);
  if (!url.endsWith("/")) url += "/";
  url += productId;
  url += "/channels/";
  url += channel;
  url += ".json";
  return url;
}

static bool fetchJsonByUrl(JsonDocument& doc, WiFiClient& client, const String& url, const char* label) {
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Accept-Encoding", "identity");
#if SUPPORT_OTA_V2 && OTA2_NO_CACHE_HEADERS
  http.addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  http.addHeader("Pragma", "no-cache");
  http.addHeader("Expires", "0");
#endif
  if (!http.begin(client, url)) {
    logError(String("Failed to begin ") + label + " request");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    logError(String("Failed to GET ") + label + ": HTTP " + String(code));
    logError(String(label) + " URL: " + url);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  if (payload.length() == 0) {
    logError(String(label) + " body is empty");
    return false;
  }
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    logError(String(label) + " JSON parse error: " + err.c_str());
    logError(String(label) + " size: " + String(payload.length()));
    String preview = payload.substring(0, 200);
    logError(String(label) + " preview: " + preview);
    return false;
  }
  return true;
}

// Fetch the channel JSON for an arbitrary productId/channel. Used both by
// the device's per-product OTA loop (productId == PRODUCT_ID) and by the
// nextgen-bootstrap firmware (productId chosen by the operator at first
// flash). Caller is responsible for any product-specific logging since
// bootstrap has no grid/displaySettings to query.
static bool fetchOta2Channel(JsonDocument& doc, WiFiClient& client,
                             const String& productId, const String& channel) {
  const String url = buildOta2ChannelUrl(productId, channel);
  logDebug(String("OTA product: ") + productId);
  logDebug("OTA channel URL: " + url);
  return fetchJsonByUrl(doc, client, url, "channel info");
}

static bool fetchOta2Artifact(JsonDocument& doc, WiFiClient& client, const String& url) {
  logDebug("OTA artifact URL: " + url);
  return fetchJsonByUrl(doc, client, url, "artifact manifest");
}

static std::vector<int> parseVersionCore(const String& version) {
  std::vector<int> parts;
  int current = 0;
  bool inNumber = false;
  for (size_t i = 0; i < version.length(); ++i) {
    const char c = version[i];
    if (std::isdigit(static_cast<unsigned char>(c))) {
      current = (current * 10) + (c - '0');
      inNumber = true;
      continue;
    }
    if (c == '.') {
      if (inNumber) {
        parts.push_back(current);
        current = 0;
        inNumber = false;
      }
      continue;
    }
    break;
  }
  if (inNumber) parts.push_back(current);
  return parts;
}

static bool isVersionNewer(const String& remote, const String& current) {
  if (remote == current) return false;
  const std::vector<int> a = parseVersionCore(remote);
  const std::vector<int> b = parseVersionCore(current);
  const size_t n = std::max(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    const int av = (i < a.size()) ? a[i] : 0;
    const int bv = (i < b.size()) ? b[i] : 0;
    if (av != bv) return av > bv;
  }
  // Same numeric core but different full string (e.g. suffix change) -> update.
  return true;
}

static bool performHttpOta(const String& firmwareUrl, WiFiClient& client) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
#if SUPPORT_OTA_V2 && OTA2_NO_CACHE_HEADERS
  http.addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  http.addHeader("Pragma", "no-cache");
  http.addHeader("Expires", "0");
#endif
  if (!http.begin(client, firmwareUrl)) {
    logError("❌ http.begin failed");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    logError("❌ Firmware download failed: HTTP " + String(code));
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    logError("❌ Invalid firmware size");
    http.end();
    return false;
  }

  if (!Update.begin(contentLength)) {
    logError("❌ Update.begin() failed");
    http.end();
    return false;
  }

  WiFiClient& stream = http.getStream();
  size_t written = Update.writeStream(stream);
  http.end();

  if (written != (size_t)contentLength) {
    logError("❌ Incomplete write: " + String(written) + "/" + String(contentLength));
    Update.abort();
    return false;
  }
  if (!Update.end()) {
    logError("❌ Update.end() failed");
    return false;
  }
  if (!Update.isFinished()) {
    logError("❌ Update not finished");
    return false;
  }
  return true;
}

static bool performFilesystemUpdate(const String& fsUrl, int expectedSize, WiFiClient& client) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
#if SUPPORT_OTA_V2 && OTA2_NO_CACHE_HEADERS
  http.addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  http.addHeader("Pragma", "no-cache");
  http.addHeader("Expires", "0");
#endif
  if (!http.begin(client, fsUrl)) {
    logError("❌ http.begin failed for filesystem update");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    logError("❌ Filesystem download failed: HTTP " + String(code));
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (expectedSize > 0 && contentLength != expectedSize) {
    logError("❌ Filesystem size mismatch: " + String(contentLength) + "/" + String(expectedSize));
    http.end();
    return false;
  }
  if (contentLength <= 0) {
    logError("❌ Invalid filesystem size");
    http.end();
    return false;
  }

  if (!Update.begin(contentLength, U_SPIFFS)) {
    logError("❌ Update.begin(U_SPIFFS) failed");
    http.end();
    return false;
  }

  WiFiClient& stream = http.getStream();
  size_t written = Update.writeStream(stream);
  http.end();

  if (written != (size_t)contentLength) {
    logError("❌ Filesystem write incomplete: " + String(written) + "/" + String(contentLength));
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {
    logError("❌ Filesystem Update.end() failed");
    return false;
  }
  return Update.isFinished();
}

#if SUPPORT_OTA_V2 == 0
static JsonVariant selectChannelBlock(JsonDocument& doc, const String& requested, String& selected) {
  selected = requested;
  JsonVariant channels = doc["channels"];
  if (channels.is<JsonObject>()) {
    JsonVariant blk = channels[requested];
    if (!blk.isNull()) return blk;
    blk = channels["stable"];
    if (!blk.isNull()) {
      selected = "stable";
      return blk;
    }
  }
  return JsonVariant(); // empty -> legacy/top-level (keep selected as requested)
}

static bool parseFiles(JsonVariantConst jfiles, std::vector<FileEntry>& out) {
  JsonArrayConst arr = jfiles.as<JsonArrayConst>();
  if (arr.isNull()) return false;
  for (JsonObjectConst v : arr) {
    FileEntry e;
    e.path = v["path"] | "";
    e.url  = v["url"]  | "";
    e.sha256 = v["sha256"] | "";
    if (e.path.length() && e.url.length()) out.push_back(e);
  }
  return true;
}

static bool isHtmlFileHealthy(const char* path) {
  File f = FS_IMPL.open(path, "r");
  if (!f) return false;
  size_t size = f.size();
  if (size < 64) { f.close(); return false; }

  const size_t headLen = std::min<size_t>(256, size);
  char headBuf[257] = {0};
  size_t headRead = f.readBytes(headBuf, headLen);
  headBuf[headRead] = '\0';
  if (std::strstr(headBuf, "<!DOCTYPE html") == nullptr) {
    f.close();
    return false;
  }

  const size_t tailLen = std::min<size_t>(256, size);
  if (size > tailLen) {
    f.seek(size - tailLen, SeekSet);
  } else {
    f.seek(0, SeekSet);
  }
  char tailBuf[257] = {0};
  size_t tailRead = f.readBytes(tailBuf, tailLen);
  tailBuf[tailRead] = '\0';
  f.close();
  return std::strstr(tailBuf, "</html>") != nullptr;
}

static bool areUiFilesHealthy() {
  for (const char* name : UI_FILES) {
    String path = "/" + String(name);
    if (!isHtmlFileHealthy(path.c_str())) return false;
  }
  return true;
}

void syncUiFilesFromConfiguredVersion() {
#if SUPPORT_OTA_V2
  logInfo("UI sync is legacy-only; skipping (OTA2 enabled).");
  return;
#endif
  logInfo("🔍 Checking UI files (configured version)...");
  if (!FS_IMPL.begin(true)) {
    logError("FS mount failed");
    return;
  }

  const String targetVersion = UI_VERSION;
  if (targetVersion.isEmpty()) {
    logError("UI_VERSION is empty; skipping UI sync.");
    return;
  }
  const String currentVersion = readFsVersion();
  if (currentVersion == targetVersion) {
    if (areUiFilesHealthy()) {
      logInfo("UI up-to-date (configured version match).");
      return;
    }
    logWarn("UI version matches but files look invalid; re-syncing.");
  }

  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure());
  client->setInsecure();

  bool ok = true;
  for (const char* name : UI_FILES) {
    const String url = String("https://raw.githubusercontent.com/lumetric-io/Wordclock/v") + targetVersion + "/data/" + name;
    const String path = "/" + String(name);
    if (!downloadToFs(url, path, *client)) {
      ok = false;
    }
  }

  if (ok) {
    writeFsVersion(targetVersion);
    logInfo("✅ UI files synced from configured version.");
  } else {
    logError("⚠️ Some UI files failed (configured version).");
  }
}

void syncFilesFromManifest() {
#if SUPPORT_OTA_V2
  logInfo("UI sync is legacy-only; skipping (OTA2 enabled).");
  return;
#endif
  logInfo("🔍 Checking UI files…");
  if (!FS_IMPL.begin(true)) {
    logError("FS mount failed");
    return;
  }

  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure());
  client->setInsecure();

  String requestedChannel = normalizeChannel(displaySettings.getUpdateChannel());

  JsonDocument doc;
  if (!fetchManifest(doc, *client, requestedChannel)) return;
  String selectedChannel;
  JsonVariant channelBlock = selectChannelBlock(doc, requestedChannel, selectedChannel);
  if (requestedChannel != selectedChannel) {
    logDebug(String("Manifest channel fallback: requested ") + requestedChannel + " -> using " + selectedChannel);
  } else {
    logDebug(String("Manifest channel: ") + selectedChannel);
  }
  if (!channelBlock.isNull() && channelBlock["release_notes"].is<const char*>()) {
    logDebug(String("Release notes (") + selectedChannel + "): " + channelBlock["release_notes"].as<const char*>());
  }

  String manifestVersion;
  if (!channelBlock.isNull()) {
    if (channelBlock["ui_version"].is<const char*>()) manifestVersion = channelBlock["ui_version"].as<const char*>();
    else if (channelBlock["version"].is<const char*>()) manifestVersion = channelBlock["version"].as<const char*>();
  }
  if (manifestVersion.isEmpty()) {
    manifestVersion = doc["ui_version"].is<const char*>() ? String(doc["ui_version"].as<const char*>())
                       : (doc["version"].is<const char*>() ? String(doc["version"].as<const char*>()) : String(""));
  }
  const String currentFsVer = readFsVersion();

  if (manifestVersion.length() && manifestVersion == currentFsVer) {
    if (areUiFilesHealthy()) {
      logInfo("UI up-to-date (version match).");
      return;
    }
    logWarn("UI version matches but files look invalid; re-syncing.");
  }

  std::vector<FileEntry> files;
  JsonVariantConst fileList;
  if (!channelBlock.isNull() && channelBlock["files"].is<JsonArray>()) {
    fileList = channelBlock["files"];
  } else if (doc["files"].is<JsonArray>()) {
    fileList = doc["files"];
  }

  if (!fileList.isNull() && parseFiles(fileList, files) && !files.empty()) {
    bool ok = true;
    for (const auto& e : files) {
      if (!downloadToFs(e.url, e.path, *client)) { ok = false; }
    }
    if (ok && manifestVersion.length()) writeFsVersion(manifestVersion);
    logInfo(ok ? "✅ UI files synced." : "⚠️ Some UI files failed.");
  } else {
    logInfo("No file list in manifest; skipping UI sync.");
  }
}

static void checkForFirmwareUpdateLegacy() {
  logInfo("🔍 Checking for new firmware...");
  ledEventPulse(LedEvent::FirmwareCheck);

  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure());
  client->setInsecure();

  String requestedChannel = normalizeChannel(displaySettings.getUpdateChannel());

  JsonDocument doc;
  if (!fetchManifest(doc, *client, requestedChannel)) return;
  String selectedChannel;
  JsonVariant channelBlock = selectChannelBlock(doc, requestedChannel, selectedChannel);
  if (requestedChannel != selectedChannel) {
    logDebug(String("Manifest channel fallback: requested ") + requestedChannel + " -> using " + selectedChannel);
  } else {
    logDebug(String("Manifest channel: ") + selectedChannel);
  }

  JsonVariant firmwareBlock;
  if (!channelBlock.isNull()) {
    firmwareBlock = channelBlock["firmware"];
  }

  String remoteVersion;
  if (!firmwareBlock.isNull()) {
    if (firmwareBlock["version"].is<const char*>()) {
      remoteVersion = firmwareBlock["version"].as<const char*>();
    }
  }
  if (remoteVersion.isEmpty() && !channelBlock.isNull() && channelBlock["version"].is<const char*>()) {
    remoteVersion = channelBlock["version"].as<const char*>();
  }
  if (remoteVersion.isEmpty()) {
    remoteVersion = doc["firmware"]["version"].is<const char*>() ? String(doc["firmware"]["version"].as<const char*>())
                   : (doc["version"].is<const char*>() ? String(doc["version"].as<const char*>()) : String(""));
  }

  String fwUrl;
  if (!firmwareBlock.isNull()) {
    if (firmwareBlock.is<const char*>()) fwUrl = firmwareBlock.as<const char*>();
    else if (firmwareBlock["url"].is<const char*>()) fwUrl = firmwareBlock["url"].as<const char*>();
  }
  if (fwUrl.isEmpty()) {
    fwUrl = doc["firmware"].is<const char*>() ? String(doc["firmware"].as<const char*>())
           : (doc["firmware"]["url"].is<const char*>() ? String(doc["firmware"]["url"].as<const char*>())
           : "");
  }

  if (!fwUrl.length()) {
    logError("❌ Firmware URL missing");
    return;
  }

  logInfo("ℹ️ Remote version: " + remoteVersion);
  if (remoteVersion == FIRMWARE_VERSION) {
    logInfo("✅ Firmware already latest (" + String(FIRMWARE_VERSION) + ")");
    ledEventStop(LedEvent::FirmwareAvailable);
    syncFilesFromManifest();
    return;
  }
  ledEventStart(LedEvent::FirmwareAvailable);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if (!http.begin(*client, fwUrl)) {
    logError("❌ http.begin failed");
    ledEventStop(LedEvent::FirmwareAvailable);
    return;
  }
  int code = http.GET();
  if (code != 200) {
    logError("❌ Firmware download failed: HTTP " + String(code));
    http.end();
    ledEventStop(LedEvent::FirmwareAvailable);
    return;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    logError("❌ Invalid firmware size");
    http.end();
    ledEventStop(LedEvent::FirmwareAvailable);
    return;
  }

  if (!Update.begin(contentLength)) {
    logError("❌ Update.begin() failed");
    http.end();
    ledEventStop(LedEvent::FirmwareAvailable);
    return;
  }

  ledEventStop(LedEvent::FirmwareAvailable);
  ledEventStart(LedEvent::FirmwareDownloading);

  WiFiClient& stream = http.getStream();
  size_t written = Update.writeStream(stream);
  http.end();

  if (written != (size_t)contentLength) {
    logError("❌ Incomplete write: " + String(written) + "/" + String(contentLength));
    Update.abort();
    ledEventStop(LedEvent::FirmwareDownloading);
    return;
  }
  if (!Update.end()) {
    logError("❌ Update.end() failed");
    ledEventStop(LedEvent::FirmwareDownloading);
    return;
  }
  if (Update.isFinished()) {
    logInfo("✅ Firmware updated, rebooting...");
    ledEventStop(LedEvent::FirmwareDownloading);
    ledEventStart(LedEvent::FirmwareApplying);
    delay(500);
    safeRestart();
  } else {
    logError("❌ Update not finished");
    ledEventStop(LedEvent::FirmwareDownloading);
  }
}

#endif

#ifndef WORDCLOCK_BOOTSTRAP
// Per-device OTA loop: depends on displaySettings (channel selection) and
// grid_layout (debug log only). Excluded from the bootstrap build because
// bootstrap doesn't link those singletons.
static void checkForFirmwareUpdateV2() {
  logInfo("🔍 Checking for new firmware...");
  ledEventPulse(LedEvent::FirmwareCheck);

  std::unique_ptr<WiFiClient> client(new WiFiClient());

  String requestedChannel = normalizeChannel(displaySettings.getUpdateChannel());

  const GridVariantInfo* info = getGridVariantInfo(displaySettings.getGridVariant());
  logDebug(String("OTA grid: ") + (info ? info->key : "unknown"));

  JsonDocument channelDoc;
  if (!fetchOta2Channel(channelDoc, *client, PRODUCT_ID, requestedChannel)) return;

  JsonVariant target = channelDoc["target"];
  if (target.isNull()) {
    logInfo("✅ No firmware update available.");
    ledEventStop(LedEvent::FirmwareAvailable);
#if SUPPORT_OTA_V2 == 0
    syncFilesFromManifest();
#endif
    return;
  }

  const String remoteVersion = target["version"] | "";
  const String manifestUrl = target["manifest_url"] | "";
  const String fsManifestUrl = target["fs_manifest_url"] | "";
  if (manifestUrl.isEmpty()) {
    logError("❌ OTA manifest_url missing");
    return;
  }

  bool fsUpdated = false;
  if (!fsManifestUrl.isEmpty()) {
    JsonDocument fsDoc;
    if (fetchOta2Artifact(fsDoc, *client, fsManifestUrl)) {
      const String fsType = fsDoc["fs"] | "";
      const String fsVersion = fsDoc["version"] | "";
      const int fsSize = fsDoc["filesize"] | 0;
      const String fsUrl = fsDoc["url"] | "";

      if (fsType != "littlefs") {
        logWarn("⚠️ FS manifest fs type not supported: " + fsType);
      } else if (fsUrl.isEmpty()) {
        logError("❌ FS manifest URL missing");
      } else {
        const String currentFsVersion = readFsImageVersion();
        if (!fsVersion.isEmpty() && fsVersion == currentFsVersion) {
          logInfo("✅ Filesystem already latest (" + fsVersion + ")");
        } else {
          logInfo("⬇️ Updating filesystem (" + fsVersion + ")...");
          if (performFilesystemUpdate(fsUrl, fsSize, *client)) {
            if (fsVersion.length()) {
              writeFsImageVersion(fsVersion);
            }
            fsUpdated = true;
            logInfo("✅ Filesystem updated");
          } else {
            logError("❌ Filesystem update failed");
          }
        }
      }
    }
  }

  logInfo("ℹ️ Remote version: " + remoteVersion);
  if (!isVersionNewer(remoteVersion, FIRMWARE_VERSION)) {
    logInfo("✅ Firmware already latest (" + String(FIRMWARE_VERSION) + ")");
    ledEventStop(LedEvent::FirmwareAvailable);
    if (fsUpdated) {
      logInfo("🔁 Restarting to apply filesystem update...");
      delay(500);
      safeRestart();
    } else {
#if SUPPORT_OTA_V2 == 0
      syncFilesFromManifest();
#endif
    }
    return;
  }

  ledEventStart(LedEvent::FirmwareAvailable);

  JsonDocument artifactDoc;
  if (!fetchOta2Artifact(artifactDoc, *client, manifestUrl)) {
    ledEventStop(LedEvent::FirmwareAvailable);
    return;
  }

  const String fwUrl = artifactDoc["url"] | "";
  const String sha256 = artifactDoc["sha256"] | "";
  if (fwUrl.isEmpty()) {
    logError("❌ Firmware URL missing from artifact manifest");
    return;
  }
  if (sha256.length()) {
    logDebug("ℹ️ OTA SHA256: " + sha256);
  }

  logInfo("⬇️ Starting firmware update...");
  ledEventStop(LedEvent::FirmwareAvailable);
  ledEventStart(LedEvent::FirmwareDownloading);
  if (!performHttpOta(fwUrl, *client)) {
    ledEventStop(LedEvent::FirmwareDownloading);
    return;
  }

  logInfo("✅ Firmware updated, rebooting...");
  ledEventStop(LedEvent::FirmwareDownloading);
  ledEventStart(LedEvent::FirmwareApplying);
  delay(500);
  safeRestart();
}

#endif  // !WORDCLOCK_BOOTSTRAP — close gate around checkForFirmwareUpdateV2

#ifdef WORDCLOCK_BOOTSTRAP
// Bootstrap firmware never auto-checks for updates; provisioning is driven
// by installProductFirmware() called from the operator's product picker.
void checkForFirmwareUpdate() {}
#else
void checkForFirmwareUpdate() {
#if SUPPORT_OTA_V2
  checkForFirmwareUpdateV2();
#else
  checkForFirmwareUpdateLegacy();
#endif
}
#endif

// ─────────────────────────────────────────────────────────────────────
// Bootstrap-only OTA: install firmware + fs for an explicitly chosen
// product/channel, ignoring any per-device state (no FIRMWARE_VERSION
// comparison, no grid/displaySettings dependencies). Used by the
// nextgen-bootstrap firmware on first-flash provisioning.
// ─────────────────────────────────────────────────────────────────────

#if SUPPORT_OTA_V2
bool installProductFirmware(const String& productId, const String& channel) {
  logInfo(String("🔧 Bootstrap provisioning ") + productId + " (" + channel + ")");

  // Update is a global singleton — registering once here means the same
  // callback fires for both the fs and firmware writeStream() loops, with
  // `total` automatically tracking whichever Update.begin(contentLength)
  // call is currently in flight.
  Update.onProgress([](size_t done, size_t total) {
    BOOT_EMIT_PROGRESS(done, total);
  });

  BOOT_EMIT_PHASE("fetching-channels", "Fetching manifest…");

  std::unique_ptr<WiFiClient> client(new WiFiClient());

  JsonDocument channelDoc;
  if (!fetchOta2Channel(channelDoc, *client, productId, channel)) {
    logError("❌ Bootstrap: failed to fetch channel manifest");
    return false;
  }

  JsonVariant target = channelDoc["target"];
  if (target.isNull()) {
    logError(String("❌ Bootstrap: channel '") + channel + "' has no target for " + productId);
    return false;
  }

  const String manifestUrl = target["manifest_url"] | "";
  const String fsManifestUrl = target["fs_manifest_url"] | "";
  if (manifestUrl.isEmpty()) {
    logError("❌ Bootstrap: manifest_url missing");
    return false;
  }

  // Filesystem first. If firmware succeeds but fs fails, the new product
  // firmware would boot without its UI — a worse failure mode than the
  // reverse (bootstrap survives if fs OTA fails before firmware OTA starts).
  if (!fsManifestUrl.isEmpty()) {
    JsonDocument fsDoc;
    if (!fetchOta2Artifact(fsDoc, *client, fsManifestUrl)) {
      logError("❌ Bootstrap: failed to fetch fs manifest");
      return false;
    }
    const String fsType = fsDoc["fs"] | "";
    const int fsSize = fsDoc["filesize"] | 0;
    const String fsUrl = fsDoc["url"] | "";
    const String fsVersion = fsDoc["version"] | "";
    if (fsType != "littlefs") {
      logError(String("❌ Bootstrap: unsupported fs type: ") + fsType);
      return false;
    }
    if (fsUrl.isEmpty()) {
      logError("❌ Bootstrap: fs URL missing");
      return false;
    }
    BOOT_EMIT_PHASE("downloading-fs", "Downloading filesystem image…");
    BOOT_EMIT_PROGRESS(0, (size_t)fsSize);
    logInfo(String("⬇️ Bootstrap: downloading fs (") + fsVersion + ", " + String(fsSize) + " bytes)…");
    if (!performFilesystemUpdate(fsUrl, fsSize, *client)) {
      logError("❌ Bootstrap: fs update failed");
      return false;
    }
    if (fsVersion.length()) writeFsImageVersion(fsVersion);
    logInfo("✅ Bootstrap: fs updated");
  }

  // Firmware
  JsonDocument artifactDoc;
  if (!fetchOta2Artifact(artifactDoc, *client, manifestUrl)) {
    logError("❌ Bootstrap: failed to fetch firmware manifest");
    return false;
  }
  const String fwUrl = artifactDoc["url"] | "";
  const int fwSize = artifactDoc["filesize"] | 0;
  if (fwUrl.isEmpty()) {
    logError("❌ Bootstrap: firmware URL missing");
    return false;
  }
  BOOT_EMIT_PHASE("downloading-firmware", "Downloading firmware…");
  BOOT_EMIT_PROGRESS(0, (size_t)fwSize);
  logInfo(String("⬇️ Bootstrap: downloading firmware from ") + fwUrl);
  if (!performHttpOta(fwUrl, *client)) {
    logError("❌ Bootstrap: firmware update failed");
    return false;
  }

  BOOT_EMIT_PHASE("applying", "Applying & rebooting…");
  logInfo("✅ Bootstrap: firmware applied; rebooting into product…");
  delay(500);
  safeRestart();
  return true;  // not reached
}

bool listAvailableChannels(const String& productId, std::vector<String>& out) {
  out.clear();
  std::unique_ptr<WiFiClient> client(new WiFiClient());
  static const char* kCandidates[] = { "stable", "early", "develop" };
  for (const char* ch : kCandidates) {
    JsonDocument doc;
    if (!fetchOta2Channel(doc, *client, productId, String(ch))) continue;
    // Channel exists if the JSON has a non-null "target".
    JsonVariant target = doc["target"];
    if (!target.isNull()) {
      out.push_back(String(ch));
    }
  }
  return !out.empty();
}
#else
bool installProductFirmware(const String&, const String&) { return false; }
bool listAvailableChannels(const String&, std::vector<String>& out) { out.clear(); return false; }
#endif

#endif // OTA_ENABLED
