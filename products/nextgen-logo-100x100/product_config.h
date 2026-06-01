#pragma once

#define PRODUCT_ID "nextgen-logo-100x100"
#define FIRMWARE_VERSION "nextgen-logo-100x100-26.5.26"
#define UI_VERSION "ui-nextgen-logo-100x100-26.5.26"
#define PRODUCT_VARIANT_LOGO 1
#define DATA_PIN 4
#define LOGO_DATA_PIN 18
// Clock string is split across two data lines to fix tail corruption:
// segment A = logical indices 0..243 on DATA_PIN, segment B = 244..end on
// CLOCK_DATA_PIN_2. Each segment is power-injected at its head. See
// src/led_segments.* and buildSegments().
#define CLOCK_DATA_PIN_2 6
#define CLOCK_SEGMENT_SPLIT 244
#define SUPPORT_OTA_V2 1
#define BLE_PROVISIONING_ENABLED 0
#define WIFI_MANAGER_ENABLED 1
#define LED_STATUS_EVENTS_ENABLED 0
