#include "mqtt_discovery_builder.h"
#include "log.h"
#include <utility>

MqttDiscoveryBuilder::MqttDiscoveryBuilder(PubSubClient& mqtt,
                                           const String& discoveryPrefix,
                                           const String& nodeId,
                                           const String& baseTopic,
                                           const String& availTopic)
    : mqtt_(mqtt), 
      discoveryPrefix_(discoveryPrefix),
      nodeId_(nodeId),
      baseTopic_(baseTopic),
      availTopic_(availTopic) {
}

void MqttDiscoveryBuilder::setDeviceInfo(const String& name,
                                         const String& model,
                                         const String& manufacturer,
                                         const String& swVersion) {
    deviceName_ = name;
    deviceModel_ = model;
    deviceManufacturer_ = manufacturer;
    deviceSwVersion_ = swVersion;
}

void MqttDiscoveryBuilder::addDeviceInfo(JsonDocument& doc) {
    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"].add(nodeId_);
    dev["name"] = deviceName_;
    if (deviceModel_.length() > 0) {
        dev["mdl"] = deviceModel_;
    }
    if (deviceManufacturer_.length() > 0) {
        dev["mf"] = deviceManufacturer_;
    }
    if (deviceSwVersion_.length() > 0) {
        dev["sw"] = deviceSwVersion_;
    }
}

void MqttDiscoveryBuilder::addAvailability(JsonDocument& doc) {
    doc["avty_t"] = availTopic_;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";
}

void MqttDiscoveryBuilder::addLight(const String& stateTopic, const String& cmdTopic) {
    Entity entity;
    entity.component = "light";
    entity.objectId = nodeId_ + "_light";
    
    entity.config["name"] = deviceName_;
    entity.config["uniq_id"] = entity.objectId;
    entity.config["stat_t"] = stateTopic;
    entity.config["cmd_t"] = cmdTopic;
    entity.config["schema"] = "json";
    entity.config["brightness"] = true;
    entity.config["brightness_scale"] = 255;
    JsonArray modes = entity.config["supported_color_modes"].to<JsonArray>();
    modes.add("rgbw");
    
    addDeviceInfo(entity.config);
    addAvailability(entity.config);
    
    entities_.push_back(std::move(entity));
}

void MqttDiscoveryBuilder::addSwitch(const String& name, const String& uniqueId,
                                     const String& stateTopic, const String& cmdTopic) {
    Entity entity;
    entity.component = "switch";
    entity.objectId = uniqueId;
    
    entity.config["name"] = name;
    entity.config["uniq_id"] = uniqueId;
    entity.config["stat_t"] = stateTopic;
    entity.config["cmd_t"] = cmdTopic;
    entity.config["pl_on"] = "ON";
    entity.config["pl_off"] = "OFF";
    
    addDeviceInfo(entity.config);
    addAvailability(entity.config);
    
    entities_.push_back(std::move(entity));
}

void MqttDiscoveryBuilder::addNumber(const String& name, const String& uniqueId,
                                     const String& stateTopic, const String& cmdTopic,
                                     int min, int max, int step,
                                     const String& unit, const String& mode) {
    Entity entity;
    entity.component = "number";
    entity.objectId = uniqueId;
    
    entity.config["name"] = name;
    entity.config["uniq_id"] = uniqueId;
    entity.config["stat_t"] = stateTopic;
    entity.config["cmd_t"] = cmdTopic;
    entity.config["min"] = min;
    entity.config["max"] = max;
    entity.config["step"] = step;
    entity.config["mode"] = mode;
    
    if (unit.length() > 0) {
        entity.config["unit_of_meas"] = unit;
    }
    
    addDeviceInfo(entity.config);
    addAvailability(entity.config);
    
    entities_.push_back(std::move(entity));
}

void MqttDiscoveryBuilder::addSelect(const String& name, const String& uniqueId,
                                     const String& stateTopic, const String& cmdTopic,
                                     const std::vector<String>& options) {
    Entity entity;
    entity.component = "select";
    entity.objectId = uniqueId;
    
    entity.config["name"] = name;
    entity.config["uniq_id"] = uniqueId;
    entity.config["stat_t"] = stateTopic;
    entity.config["cmd_t"] = cmdTopic;
    
    JsonArray opts = entity.config["options"].to<JsonArray>();
    for (const auto& option : options) {
        opts.add(option);
    }
    
    addDeviceInfo(entity.config);
    addAvailability(entity.config);
    
    entities_.push_back(std::move(entity));
}

