#pragma once

#include <Arduino.h>

// Heartbeat interval (1 hour in milliseconds)
#define HEARTBEAT_INTERVAL_MS (60 * 60 * 1000UL)

// Startup delay before first heartbeat (30 seconds)
#define HEARTBEAT_STARTUP_DELAY_MS (30 * 1000UL)

/**
 * Initialize heartbeat module.
 * Call once during setup after WiFi and device identity are ready.
 */
void initHeartbeat();

/**
 * Process heartbeat in main loop.
 * Sends heartbeat to fleet API at configured interval.
 * Timing: executes at :30 seconds of the minute to avoid LED updates.
 * 
 * @param nowMs Current millis() value
 */
void processHeartbeat(unsigned long nowMs);

/**
 * Trigger an immediate heartbeat on next processHeartbeat() call.
 * Use after WiFi reconnect or other significant events.
 */
void triggerHeartbeat();

/**
 * Send heartbeat to fleet API.
 * Called internally by processHeartbeat(), but can be called directly if needed.
 * 
 * @return true if heartbeat was sent successfully
 */
bool sendHeartbeat();
