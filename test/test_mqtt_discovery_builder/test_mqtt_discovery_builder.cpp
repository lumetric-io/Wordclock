#include <gtest/gtest.h>
#include <ArduinoJson.h>
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_log.h"
#include "../mocks/mock_log.cpp"  // Include implementation
#include "../mocks/mock_pubsubclient.h"

// Ensure mock log.h is used instead of real log.h
// Define this before including builder code to prevent real log.h from defining LogLevel
#define LOG_H
#include "../mocks/log.h"

// Mock PubSubClient.h before including builder
// The test/mocks directory is in the include path, so our mocks will be found first

// Include production code
#include "../../src/mqtt_discovery_builder.h"
#include "../../src/mqtt_discovery_builder.cpp"

class MqttDiscoveryBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockMqtt.clear();
    }
    
    void TearDown() override {
        mockMqtt.clear();
    }
    
    PubSubClient mockMqtt;
    String discoveryPrefix = "homeassistant";
    String nodeId = "test123";
    String baseTopic = "wordclock/test";
    String availTopic = "wordclock/test/availability";
    
    MqttDiscoveryBuilder createBuilder() {
        return MqttDiscoveryBuilder(mockMqtt, discoveryPrefix, nodeId, baseTopic, availTopic);
    }
    
    bool jsonContains(const String& json, const String& key, const String& value) {
        JsonDocument doc;
        std::string jsonStr = json.c_str();
        DeserializationError err = deserializeJson(doc, jsonStr);
        if (err) return false;
        if (!doc[key].is<const char*>()) return false;
        return strcmp(doc[key].as<const char*>(), value.c_str()) == 0;
    }
    
    bool jsonContainsKey(const String& json, const String& key) {
        JsonDocument doc;
        std::string jsonStr = json.c_str();
        DeserializationError err = deserializeJson(doc, jsonStr);
        if (err) return false;
        return doc[key].is<const char*>() || doc[key].is<int>() || doc[key].is<bool>();
    }
};

// Constructor and Setup Tests
TEST_F(MqttDiscoveryBuilderTest, Constructor_InitializesCorrectly) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "TestModel", "TestMfg", "1.0");
    builder.addSwitch("Test Switch", nodeId + "_switch", "state/topic", "cmd/topic");
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    ASSERT_EQ(1024, mockMqtt.getBufferSize());
}

TEST_F(MqttDiscoveryBuilderTest, SetDeviceInfo_StoresInformation) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("My Clock", "ModelX", "ManufacturerY", "2.0");
    builder.addSwitch("Test", nodeId + "_test", "state", "cmd");
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/switch/test123_test/config");
    ASSERT_TRUE(jsonContains(payload, "name", "Test"));
    
    // Check device info is in the payload
    JsonDocument doc;
    std::string payloadStr = payload.c_str();
    deserializeJson(doc, payloadStr);
    JsonObject dev = doc["dev"];
    ASSERT_STREQ("test123", dev["ids"][0].as<const char*>());
    ASSERT_STREQ("My Clock", dev["name"].as<const char*>());
    ASSERT_STREQ("ModelX", dev["mdl"].as<const char*>());
    ASSERT_STREQ("ManufacturerY", dev["mf"].as<const char*>());
    ASSERT_STREQ("2.0", dev["sw"].as<const char*>());
}

// Light Entity Tests
TEST_F(MqttDiscoveryBuilderTest, AddLight_CreatesLightEntity) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addLight("light/state", "light/set");
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    ASSERT_TRUE(mockMqtt.wasPublished("homeassistant/light/test123_light/config"));
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/light/test123_light/config");
    ASSERT_TRUE(jsonContains(payload, "schema", "json"));
    ASSERT_TRUE(jsonContains(payload, "stat_t", "light/state"));
    ASSERT_TRUE(jsonContains(payload, "cmd_t", "light/set"));
    ASSERT_TRUE(jsonContainsKey(payload, "brightness"));
    ASSERT_TRUE(jsonContainsKey(payload, "rgb"));
}

// Switch Entity Tests
TEST_F(MqttDiscoveryBuilderTest, AddSwitch_CreatesSwitchEntity) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addSwitch("My Switch", nodeId + "_myswitch", "switch/state", "switch/set");
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    ASSERT_TRUE(mockMqtt.wasPublished("homeassistant/switch/test123_myswitch/config"));
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/switch/test123_myswitch/config");
    ASSERT_TRUE(jsonContains(payload, "name", "My Switch"));
    ASSERT_TRUE(jsonContains(payload, "uniq_id", "test123_myswitch"));
    ASSERT_TRUE(jsonContains(payload, "stat_t", "switch/state"));
    ASSERT_TRUE(jsonContains(payload, "cmd_t", "switch/set"));
    ASSERT_TRUE(jsonContains(payload, "pl_on", "ON"));
    ASSERT_TRUE(jsonContains(payload, "pl_off", "OFF"));
}

