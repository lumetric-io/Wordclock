#pragma once

#include <stdint.h>

enum class BleProvisioningReason : uint8_t {
  FirstBootNoCreds = 0,
  WiFiUnavailableAtBoot = 1,
  ManualTrigger = 2,
  Unknown = 255,
};

void initBleProvisioning();
void processBleProvisioning();
void startBleProvisioning(BleProvisioningReason reason);
void stopBleProvisioning();
bool isBleProvisioningActive();
bool takeBleProvisioningTimeout();
const char* getBleProvisioningState();
