#include "language_settings.h"

#include <Preferences.h>

#include "log.h"

namespace {

constexpr const char* NS_DISPLAY = "wc_display";
constexpr const char* NS_SYSTEM = "wc_system";
constexpr const char* KEY_LANG = "lang";
constexpr const char* KEY_DIALECT = "dialect";
constexpr const char* KEY_SOURCE = "lang_src";

// Marker for the one-shot field migration. Versioned because a future
// migration may need to revisit the same decision with better evidence.
constexpr const char* KEY_PIN_MARKER = "lang_pin_v1";

constexpr const char* SRC_DEFAULT = "default";
constexpr const char* SRC_USER = "user";
constexpr const char* SRC_MIGRATED = "migrated";

String g_storedLanguage;
String g_storedDialect;
LanguageSettings::Source g_source = LanguageSettings::Source::Default;

LanguageSettings::Source parseSource(const String& s) {
  if (s == SRC_USER) return LanguageSettings::Source::User;
  if (s == SRC_MIGRATED) return LanguageSettings::Source::Migrated;
  return LanguageSettings::Source::Default;
}

const char* sourceKey(LanguageSettings::Source s) {
  switch (s) {
    case LanguageSettings::Source::User: return SRC_USER;
    case LanguageSettings::Source::Migrated: return SRC_MIGRATED;
    default: return SRC_DEFAULT;
  }
}

void writeChoice(const char* lang, const char* dialect, LanguageSettings::Source src) {
  Preferences prefs;
  prefs.begin(NS_DISPLAY, false);
  if (lang) prefs.putString(KEY_LANG, lang);
  if (dialect) prefs.putString(KEY_DIALECT, dialect);
  prefs.putString(KEY_SOURCE, sourceKey(src));
  prefs.end();
}

// Evidence that this chip has run per-device firmware before. Ordered from
// deterministic to merely corroborating; any one of them is enough.
//
// The bias is deliberate and asymmetric. Mistaking a field device for a new
// one would eventually blank a working clock; mistaking a new device for a
// field one costs the customer one skipped confirmation. So this ORs its
// signals rather than requiring agreement.
bool hasPriorUseEvidence() {
  Preferences prefs;

  // The settings migration ran to completion on an earlier boot. Written
  // unconditionally by every per-device firmware, never by bootstrap.
  prefs.begin(NS_SYSTEM, true);
  const bool migrated = prefs.getBool("migrated_v2", false);
  prefs.end();
  if (migrated) return true;

  // The web-UI credentials were seeded on an earlier boot. Same property.
  prefs.begin("ui_auth", true);
  const bool uiSeeded = prefs.getBool("ui_init", false);
  prefs.end();
  if (uiSeeded) return true;

  // Corroborating: settings only a running per-device firmware persists.
  prefs.begin(NS_DISPLAY, true);
  const bool hasDisplaySettings = prefs.isKey("his_sec") || prefs.isKey("upd_ch");
  prefs.end();
  if (hasDisplaySettings) return true;

  prefs.begin("wc_led", true);
  const bool hasLedState = prefs.isKey("br");
  prefs.end();
  return hasLedState;
}

}  // namespace

