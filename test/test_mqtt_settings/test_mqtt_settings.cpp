#include <gtest/gtest.h>
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_preferences.h"

// Include production code
#include "../../src/mqtt_settings.h"
#include "../../src/mqtt_settings.cpp"

class MqttSettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        Preferences::reset();
    }
    
    void TearDown() override {
        Preferences::reset();
    }
};

// Load Tests
TEST_F(MqttSettingsTest, Load_NoSettings_ReturnsDefaults) {
    MqttSettings settings;
    bool loaded = mqtt_settings_load(settings);
    
    ASSERT_FALSE(loaded);  // No persisted settings yet
    ASSERT_STREQ("", settings.host.c_str());
    ASSERT_EQ(1883, settings.port);
    ASSERT_STREQ("", settings.user.c_str());
    ASSERT_STREQ("", settings.pass.c_str());
    ASSERT_STREQ("homeassistant", settings.discoveryPrefix.c_str());
    ASSERT_STREQ("wordclock", settings.baseTopic.c_str());
    ASSERT_FALSE(settings.allowAnonymous);
}

// Save Tests
TEST_F(MqttSettingsTest, Save_ValidSettings_Success) {
    MqttSettings settings;
    settings.host = "mqtt.example.com";
    settings.port = 8883;
    settings.user = "testuser";
    settings.pass = "testpass";
    settings.discoveryPrefix = "ha";
    settings.baseTopic = "myclock";
    settings.allowAnonymous = true;
    
    ASSERT_TRUE(mqtt_settings_save(settings));
}

TEST_F(MqttSettingsTest, Save_EmptyHost_Allowed) {
    MqttSettings settings;
    settings.host = "";
    settings.port = 1883;
    
    ASSERT_TRUE(mqtt_settings_save(settings));
}

// Persistence Tests
TEST_F(MqttSettingsTest, SaveAndLoad_PreservesAllFields) {
    MqttSettings save_settings;
    save_settings.host = "broker.hivemq.com";
    save_settings.port = 8883;
    save_settings.user = "myuser";
    save_settings.pass = "mypassword";
    save_settings.discoveryPrefix = "homeassistant";
    save_settings.baseTopic = "wordclock123";
    save_settings.allowAnonymous = true;
    
    ASSERT_TRUE(mqtt_settings_save(save_settings));
    
    MqttSettings load_settings;
    ASSERT_TRUE(mqtt_settings_load(load_settings));
    
    ASSERT_STREQ(save_settings.host.c_str(), load_settings.host.c_str());
    ASSERT_EQ(save_settings.port, load_settings.port);
    ASSERT_STREQ(save_settings.user.c_str(), load_settings.user.c_str());
    ASSERT_STREQ(save_settings.pass.c_str(), load_settings.pass.c_str());
    ASSERT_STREQ(save_settings.discoveryPrefix.c_str(), load_settings.discoveryPrefix.c_str());
    ASSERT_STREQ(save_settings.baseTopic.c_str(), load_settings.baseTopic.c_str());
    ASSERT_EQ(save_settings.allowAnonymous, load_settings.allowAnonymous);
}

// Port Validation Tests
TEST_F(MqttSettingsTest, SaveAndLoad_StandardPort) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.port = 1883;
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_EQ(1883, loaded.port);
}

TEST_F(MqttSettingsTest, SaveAndLoad_SecurePort) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.port = 8883;
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_EQ(8883, loaded.port);
}

TEST_F(MqttSettingsTest, SaveAndLoad_CustomPort) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.port = 12345;
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_EQ(12345, loaded.port);
}

// Empty Credentials Tests
TEST_F(MqttSettingsTest, SaveAndLoad_EmptyCredentials) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.user = "";
    settings.pass = "";
    settings.allowAnonymous = true;
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_STREQ("", loaded.user.c_str());
    ASSERT_STREQ("", loaded.pass.c_str());
    ASSERT_TRUE(loaded.allowAnonymous);
}

// Discovery Prefix Tests
TEST_F(MqttSettingsTest, SaveAndLoad_CustomDiscoveryPrefix) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.discoveryPrefix = "custom_ha";
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_STREQ("custom_ha", loaded.discoveryPrefix.c_str());
}

TEST_F(MqttSettingsTest, SaveAndLoad_EmptyDiscoveryPrefix) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.discoveryPrefix = "";
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_STREQ("", loaded.discoveryPrefix.c_str());
}

// Base Topic Tests
TEST_F(MqttSettingsTest, SaveAndLoad_CustomBaseTopic) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.baseTopic = "living_room/clock";
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_STREQ("living_room/clock", loaded.baseTopic.c_str());
}

// Anonymous Flag Tests
TEST_F(MqttSettingsTest, SaveAndLoad_AnonymousTrue) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.allowAnonymous = true;
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_TRUE(loaded.allowAnonymous);
}

TEST_F(MqttSettingsTest, SaveAndLoad_AnonymousFalse) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.allowAnonymous = false;
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_FALSE(loaded.allowAnonymous);
}

// Multiple Save Tests
TEST_F(MqttSettingsTest, MultipleSaves_OverwritesPrevious) {
    MqttSettings settings1;
    settings1.host = "broker1.local";
    settings1.port = 1883;
    mqtt_settings_save(settings1);
    
    MqttSettings settings2;
    settings2.host = "broker2.local";
    settings2.port = 8883;
    mqtt_settings_save(settings2);
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    
    ASSERT_STREQ("broker2.local", loaded.host.c_str());
    ASSERT_EQ(8883, loaded.port);
}

// Special Characters Tests
TEST_F(MqttSettingsTest, SaveAndLoad_SpecialCharactersInPassword) {
    MqttSettings settings;
    settings.host = "broker.local";
    settings.user = "user";
    settings.pass = "p@ss!w0rd#123";
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_STREQ("p@ss!w0rd#123", loaded.pass.c_str());
}

TEST_F(MqttSettingsTest, SaveAndLoad_SpecialCharactersInHost) {
    MqttSettings settings;
    settings.host = "mqtt-broker.example.com";
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_STREQ("mqtt-broker.example.com", loaded.host.c_str());
}

// Long String Tests
TEST_F(MqttSettingsTest, SaveAndLoad_LongHostname) {
    MqttSettings settings;
    settings.host = "very-long-hostname-for-mqtt-broker.example.com";
    
    ASSERT_TRUE(mqtt_settings_save(settings));
    
    MqttSettings loaded;
    mqtt_settings_load(loaded);
    ASSERT_STREQ("very-long-hostname-for-mqtt-broker.example.com", loaded.host.c_str());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

