#pragma once

// Include product config first so it can override defaults
#ifdef PRODUCT_CONFIG_HEADER
#include PRODUCT_CONFIG_HEADER
#elif defined(__has_include)
#if __has_include("product_config.h")
#include "product_config.h"
#endif
#endif

// Log buffer and default log level (can be overridden by product_config.h)
#ifndef DEFAULT_LOG_LEVEL
#define DEFAULT_LOG_LEVEL LOG_LEVEL_ERROR
#endif
#ifndef LOG_BUFFER_SIZE
#define LOG_BUFFER_SIZE 50  // Reduced from 150 to save flash space
#endif

// Default update channel (can be overridden by product_config.h)
#ifndef DEFAULT_UPDATE_CHANNEL
#define DEFAULT_UPDATE_CHANNEL "stable"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "26.2.2"
#endif
#ifndef UI_VERSION
#define UI_VERSION "26.2.2"
#endif
#ifndef PRODUCT_ID
#define PRODUCT_ID "wordclock-legacy"
#endif

#ifndef SUPPORT_OTA_V2
#define SUPPORT_OTA_V2 0
#endif

#ifndef OTA_ENABLED
#define OTA_ENABLED 1
#endif

#ifndef OTA2_NO_CACHE_HEADERS
#define OTA2_NO_CACHE_HEADERS 1
#endif

#ifndef BLE_PROVISIONING_ENABLED
#define BLE_PROVISIONING_ENABLED 0
#endif

#ifndef BLE_PROVISIONING_TIMEOUT_SEC
#define BLE_PROVISIONING_TIMEOUT_SEC 15
#endif

#ifndef BLE_PROVISIONING_DISABLE_TIMEOUT
#define BLE_PROVISIONING_DISABLE_TIMEOUT 0
#endif

#ifndef BLE_DEVICE_NAME_PREFIX
#define BLE_DEVICE_NAME_PREFIX PRODUCT_ID "-"
#endif

#ifndef BLE_PASSKEY_DISPLAY_ENABLED
#define BLE_PASSKEY_DISPLAY_ENABLED 0
#endif

#ifndef BLE_PASSKEY_ON_MS
#define BLE_PASSKEY_ON_MS 700
#endif

#ifndef BLE_PASSKEY_OFF_MS
#define BLE_PASSKEY_OFF_MS 300
#endif

#ifndef SUPPORT_MINUTE_LEDS
#define SUPPORT_MINUTE_LEDS 1
#endif

#ifndef SETUP_BLINK_LED_COUNT
#define SETUP_BLINK_LED_COUNT 0
#endif

#ifndef SETUP_BLINK_ENABLED
#define SETUP_BLINK_ENABLED 1
#endif

#ifndef WIFI_MANAGER_ENABLED
#define WIFI_MANAGER_ENABLED 1
#endif

#ifndef UPDATE_UPLOAD_ENABLED
#define UPDATE_UPLOAD_ENABLED 1
#endif

#ifndef SETUP_ASSUME_DONE_IF_LEGACY_CONFIG
#define SETUP_ASSUME_DONE_IF_LEGACY_CONFIG 0
#endif

#ifndef DATA_PIN
#define DATA_PIN 4
#endif
#ifndef LOGO_DATA_PIN
#define LOGO_DATA_PIN DATA_PIN
#endif
#define DEFAULT_BRIGHTNESS 5

#define CLOCK_NAME "Wordclock"
#define AP_NAME "Wordclock_AP"
#define OTA_PORT 3232

// Time
#define TZ_INFO "CET-1CEST,M3.5.0/2,M10.5.0/3"
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"

// Logging

#define SERIAL_BAUDRATE 115200
#define WIFI_CONFIG_PORTAL_TIMEOUT 300 // seconds
#define WIFI_CONNECT_MAX_RETRIES 20
#define WIFI_CONNECT_RETRY_DELAY_MS 500
#define MDNS_HOSTNAME "wordclock"
#define MDNS_START_DELAY_MS 1000
#define TIME_SYNC_TIMEOUT_MS 15000

#define OTA_UPDATE_COMPLETE_DELAY_MS 1000
#define EEPROM_WRITE_DELAY_MS 500

#define DAILY_FIRMWARE_CHECK_HOUR 2
#define DAILY_FIRMWARE_CHECK_MINUTE 0
#define DAILY_FIRMWARE_CHECK_INTERVAL_SEC 3600

constexpr unsigned long SWEEP_STEP_MS = 20;
constexpr unsigned long WORD_SEQUENCE_STEP_MS = 1000;
constexpr unsigned long WORD_SEQUENCE_HOLD_MS = 1000;
