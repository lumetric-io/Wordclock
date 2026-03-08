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
/** Current highest-priority LED event (for dashboard/API). */
LedEvent ledEventGetCurrent(void);
/** True if any LED event is active (minute LEDs should not show time when this is true and they are used for events). */
bool ledEventIsActive(void);