void MqttDiscoveryBuilder::addBinarySensor(const String& name, const String& uniqueId,
                                           const String& stateTopic,
                                           const String& deviceClass) {
    Entity entity;
    entity.component = "binary_sensor";
    entity.objectId = uniqueId;
    
    entity.config["name"] = name;
    entity.config["uniq_id"] = uniqueId;
    entity.config["stat_t"] = stateTopic;
    entity.config["pl_on"] = "ON";
    entity.config["pl_off"] = "OFF";
    
    if (deviceClass.length() > 0) {
        entity.config["dev_cla"] = deviceClass;
    }
    
    addDeviceInfo(entity.config);
    addAvailability(entity.config);
    
    entities_.push_back(std::move(entity));
}

void MqttDiscoveryBuilder::addButton(const String& name, const String& uniqueId,
                                     const String& cmdTopic,
                                     const String& deviceClass) {
    Entity entity;
    entity.component = "button";
    entity.objectId = uniqueId;
    
    entity.config["name"] = name;
    entity.config["uniq_id"] = uniqueId;
    entity.config["cmd_t"] = cmdTopic;
    
    if (deviceClass.length() > 0) {
        entity.config["dev_cla"] = deviceClass;
    }
    
    addDeviceInfo(entity.config);
    addAvailability(entity.config);
    
    entities_.push_back(std::move(entity));
}

void MqttDiscoveryBuilder::addSensor(const String& name, const String& uniqueId,
                                     const String& stateTopic,
                                     const String& unit,
                                     const String& deviceClass,
                                     const String& stateClass) {
    Entity entity;
    entity.component = "sensor";
    entity.objectId = uniqueId;
    
    entity.config["name"] = name;
    entity.config["uniq_id"] = uniqueId;
    entity.config["stat_t"] = stateTopic;
    
    if (unit.length() > 0) {
        entity.config["unit_of_meas"] = unit;
    }
    if (deviceClass.length() > 0) {
        entity.config["dev_cla"] = deviceClass;
    }
    if (stateClass.length() > 0) {
        entity.config["stat_cla"] = stateClass;
    }
    
    addDeviceInfo(entity.config);
    addAvailability(entity.config);
    
    entities_.push_back(std::move(entity));
}

void MqttDiscoveryBuilder::addText(const String& name, const String& uniqueId,
                                   const String& stateTopic, const String& cmdTopic,
                                   int minLen, int maxLen,
                                   const String& pattern, const String& mode) {
    Entity entity;
    entity.component = "text";
    entity.objectId = uniqueId;
    
    entity.config["name"] = name;
    entity.config["uniq_id"] = uniqueId;
    entity.config["stat_t"] = stateTopic;
    entity.config["cmd_t"] = cmdTopic;
    entity.config["min"] = minLen;
    entity.config["max"] = maxLen;
    entity.config["mode"] = mode;
    
    if (pattern.length() > 0) {
        entity.config["pattern"] = pattern;
    }
    
    addDeviceInfo(entity.config);
    addAvailability(entity.config);
    
    entities_.push_back(std::move(entity));
}

void MqttDiscoveryBuilder::publishEntity(const Entity& entity) {
    String topic = discoveryPrefix_ + "/" + entity.component + "/" + 
                   entity.objectId + "/config";
    
    String output;
    serializeJson(entity.config, output);
    
    if (mqtt_.publish(topic.c_str(), output.c_str(), true)) {
        logDebug(String("Published discovery: ") + entity.component + "/" + entity.objectId);
    } else {
        logWarn(String("Failed to publish: ") + entity.component + "/" + entity.objectId);
    }
}

int MqttDiscoveryBuilder::publish() {
    mqtt_.setBufferSize(1024);
    
    int published = 0;
    for (const auto& entity : entities_) {
        publishEntity(entity);
        published++;
        delay(10);  // Small delay between publishes
    }
    
    logInfo(String("Published ") + published + " discovery entities");
    return published;
}

void MqttDiscoveryBuilder::clear() {
    entities_.clear();
}
