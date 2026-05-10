#pragma once

#include <stdbool.h>

extern bool g_wifiHadCredentialsAtBoot;

void initNetwork();
void processNetwork();
bool isWiFiConnected();
void resetWiFiSettings();

// True when this boot has no saved Wi-Fi credentials and we're not yet
// connected — i.e. the device is sitting on the WiFiManager portal waiting
// for the operator to configure Wi-Fi for the first time. Heavy work
// (wordclock rendering, periodic STA reconnect scans) should be skipped in
// this mode so the AP stays responsive. Distinct from "lost Wi-Fi
// mid-operation" (had creds, currently disconnected) — that case keeps the
// clock face running and reconnect scans ticking.
bool isInitialSetupMode();
