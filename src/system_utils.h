#pragma once

// System utility functions for safe operations

/**
 * @brief Flush all settings to persistent storage
 * @note Ensures all pending changes are written to flash before critical operations
 */
void flushAllSettings();

/**
 * @brief Safely restart the ESP32
 * @note Flushes all settings to persistent storage before restarting
 * Call this instead of ESP.restart() to prevent data loss
 */
void safeRestart();

