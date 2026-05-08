#pragma once
// Bootstrap provisioning — first-flash product picker for the
// nextgen-bootstrap firmware. Drives the OTA-install state machine and the
// HTTP API consumed by data/bootstrap.html.

#include <Arduino.h>
#include <WebServer.h>

enum class BootstrapState : uint8_t {
  Idle = 0,
  FetchingChannels,
  DownloadingFirmware,
  DownloadingFs,
  Applying,
  Done,
  Failed,
};

// Hardcoded product table. Order matches the picker UI.
//
// `apName` is the Wi-Fi SSID the per-device firmware will broadcast for its
// own enrollment portal after the OTA reboot. The bootstrap UI surfaces it
// to the operator as a positive completion signal: once that SSID appears
// on their phone, the new firmware has booted. The bootstrap can't probe
// the device directly because per-device firmware does its own fresh Wi-Fi
// enrollment (no credential handoff from bootstrap, by design).
struct BootstrapProduct {
  const char* id;
  const char* label;
  const char* description;
  const char* apName;
};

extern const BootstrapProduct kBootstrapProducts[];
extern const size_t kBootstrapProductCount;

// State accessors (read-only snapshot of the state machine).
BootstrapState bootstrapState();
String bootstrapStatusMessage();
String bootstrapSelectedProduct();
String bootstrapSelectedChannel();

// Register all bootstrap HTTP routes on the global WebServer instance.
// Must be called after WebServer::begin().
void registerBootstrapRoutes(WebServer& server);