// Number Entity Tests
TEST_F(MqttDiscoveryBuilderTest, AddNumber_CreatesNumberEntity) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addNumber("Brightness", nodeId + "_brightness", 
                     "brightness/state", "brightness/set",
                     0, 255, 1, "%");
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/number/test123_brightness/config");
    ASSERT_TRUE(jsonContains(payload, "name", "Brightness"));
    ASSERT_TRUE(jsonContains(payload, "unit_of_meas", "%"));
    
    JsonDocument doc;
    std::string payloadStr = payload.c_str();
    deserializeJson(doc, payloadStr);
    ASSERT_EQ(0, doc["min"].as<int>());
    ASSERT_EQ(255, doc["max"].as<int>());
    ASSERT_EQ(1, doc["step"].as<int>());
}

TEST_F(MqttDiscoveryBuilderTest, AddNumber_WithoutUnit_OmitsUnitField) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addNumber("Count", nodeId + "_count", "count/state", "count/set", 0, 100);
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/number/test123_count/config");
    ASSERT_FALSE(jsonContainsKey(payload, "unit_of_meas"));
}

// Select Entity Tests
TEST_F(MqttDiscoveryBuilderTest, AddSelect_CreatesSelectEntity) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addSelect("Mode", nodeId + "_mode", "mode/state", "mode/set",
                     {"AUTO", "MANUAL", "OFF"});
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/select/test123_mode/config");
    JsonDocument doc;
    std::string payloadStr = payload.c_str();
    deserializeJson(doc, payloadStr);
    
    JsonArray options = doc["options"];
    ASSERT_EQ(3, options.size());
    ASSERT_STREQ("AUTO", options[0].as<const char*>());
    ASSERT_STREQ("MANUAL", options[1].as<const char*>());
    ASSERT_STREQ("OFF", options[2].as<const char*>());
}

// Binary Sensor Tests
TEST_F(MqttDiscoveryBuilderTest, AddBinarySensor_CreatesBinarySensorEntity) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addBinarySensor("Active", nodeId + "_active", "active/state");
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/binary_sensor/test123_active/config");
    ASSERT_TRUE(jsonContains(payload, "name", "Active"));
    ASSERT_TRUE(jsonContains(payload, "stat_t", "active/state"));
    ASSERT_TRUE(jsonContains(payload, "pl_on", "ON"));
    ASSERT_TRUE(jsonContains(payload, "pl_off", "OFF"));
}

TEST_F(MqttDiscoveryBuilderTest, AddBinarySensor_WithDeviceClass_IncludesDeviceClass) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addBinarySensor("Motion", nodeId + "_motion", "motion/state", "motion");
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/binary_sensor/test123_motion/config");
    ASSERT_TRUE(jsonContains(payload, "dev_cla", "motion"));
}

// Button Entity Tests
TEST_F(MqttDiscoveryBuilderTest, AddButton_CreatesButtonEntity) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addButton("Restart", nodeId + "_restart", "restart/cmd");
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/button/test123_restart/config");
    ASSERT_TRUE(jsonContains(payload, "name", "Restart"));
    ASSERT_TRUE(jsonContains(payload, "cmd_t", "restart/cmd"));
}

TEST_F(MqttDiscoveryBuilderTest, AddButton_WithDeviceClass_IncludesDeviceClass) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addButton("Update", nodeId + "_update", "update/cmd", "update");
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/button/test123_update/config");
    ASSERT_TRUE(jsonContains(payload, "dev_cla", "update"));
}

// Sensor Entity Tests
TEST_F(MqttDiscoveryBuilderTest, AddSensor_CreatesSensorEntity) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addSensor("Temperature", nodeId + "_temp", "temp/state", "°C", "temperature");
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/sensor/test123_temp/config");
    ASSERT_TRUE(jsonContains(payload, "name", "Temperature"));
    ASSERT_TRUE(jsonContains(payload, "stat_t", "temp/state"));
    ASSERT_TRUE(jsonContains(payload, "unit_of_meas", "°C"));
    ASSERT_TRUE(jsonContains(payload, "dev_cla", "temperature"));
}

TEST_F(MqttDiscoveryBuilderTest, AddSensor_WithStateClass_IncludesStateClass) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addSensor("RSSI", nodeId + "_rssi", "rssi/state", "dBm", "signal_strength", "measurement");
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/sensor/test123_rssi/config");
    ASSERT_TRUE(jsonContains(payload, "stat_cla", "measurement"));
}

