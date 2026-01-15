#ifndef SETTINGS_MIGRATION_H
#define SETTINGS_MIGRATION_H

#include <Preferences.h>
#include "log.h"

class SettingsMigration {
public:
    static void migrateIfNeeded() {
        migrateLogDeleteOnBootDefault();

        Preferences prefs;
        
        // Check if migration already done
        prefs.begin("wc_system", true);
        bool migrated = prefs.getBool("migrated_v2", false);
        prefs.end();
        
        if (migrated) {
            return;  // Already migrated
        }
        
        logInfo("⚙️ Migrating settings to new format...");
        
        migrateLedState();
        migrateDisplaySettings();
        migrateNightMode();
        migrateSetupState();
        migrateLogSettings();
        
        // Mark as migrated
        prefs.begin("wc_system", false);
        prefs.putBool("migrated_v2", true);
        prefs.end();
        
        logInfo("✅ Settings migration complete");
    }
    
private:
    static void migrateLogDeleteOnBootDefault() {
        Preferences prefs;
        prefs.begin("wc_system", false);
        bool done = prefs.getBool("log_del_on_boot_default_v1", false);
        if (done) {
            prefs.end();
            return;
        }
        setLogDeleteOnBoot(true);
        prefs.putBool("log_del_on_boot_default_v1", true);
        prefs.end();
        logInfo("  ✓ Log delete-on-boot default enabled");
    }

    static void migrateLedState() {
        Preferences oldPrefs, newPrefs;
        
        if (!oldPrefs.begin("led", true)) {
            return;  // No old data
        }
        
        // Check if old namespace has any data
        if (!oldPrefs.isKey("r") && !oldPrefs.isKey("br")) {
            oldPrefs.end();
            return;  // No data to migrate
        }
        
        newPrefs.begin("wc_led", false);
        newPrefs.putUChar("r", oldPrefs.getUChar("r", 0));
        newPrefs.putUChar("g", oldPrefs.getUChar("g", 0));
        newPrefs.putUChar("b", oldPrefs.getUChar("b", 0));
        newPrefs.putUChar("w", oldPrefs.getUChar("w", 255));
        newPrefs.putUChar("br", oldPrefs.getUChar("br", 64));
        newPrefs.end();
        oldPrefs.end();
        
        // Clear old namespace
        oldPrefs.begin("led", false);
        oldPrefs.clear();
        oldPrefs.end();
        
        logInfo("  ✓ LED state migrated");
    }
    
    static void migrateDisplaySettings() {
        Preferences oldPrefs, newPrefs;
        
        if (!oldPrefs.begin("display", true)) {
            return;  // No old data
        }
        
        // Check if old namespace has any data
        if (!oldPrefs.isKey("his_sec") && !oldPrefs.isKey("sell_on") && !oldPrefs.isKey("anim_on")) {
            oldPrefs.end();
            return;  // No data to migrate
        }
        
        newPrefs.begin("wc_display", false);
        newPrefs.putUShort("his_sec", oldPrefs.getUShort("his_sec", 360));
        newPrefs.putBool("sell_on", oldPrefs.getBool("sell_on", false));
        newPrefs.putBool("anim_on", oldPrefs.getBool("anim_on", false));
        newPrefs.putUChar("anim_mode", oldPrefs.getUChar("anim_mode", 0));
        newPrefs.putBool("auto_upd", oldPrefs.getBool("auto_upd", true));
        
        // Migrate update channel if it exists
        if (oldPrefs.isKey("upd_ch")) {
            newPrefs.putString("upd_ch", oldPrefs.getString("upd_ch", "stable"));
        }
        
        // Migrate grid variant if it exists
        if (oldPrefs.isKey("grid_id")) {
            newPrefs.putUChar("grid_id", oldPrefs.getUChar("grid_id", 0));
        }
        
        newPrefs.end();
        oldPrefs.end();
        
        // Clear old namespace
        oldPrefs.begin("display", false);
        oldPrefs.clear();
        oldPrefs.end();
        
        logInfo("  ✓ Display settings migrated");
    }
    
    static void migrateNightMode() {
        Preferences oldPrefs, newPrefs;
        
        if (!oldPrefs.begin("night", true)) {
            return;  // No old data
        }
        
        // Check if old namespace has any data
        if (!oldPrefs.isKey("enabled")) {
            oldPrefs.end();
            return;  // No data to migrate
        }
        
        newPrefs.begin("wc_night", false);
        newPrefs.putBool("enabled", oldPrefs.getBool("enabled", false));
        newPrefs.putUChar("effect", oldPrefs.getUChar("effect", 1));
        newPrefs.putUChar("dim_pct", oldPrefs.getUChar("dim_pct", 20));
        newPrefs.putUShort("start", oldPrefs.getUShort("start", 22 * 60));
        newPrefs.putUShort("end", oldPrefs.getUShort("end", 6 * 60));
        newPrefs.end();
        oldPrefs.end();
        
        // Clear old namespace
        oldPrefs.begin("night", false);
        oldPrefs.clear();
        oldPrefs.end();
        
        logInfo("  ✓ Night mode migrated");
    }
    
    static void migrateSetupState() {
        Preferences oldPrefs, newPrefs;
        
        if (!oldPrefs.begin("setup", true)) {
            return;  // No old data
        }
        
        // Check if old namespace has any data
        if (!oldPrefs.isKey("done") && !oldPrefs.isKey("ver")) {
            oldPrefs.end();
            return;  // No data to migrate
        }
        
        newPrefs.begin("wc_setup", false);
        newPrefs.putBool("done", oldPrefs.getBool("done", false));
        newPrefs.putUChar("ver", oldPrefs.getUChar("ver", 0));
        newPrefs.end();
        oldPrefs.end();
        
        // Clear old namespace
        oldPrefs.begin("setup", false);
        oldPrefs.clear();
        oldPrefs.end();
        
        logInfo("  ✓ Setup state migrated");
    }
    
    static void migrateLogSettings() {
        Preferences oldPrefs, newPrefs;
        
        if (!oldPrefs.begin("log", true)) {
            return;  // No old data
        }
        
        // Check if old namespace has any data
        if (!oldPrefs.isKey("level") && !oldPrefs.isKey("retention")) {
            oldPrefs.end();
            return;  // No data to migrate
        }
        
        newPrefs.begin("wc_log", false);
        newPrefs.putUChar("level", oldPrefs.getUChar("level", 1));
        newPrefs.putUInt("retention", oldPrefs.getUInt("retention", 1));
        newPrefs.putBool("delOnBoot", oldPrefs.getBool("delOnBoot", false));
        newPrefs.end();
        oldPrefs.end();
        
        // Clear old namespace
        oldPrefs.begin("log", false);
        oldPrefs.clear();
        oldPrefs.end();
        
        logInfo("  ✓ Log settings migrated");
    }
};

#endif // SETTINGS_MIGRATION_H
