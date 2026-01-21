#include "mqtt_settings.h"
#include <Preferences.h>

static const char* NS = "mqtt";

static uint16_t read_u16(Preferences& p, const char* key, uint16_t defv) {
  uint32_t v = p.getUInt(key, (uint32_t)defv);
  if (v > 65535) v = defv;
  return (uint16_t)v;
}

bool mqtt_settings_load(MqttSettings& out) {
  Preferences p;
  p.begin(NS, /*readOnly*/ true);
  String host = p.getString("host", "");
  if (host.length() == 0) {
    // No persisted config yet: use neutral defaults; user sets via UI
    out.host = "";
    out.port = 1883;
    out.user = "";
    out.pass = "";
    out.discoveryPrefix = "homeassistant";
    out.baseTopic = "wordclock";
    out.allowAnonymous = false;
    p.end();
    return false; // not persisted yet
  }

  out.host = host;
  out.port = read_u16(p, "port", 1883);
  out.user = p.getString("user", "");
  out.pass = p.getString("pass", "");
  out.discoveryPrefix = p.getString("disc", "homeassistant");
  out.baseTopic = p.getString("base", "wordclock");
  out.allowAnonymous = p.getBool("anon", false);
  p.end();
  return true;
}

bool mqtt_settings_save(const MqttSettings& in) {
  Preferences p;
  if (!p.begin(NS, /*readOnly*/ false)) return false;
  bool ok = true;
  ok &= p.putString("host", in.host) > 0 || in.host.length() == 0;
  ok &= p.putUInt("port", (uint32_t)in.port) > 0;
  ok &= p.putString("user", in.user) >= 0; // allow empty
  ok &= p.putString("pass", in.pass) >= 0; // allow empty
  ok &= p.putString("disc", in.discoveryPrefix) > 0 || in.discoveryPrefix.length() == 0;
  ok &= p.putString("base", in.baseTopic) > 0 || in.baseTopic.length() == 0;
  ok &= p.putBool("anon", in.allowAnonymous);
  p.end();
  return ok;
}

void mqtt_settings_clear() {
  Preferences p;
  if (!p.begin(NS, /*readOnly*/ false)) return;
  p.clear();
  p.end();
}
