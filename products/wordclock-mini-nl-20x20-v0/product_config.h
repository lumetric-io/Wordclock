#pragma once

#define PRODUCT_ID "wordclock-mini-nl-20x20-v0"
#define FIRMWARE_VERSION "mini-nl-20x20-v0-26.4.26"
#define UI_VERSION "ui-mini-nl-20x20-v0-26.4.26"
#define PRODUCT_VARIANT_MINI 1
#define DATA_PIN 4
#define SETUP_ASSUME_DONE_IF_LEGACY_CONFIG 0
#define SUPPORT_MINUTE_LEDS 0
#define SUPPORT_OTA_V2 1
#define BLE_PROVISIONING_ENABLED 0
#define WIFI_MANAGER_ENABLED 1
#define OTA_ENABLED 0
#define LED_STATUS_EVENTS_ENABLED 1
#define LED_STATUS_EVENT_USE_MINUTE_LEDS 0
#define LED_STATUS_EVENT_LED_COUNT 4
// Corner LEDs of the visible 9x9 grid, shifted +1 vs nl_20x20_v1 because LED 0 is dark on this hardware.
#define LED_STATUS_EVENT_LED_IDS 1,9,97,105

// Log level: LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR
// #define DEFAULT_LOG_LEVEL LOG_LEVEL_ERROR

// Update channel: "stable", "early", "develop"
// #define DEFAULT_UPDATE_CHANNEL "stable"
