#pragma once

#define PRODUCT_ID "nextgen-logo-105x105"
#define FIRMWARE_VERSION "nextgen-logo-105x105-26.08.20-dev.6"
#define UI_VERSION "ui-nextgen-logo-105x105-26.08.20-dev.6"
#define PRODUCT_VARIANT_LOGO 1
#define DATA_PIN 4
#define LOGO_DATA_PIN 18
// Clock string is split across two data lines to fix tail corruption:
// segment A = logical indices 0..243 on DATA_PIN, segment B = 244..end on
// CLOCK_DATA_PIN_2. Each segment is power-injected at its head. The split value
// is the count of LEDs on DATA_PIN (== 0-based logical index of the first LED
// on CLOCK_DATA_PIN_2): physical cut is between LED 243 and 244. See
// src/led_segments.* and buildSegments().
#define CLOCK_DATA_PIN_2 6
#define CLOCK_SEGMENT_SPLIT 244
// Clock brightness is uncapped (255). The earlier 200 cap mitigated the
// tail-corruption brownout; the split data line + split 5V injection now handle
// the power budget, so full brightness is restored.
#define MAX_BRIGHTNESS 255
#define SUPPORT_OTA_V2 1
#define BLE_PROVISIONING_ENABLED 0
#define WIFI_MANAGER_ENABLED 1
#define LED_STATUS_EVENTS_ENABLED 0
