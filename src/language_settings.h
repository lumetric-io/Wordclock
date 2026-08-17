#pragma once

#include <Arduino.h>

#include "grid_layout.h"

// ---------------------------------------------------------------------------
// Which language the clock speaks, and who decided that
// ---------------------------------------------------------------------------
// The front plate is physical: a clock with a German plate showing Dutch words
// spells nonsense. So the language cannot be a factory setting or a guess — it
// is chosen once, by the person who can see the plate.
//
// Stored in NVS namespace "wc_display":
//   lang      ISO code, e.g. "nl" / "de"
//   dialect   ClockDialect::id, e.g. "nl" / "de-nord" / "de-sued"
//   lang_src  who set it — see Source below
//
// The source is what a later phase gates the display on ("has anyone actually
// chosen?"), and what the heartbeat reports so the fleet can be checked before
// that gate is ever armed. It is deliberately not a boolean: "nobody chose,
// this is the build default" and "a migration decided on the customer's behalf"
// are different situations, and only the fleet numbers can tell them apart.
namespace LanguageSettings {

enum class Source : uint8_t {
  Default,   // nothing stored — the build's first variant is rendering
  User,      // a person chose, via setup or the API
  Migrated   // pinned by the field migration, see pinExistingDeviceIfNeeded()
};

// Reads NVS and applies the stored language + dialect to grid_layout. Call
// once at boot, after SettingsMigration and before anything renders.
void begin();

// What is rendering right now (delegates to grid_layout).
const char* activeLanguage();
const char* activeDialect();

// What is in NVS. Differs from active() only between a setLanguage() call and
// the reboot that carries it out.
const char* storedLanguage();
const char* storedDialect();

Source source();
const char* sourceName();  // "default" | "user" | "migrated"

// True once anyone has made a choice. Fase 1b gates the display on this; in
// 1a nothing calls it except the API and the tests.
bool isSetupComplete();

// True when a stored language is waiting for a reboot to take effect.
bool rebootRequired();

// Persist a language choice (lang_src becomes "user"). Does not switch the
// running variant — that changes the letter grid, the word table and the LED
// counts under a live render loop, so the caller reboots instead. False if the
// code is not compiled into this build.
bool setLanguage(const char* code);

// Persist and immediately apply a dialect. Only the phrase table changes, so
// this is safe while running. False if the id is not one of the active
// variant's dialects.
bool setDialect(const char* id);

// One-shot field migration. Every clock in the field predates the language
// choice and speaks the build's default language; leaving them on
// Source::Default would mean a future display gate blanks a clock that has
// been telling the time correctly for a year.
//
// Must be called from SettingsMigration::migrateIfNeeded() *before* it writes
// wc_system/migrated_v2 — that key is this function's main evidence, and it is
// set at the end of the very first boot of any per-device firmware, new chips
// included. See the ordering note in the implementation.
void pinExistingDeviceIfNeeded();

}  // namespace LanguageSettings
