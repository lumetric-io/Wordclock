#pragma once

#include <Arduino.h>

void mqtt_begin();
void mqtt_loop();
void mqtt_publish_state(bool force = false);
void mqtt_publish_update_status(bool running);
void mqtt_publish_update_status(bool running);

// Apply new settings at runtime: disconnect, update client, reconnect
struct MqttSettings; // fwd
void mqtt_apply_settings(const MqttSettings& s);

// Force MQTT reconnection attempt, clearing abort state
void mqtt_force_reconnect();

// Status helpers for Web UI
bool mqtt_is_connected();
const String& mqtt_last_error();
