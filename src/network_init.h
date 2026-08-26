#pragma once

#include <Arduino.h>
#include <stdbool.h>

extern bool g_wifiHadCredentialsAtBoot;

void initNetwork();
void processNetwork();
bool isWiFiConnected();
void resetWiFiSettings();

// Save station credentials for the next boot without joining the network now.
// Returns false on a malformed SSID/password or if the driver rejects them.
bool storeWifiCredentials(const String& ssid, const String& password);

// SSID this clock will try on its next boot, or "" when it has none of its
// own. Never reports the factory network — see the definition.
String getStoredWifiSsid();

// True when this boot has no saved Wi-Fi credentials and we're not yet
// connected — i.e. the device is sitting on the WiFiManager portal waiting
// for the operator to configure Wi-Fi for the first time. Heavy work
// (wordclock rendering, periodic STA reconnect scans) should be skipped in
// this mode so the AP stays responsive. Distinct from "lost Wi-Fi
// mid-operation" (had creds, currently disconnected) — that case keeps the
// clock face running and reconnect scans ticking.
bool isInitialSetupMode();
