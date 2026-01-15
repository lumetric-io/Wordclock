#ifndef MOCK_MQTT_H
#define MOCK_MQTT_H

// Mock MQTT publishing for tests
inline void mqtt_publish_state(bool force) {
    // Silent in tests
    (void)force;
}

#endif // MOCK_MQTT_H

