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
struct BootstrapProduct {
  const char* id;
  const char* label;
  const char* description;
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
