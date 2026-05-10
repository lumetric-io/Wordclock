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
// it holds one entry per channel that has a non-null target, including the
// firmware version the channel currently points at (so UIs can preview what
// would actually get installed).
struct ChannelTarget {
  String name;        // "stable" / "early" / "develop"
  String fwVersion;   // channel.json → target.version (may be empty)
};
bool listAvailableChannels(const String& productId, std::vector<ChannelTarget>& out);

// Bootstrap-only: fetch the stable channel for `nextgen-bootstrap`, compare
// against the running FIRMWARE_VERSION, and install if newer. On success the
// device reboots inside installProductFirmware() and never returns. Return
// values reflect the up-front check only:
//   - returns false if no update was needed (already latest) or the check
//     itself failed (network / manifest error). `outUpToDate` distinguishes
//     the two: true means up-to-date, false means error.
// Compiled only into the bootstrap firmware (per the WORDCLOCK_BOOTSTRAP gate
// in the implementation file).
bool checkForBootstrapSelfUpdate(bool& outUpToDate, String& outRemoteVersion);

#if SUPPORT_OTA_V2 == 0
void syncFilesFromManifest();
void syncUiFilesFromConfiguredVersion();
#endif
