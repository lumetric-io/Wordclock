#pragma once

// nextgen-bootstrap is the first-flash provisioning firmware. Its only job
// is to bring up Wi-Fi, present a product picker, and OTA-install the chosen
// product firmware + fs.bin. It does not drive LEDs, render time, or talk
// to MQTT; most of that code is excluded via build_src_filter.

#define PRODUCT_ID "nextgen-bootstrap"
#define FIRMWARE_VERSION "nextgen-bootstrap-0.1.6"
#define UI_VERSION "ui-nextgen-bootstrap-0.1.6"

// Marker used by gated code in shared sources (ota_updater.cpp,
// system_utils.cpp) to compile out per-device dependencies that
// bootstrap doesn't have (displaySettings, ledState, nightMode, grid_layout).
#define WORDCLOCK_BOOTSTRAP 1

// No physical LED output from the bootstrap. DATA_PIN is still defined
// because shared headers reference it; the value doesn't matter since
// no LED driver is linked.
#define DATA_PIN 4

#define SUPPORT_OTA_V2 1
#define BLE_PROVISIONING_ENABLED 0
#define WIFI_MANAGER_ENABLED 1
#define LED_STATUS_EVENTS_ENABLED 0
#define OTA_ENABLED 1
