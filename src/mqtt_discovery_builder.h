#ifndef MQTT_DISCOVERY_BUILDER_H
#define MQTT_DISCOVERY_BUILDER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <vector>

/**
 * @brief Builder for Home Assistant MQTT Discovery messages
 * 
 * Simplifies creation and publishing of HA discovery configs by:
 * - Handling common device information
 * - Managing topic generation
 * - Providing type-safe entity builders
 * - Batching publications
 */
class MqttDiscoveryBuilder {
public:
    /**
     * @brief Create discovery builder
     * @param mqtt MQTT client reference
     * @param discoveryPrefix HA discovery prefix (default: "homeassistant")
     * @param nodeId Unique device identifier
     * @param baseTopic Base MQTT topic for this device
     * @param availTopic Availability topic
     */
    MqttDiscoveryBuilder(PubSubClient& mqtt, 
                        const String& discoveryPrefix,
                        const String& nodeId,
                        const String& baseTopic,
                        const String& availTopic);
    
    /**
     * @brief Set device information (name, model, manufacturer, etc.)
     */
    void setDeviceInfo(const String& name, 
                      const String& model,
                      const String& manufacturer,
                      const String& swVersion);
    
    // Entity builders
    void addLight(const String& stateTopic, const String& cmdTopic);
    
    void addSwitch(const String& name, const String& uniqueId,
                   const String& stateTopic, const String& cmdTopic);
    
    void addNumber(const String& name, const String& uniqueId,
                   const String& stateTopic, const String& cmdTopic,
                   int min, int max, int step = 1, 
                   const String& unit = "", const String& mode = "auto");
    
    void addSelect(const String& name, const String& uniqueId,
                   const String& stateTopic, const String& cmdTopic,
                   const std::vector<String>& options);
    
    void addBinarySensor(const String& name, const String& uniqueId,
                        const String& stateTopic,
                        const String& deviceClass = "");
    
    void addButton(const String& name, const String& uniqueId,
                   const String& cmdTopic,
                   const String& deviceClass = "");
    
    void addSensor(const String& name, const String& uniqueId,
                   const String& stateTopic,
                   const String& unit = "",
                   const String& deviceClass = "",
                   const String& stateClass = "");
    
    void addText(const String& name, const String& uniqueId,
                 const String& stateTopic, const String& cmdTopic,
                 int minLen = 0, int maxLen = 255, 
                 const String& pattern = "", const String& mode = "text");
    
    /**
     * @brief Publish all configured entities to MQTT
     * @return Number of entities published
     */
    int publish();
    
    /**
     * @brief Clear all entities (for republishing)
     */
    void clear();
    
private:
    struct Entity {
        String component;
        String objectId;
        JsonDocument config;
    };
    
    void addDeviceInfo(JsonDocument& doc);
    void addAvailability(JsonDocument& doc);
    void publishEntity(const Entity& entity);
    
    PubSubClient& mqtt_;
    String discoveryPrefix_;
    String nodeId_;
    String baseTopic_;
    String availTopic_;
    
    String deviceName_;
    String deviceModel_;
    String deviceManufacturer_;
    String deviceSwVersion_;
    
    std::vector<Entity> entities_;
};

#endif // MQTT_DISCOVERY_BUILDER_H