// Text Entity Tests
TEST_F(MqttDiscoveryBuilderTest, AddText_CreatesTextEntity) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addText("Name", nodeId + "_name", "name/state", "name/set", 0, 50);
    
    int count = builder.publish();
    ASSERT_EQ(1, count);
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/text/test123_name/config");
    ASSERT_TRUE(jsonContains(payload, "name", "Name"));
    ASSERT_TRUE(jsonContains(payload, "stat_t", "name/state"));
    ASSERT_TRUE(jsonContains(payload, "cmd_t", "name/set"));
    ASSERT_TRUE(jsonContains(payload, "mode", "text"));
    
    JsonDocument doc;
    std::string payloadStr = payload.c_str();
    deserializeJson(doc, payloadStr);
    ASSERT_EQ(0, doc["min"].as<int>());
    ASSERT_EQ(50, doc["max"].as<int>());
}

TEST_F(MqttDiscoveryBuilderTest, AddText_WithPattern_IncludesPattern) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addText("Time", nodeId + "_time", "time/state", "time/set",
                   5, 5, "^([01][0-9]|2[0-3]):[0-5][0-9]$");
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/text/test123_time/config");
    ASSERT_TRUE(jsonContains(payload, "pattern", "^([01][0-9]|2[0-3]):[0-5][0-9]$"));
}

// Availability Tests
TEST_F(MqttDiscoveryBuilderTest, AllEntities_IncludeAvailability) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addSwitch("Test", nodeId + "_test", "state", "cmd");
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/switch/test123_test/config");
    ASSERT_TRUE(jsonContains(payload, "avty_t", availTopic));
    ASSERT_TRUE(jsonContains(payload, "pl_avail", "online"));
    ASSERT_TRUE(jsonContains(payload, "pl_not_avail", "offline"));
}

// Multiple Entities Tests
TEST_F(MqttDiscoveryBuilderTest, MultipleEntities_AllPublished) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    
    builder.addSwitch("Switch 1", nodeId + "_sw1", "sw1/state", "sw1/set");
    builder.addSwitch("Switch 2", nodeId + "_sw2", "sw2/state", "sw2/set");
    builder.addButton("Button 1", nodeId + "_btn1", "btn1/cmd");
    builder.addSensor("Sensor 1", nodeId + "_sens1", "sens1/state");
    
    int count = builder.publish();
    ASSERT_EQ(4, count);
    ASSERT_EQ(4, mockMqtt.getPublishedCount());
    
    ASSERT_TRUE(mockMqtt.wasPublished("homeassistant/switch/test123_sw1/config"));
    ASSERT_TRUE(mockMqtt.wasPublished("homeassistant/switch/test123_sw2/config"));
    ASSERT_TRUE(mockMqtt.wasPublished("homeassistant/button/test123_btn1/config"));
    ASSERT_TRUE(mockMqtt.wasPublished("homeassistant/sensor/test123_sens1/config"));
}

// Clear Tests
TEST_F(MqttDiscoveryBuilderTest, Clear_RemovesAllEntities) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addSwitch("Test 1", nodeId + "_test1", "state1", "cmd1");
    builder.addSwitch("Test 2", nodeId + "_test2", "state2", "cmd2");
    
    builder.clear();
    int count = builder.publish();
    
    ASSERT_EQ(0, count);
    ASSERT_EQ(0, mockMqtt.getPublishedCount());
}

// Edge Cases
TEST_F(MqttDiscoveryBuilderTest, EmptyDeviceInfo_StillWorks) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("", "", "", "");
    builder.addSwitch("Test", nodeId + "_test", "state", "cmd");
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/switch/test123_test/config");
    JsonDocument doc;
    std::string payloadStr = payload.c_str();
    deserializeJson(doc, payloadStr);
    JsonObject dev = doc["dev"];
    ASSERT_STREQ("", dev["name"].as<const char*>());
}

TEST_F(MqttDiscoveryBuilderTest, CustomDiscoveryPrefix_UsedInTopics) {
    String customPrefix = "custom_ha";
    MqttDiscoveryBuilder builder(mockMqtt, customPrefix, nodeId, baseTopic, availTopic);
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addSwitch("Test", nodeId + "_test", "state", "cmd");
    
    builder.publish();
    
    ASSERT_TRUE(mockMqtt.wasPublished("custom_ha/switch/test123_test/config"));
}

TEST_F(MqttDiscoveryBuilderTest, Number_WithMode_SetsMode) {
    MqttDiscoveryBuilder builder = createBuilder();
    builder.setDeviceInfo("Test Clock", "Model", "Mfg", "1.0");
    builder.addNumber("Test", nodeId + "_test", "state", "cmd", 0, 100, 1, "", "box");
    
    builder.publish();
    
    String payload = mockMqtt.getPublishedPayload("homeassistant/number/test123_test/config");
    ASSERT_TRUE(jsonContains(payload, "mode", "box"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

