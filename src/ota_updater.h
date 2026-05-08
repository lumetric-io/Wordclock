// Public OTA functions (implementation in ota_updater.cpp)
#pragma once

#include <Arduino.h>
#include <vector>

void checkForFirmwareUpdate();
String getUiVersion();

// Bootstrap-only OTA primitives (used by the nextgen-bootstrap firmware on
// first-flash provisioning). No-ops when OTA is disabled at compile time.

// Download fs.bin then firmware.bin for the chosen product/channel and
// reboot. Skips the FIRMWARE_VERSION-comparison gate that the per-device
// OTA loop uses, so it always installs whatever the channel publishes.
// Returns false on error; on success the device reboots and never returns.
bool installProductFirmware(const String& productId, const String& channel);

// Probe the OTA server for which channels (stable/early/develop) currently
// publish a target for the given product. `out` is cleared first; on success
// it holds the names of channels that exist.
bool listAvailableChannels(const String& productId, std::vector<String>& out);

#if SUPPORT_OTA_V2 == 0
void syncFilesFromManifest();
void syncUiFilesFromConfiguredVersion();
#endif
