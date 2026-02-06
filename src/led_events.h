#pragma once

#include <Arduino.h>

enum class LedEvent : uint8_t {
  FirmwareCheck,
  FirmwareAvailable,
  FirmwareDownloading,
  FirmwareApplying,
  NtpFailed,
  MqttDisconnected,
  BleProvisioning,
  WifiManagerPortal,
};

void ledEventStart(LedEvent event);
void ledEventStop(LedEvent event);
void ledEventPulse(LedEvent event);
bool ledEventsTick(unsigned long nowMs);
