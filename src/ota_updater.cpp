#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <esp_https_ota.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include "fs_compat.h"
#include "config.h"
#include "log.h"
#include "secrets.h"
#include "ota_updater.h"
#include "display_settings.h"
#include "system_utils.h"

static const char* FS_VERSION_FILE = "/.fs_version"; // marker
static const char* UI_FILES[] = {
  "admin.html",
  "changepw.html",
  "dashboard.html",
  "logs.html",
  "mqtt.html",
  "setup.html",
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

static String normalizeChannel(String ch) {
  ch.toLowerCase();
  if (ch != "stable" && ch != "early" && ch != "develop") {
    ch = "stable";
  }
  return ch;
}

static String buildManifestUrl(const String& channel) {
  String url = String(VERSION_URL_BASE);
  url += (url.indexOf('?') >= 0) ? "&channel=" : "?channel=";
  url += channel;
  return url;
}

static String buildOta2ChannelUrl(const String& productId, const String& channel) {
  String url = String(OTA_BASE_URL);
  if (!url.endsWith("/")) url += "/";
  url += productId;
  url += "/channels/";
  url += channel;
  url += ".json";
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

static bool fetchJsonByUrl(JsonDocument& doc, WiFiClientSecure& client, const String& url, const char* label) {
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Accept-Encoding", "identity");
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

static bool fetchOta2Channel(JsonDocument& doc, WiFiClientSecure& client, const String& channel) {
  const String url = buildOta2ChannelUrl(PRODUCT_ID, channel);
  logDebug("OTA channel URL: " + url);
  return fetchJsonByUrl(doc, client, url, "channel info");
}

static bool fetchOta2Artifact(JsonDocument& doc, WiFiClientSecure& client, const String& url) {
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

static bool performHttpsOta(const String& firmwareUrl) {
  esp_http_client_config_t http_config = {};
  http_config.url = firmwareUrl.c_str();
  http_config.crt_bundle_attach = esp_crt_bundle_attach;
  http_config.timeout_ms = 15000;
  http_config.keep_alive_enable = true;
  http_config.skip_cert_common_name_check = true;

  esp_https_ota_config_t ota_config = {};
  ota_config.http_config = &http_config;

  esp_err_t ret = esp_https_ota(&ota_config);
  if (ret != ESP_OK) {
    logError(String("❌ esp_https_ota failed: ") + esp_err_to_name(ret));
    return false;
  }
  return true;
}

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

void checkForFirmwareUpdate() {
  logInfo("🔍 Checking for new firmware...");

  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure());
  client->setInsecure();

  String requestedChannel = normalizeChannel(displaySettings.getUpdateChannel());

  JsonDocument channelDoc;
  if (!fetchOta2Channel(channelDoc, *client, requestedChannel)) return;

  JsonVariant target = channelDoc["target"];
  if (target.isNull()) {
    logInfo("✅ No firmware update available.");
    syncFilesFromManifest();
    return;
  }

  const String remoteVersion = target["version"] | "";
  const String manifestUrl = target["manifest_url"] | "";
  if (manifestUrl.isEmpty()) {
    logError("❌ OTA manifest_url missing");
    return;
  }

  logInfo("ℹ️ Remote version: " + remoteVersion);
  if (!isVersionNewer(remoteVersion, FIRMWARE_VERSION)) {
    logInfo("✅ Firmware already latest (" + String(FIRMWARE_VERSION) + ")");
    syncFilesFromManifest();
    return;
  }

  JsonDocument artifactDoc;
  if (!fetchOta2Artifact(artifactDoc, *client, manifestUrl)) return;

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
  if (!performHttpsOta(fwUrl)) {
    return;
  }

  logInfo("✅ Firmware updated, rebooting...");
  delay(500);
  safeRestart();
}
