#pragma once

#include "led_state.h"
#include "display_settings.h"
#include "grid_layout.h"
#include "log.h"
#if defined(PRODUCT_VARIANT_LOGO)
#include "logo_leds.h"
#endif

// Initialize LED and display settings
// This function initializes the LED state and display settings from persistent storage.
// Ensures the clock starts up with correct color and brightness.
inline void initDisplay() {
    ledState.begin();
#if defined(PRODUCT_VARIANT_LOGO)
    logoLeds.begin();
#endif
    displaySettings.begin();
    // Grid is compile-time only — log which variant this build ships with so
    // it shows up alongside firmware/UI versions in the boot log.
    const GridVariantInfo* info = getGridVariantInfo(getActiveGridVariant());
    if (info) {
        logInfo(String("🧩 Grid variant: ") + info->label + " (" + info->key + ")");
    }
    logInfo("🟢 LED and display settings initialized");
}
