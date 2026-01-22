#pragma once

#include <Arduino.h>

String get_device_id();
bool set_device_id(const String& id);
String get_hardware_id();
String get_device_token();
bool set_device_token(const String& token);
bool get_registration_blocked();
bool set_registration_blocked(bool blocked);
