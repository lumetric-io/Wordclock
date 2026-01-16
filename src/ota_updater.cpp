#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cctype>
#include "fs_compat.h"
#include "config.h"
#include "log.h"
#include "secrets.h"
#include "ota_updater.h"
#include "display_settings.h"
#include "system_utils.h"

static const char* FS_IMAGE_VERSION_FILE = "/.fs_image_version";

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

static String normalizeChannel(String ch) {
  ch.toLowerCase();
  if (ch != "stable" && ch != "early" && ch != "develop") {
    ch = "stable";
  }
  return ch;
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

static bool fetchJsonByUrl(JsonDocument& doc, WiFiClient& client, const String& url, const char* label) {
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

static bool fetchOta2Channel(JsonDocument& doc, WiFiClient& client, const String& channel) {
  const String url = buildOta2ChannelUrl(PRODUCT_ID, channel);
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

void checkForFirmwareUpdate() {
  logInfo("🔍 Checking for new firmware...");

  std::unique_ptr<WiFiClient> client(new WiFiClient());

  String requestedChannel = normalizeChannel(displaySettings.getUpdateChannel());

  JsonDocument channelDoc;
  if (!fetchOta2Channel(channelDoc, *client, requestedChannel)) return;

  JsonVariant target = channelDoc["target"];
  if (target.isNull()) {
    logInfo("✅ No firmware update available.");
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
    if (fsUpdated) {
      logInfo("🔁 Restarting to apply filesystem update...");
      delay(500);
      safeRestart();
    }
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
  if (!performHttpOta(fwUrl, *client)) {
    return;
  }

  logInfo("✅ Firmware updated, rebooting...");
  delay(500);
  safeRestart();
}