namespace LanguageSettings {

void begin() {
  Preferences prefs;
  prefs.begin(NS_DISPLAY, true);
  const String lang = prefs.getString(KEY_LANG, "");
  const String dialect = prefs.getString(KEY_DIALECT, "");
  const String src = prefs.getString(KEY_SOURCE, "");
  prefs.end();

  g_source = parseSource(src);

  // Language first: switching variants resets the dialect to that variant's
  // own first entry, which would undo a dialect applied before it.
  if (lang.length() > 0 && !setActiveLanguage(lang.c_str())) {
    // The stored language is not in this build — a downgrade, or a plate that
    // moved to a different SKU. Render the build default rather than nothing,
    // and leave NVS untouched so a later firmware can still honour the choice.
    logWarn(String("⚠️ Stored language '") + lang + "' is not in this build; rendering '" +
            getActiveLanguage() + "'");
  }

  if (dialect.length() > 0 && !setActiveDialect(dialect.c_str())) {
    logWarn(String("⚠️ Stored dialect '") + dialect + "' is unknown for language '" +
            getActiveLanguage() + "'; using default");
  }

  // Nothing stored means the build default is what the clock speaks, so report
  // that as the stored value too — it keeps rebootRequired() honest.
  g_storedLanguage = lang.length() > 0 ? lang : String(getActiveLanguage());
  const ClockDialect* active = getActiveDialect();
  g_storedDialect = dialect.length() > 0 ? dialect : String(active ? active->id : "");

  logInfo(String("🗣️ Language: ") + getActiveLanguage() + " / " +
          (active ? active->id : "?") + " (source: " + sourceName() + ")");
}

const char* activeLanguage() { return getActiveLanguage(); }

const char* activeDialect() {
  const ClockDialect* d = getActiveDialect();
  return d ? d->id : "";
}

const char* storedLanguage() { return g_storedLanguage.c_str(); }
const char* storedDialect() { return g_storedDialect.c_str(); }

Source source() { return g_source; }
const char* sourceName() { return sourceKey(g_source); }

bool isSetupComplete() { return g_source != Source::Default; }

bool rebootRequired() { return g_storedLanguage != String(getActiveLanguage()); }

bool setLanguage(const char* code) {
  if (!code || !hasLanguage(code)) return false;

  // Picking the language already running is still a choice — on a
  // single-language product it is the only thing the customer confirms — so it
  // must move the source off Default even though nothing visibly changes.
  const bool changing = String(code) != String(getActiveLanguage());

  Preferences prefs;
  prefs.begin(NS_DISPLAY, false);
  prefs.putString(KEY_LANG, code);
  prefs.putString(KEY_SOURCE, sourceKey(Source::User));
  if (changing) {
    // A dialect belongs to one plate, so it does not survive a language
    // switch. Drop it rather than leave a stale id that would warn on every
    // boot; the next boot falls back to the new variant's first dialect.
    prefs.remove(KEY_DIALECT);
  }
  prefs.end();

  if (changing) g_storedDialect = "";
  g_storedLanguage = code;
  g_source = Source::User;
  logInfo(String("🗣️ Language set to '") + code + "'" +
          (changing ? " — reboot required" : " (unchanged)"));
  return true;
}

bool setDialect(const char* id) {
  if (!setActiveDialect(id)) return false;
  writeChoice(nullptr, id, g_source);
  g_storedDialect = id;
  logInfo(String("🗣️ Dialect set to '") + id + "'");
  return true;
}

void pinExistingDeviceIfNeeded() {
  Preferences prefs;

  prefs.begin(NS_SYSTEM, true);
  const bool alreadyRan = prefs.getBool(KEY_PIN_MARKER, false);
  prefs.end();
  if (alreadyRan) return;

  prefs.begin(NS_DISPLAY, true);
  const bool alreadyChosen = prefs.isKey(KEY_SOURCE);
  prefs.end();

  if (!alreadyChosen && hasPriorUseEvidence()) {
    // Nothing has overridden the variant yet at this point in boot, so
    // getActiveLanguage() is the build default — which is precisely the
    // language this device has been showing since it was flashed. Pinning
    // anything else would change what a working clock says.
    const char* lang = getActiveLanguage();
    const ClockDialect* dialect = getActiveDialect();
    writeChoice(lang, dialect ? dialect->id : nullptr, Source::Migrated);
    logInfo(String("🗣️ Existing device pinned to language '") + lang + "'");
  }

  // Written last and unconditionally.
  //
  // Last, because a reset between the pin and the marker must retry the pin
  // rather than skip it.
  //
  // Unconditionally, because a brand-new chip has to be recorded as "seen
  // while still new". migrated_v2 is set at the end of this same first boot,
  // so from the second boot onwards a new chip is indistinguishable from a
  // field device — this marker is what freezes the verdict made while the
  // evidence was still meaningful.
  prefs.begin(NS_SYSTEM, false);
  prefs.putBool(KEY_PIN_MARKER, true);
  prefs.end();
}

}  // namespace LanguageSettings
