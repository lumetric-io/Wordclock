#include "mqtt_command_handler.h"
#include "display_settings.h"
#include "night_mode.h"
#include "led_state.h"
#include "log.h"
#include "wordclock.h"
#include "time_mapper.h"
#include <ArduinoJson.h>

// External references
extern DisplaySettings displaySettings;
extern bool clockEnabled;

// Forward declarations for publisher functions (defined in mqtt_client.cpp)
extern void publishLightState();
extern void publishSwitch(const String& topic, bool on);
extern void publishNumber(const String& topic, int v);
extern void publishSelect(const String& topic);
extern void publishNightOverrideState();
extern void publishNightActiveState();
extern void publishNightEffectState();
extern void publishNightDimState();
extern void publishNightScheduleState();

// ============================================================================
// Registry Implementation
// ============================================================================

MqttCommandRegistry& MqttCommandRegistry::instance() {
    static MqttCommandRegistry registry;
    return registry;
}

MqttCommandRegistry::~MqttCommandRegistry() {
    // Clean up owned handler pointers
    for (auto& pair : handlers_) {
        delete pair.second;
    }
    handlers_.clear();
    lambdaHandlers_.clear();
}

void MqttCommandRegistry::registerHandler(const String& topic, MqttCommandHandler* handler) {
    // Delete any existing handler for this topic
    auto it = handlers_.find(topic);
    if (it != handlers_.end()) {
        delete it->second;
    }
    handlers_[topic] = handler;
}

void MqttCommandRegistry::registerLambda(const String& topic, 
                                        std::function<void(const String&)> handler) {
    lambdaHandlers_[topic] = handler;
}

void MqttCommandRegistry::handleMessage(const String& topic, const String& payload) {
    // Try direct handler first
    auto it = handlers_.find(topic);
    if (it != handlers_.end()) {
        it->second->handle(payload);
        return;
    }
    
    // Try lambda handler
    auto lit = lambdaHandlers_.find(topic);
    if (lit != lambdaHandlers_.end()) {
        lit->second(payload);
        return;
    }
    
    logWarn(String("Unhandled MQTT topic: ") + topic);
}

void MqttCommandRegistry::clear() {
    for (auto& pair : handlers_) {
        delete pair.second;
    }
    handlers_.clear();
    lambdaHandlers_.clear();
}

// ============================================================================
// Light Command Handler
// ============================================================================

void LightCommandHandler::handle(const String& payload) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        logWarn(String("Light command JSON parse error: ") + err.c_str());
        return;
    }
    
    // Handle state (ON/OFF)
    if (doc["state"].is<const char*>()) {
        const char* st = doc["state"];
        clockEnabled = (strcmp(st, "ON") == 0);
    }
    
    // Handle brightness
    if (doc["brightness"].is<int>()) {
        int br = doc["brightness"].as<int>();
        br = constrain(br, 0, 255);
        ledState.setBrightness(br);
    }
    
    // Handle color
    if (doc["color"].is<JsonObject>()) {
        uint8_t r = doc["color"]["r"] | 0;
        uint8_t g = doc["color"]["g"] | 0;
        uint8_t b = doc["color"]["b"] | 0;
        ledState.setRGB(r, g, b);
    }
    
    // Apply display immediately
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        auto idx = get_led_indices_for_time(&timeinfo);
        showLeds(idx);
    }
    
    publishLightState();
}

// ============================================================================
// Switch Command Handler
// ============================================================================

SwitchCommandHandler::SwitchCommandHandler(const String& name,
                                          std::function<void(bool)> setter,
                                          std::function<void()> publisher)
    : name_(name), setter_(setter), publisher_(publisher) {}

void SwitchCommandHandler::handle(const String& payload) {
    bool on = (payload == "ON" || payload == "on" || payload == "1" || 
               payload == "true" || payload == "True");
    setter_(on);
    publisher_();
}

// ============================================================================
// Number Command Handler
// ============================================================================

NumberCommandHandler::NumberCommandHandler(int min, int max,
                                          std::function<void(int)> setter,
                                          std::function<void()> publisher)
    : min_(min), max_(max), setter_(setter), publisher_(publisher) {}

void NumberCommandHandler::handle(const String& payload) {
    int value = payload.toInt();
    value = constrain(value, min_, max_);
    setter_(value);
    publisher_();
}

// ============================================================================
// Select Command Handler
// ============================================================================

SelectCommandHandler::SelectCommandHandler(const std::vector<String>& validOptions,
                                          std::function<void(const String&)> setter,
                                          std::function<void()> publisher)
    : validOptions_(validOptions), setter_(setter), publisher_(publisher) {}

void SelectCommandHandler::handle(const String& payload) {
    // Try exact match first
    for (const auto& option : validOptions_) {
        if (payload == option) {
            setter_(payload);
            publisher_();
            return;
        }
    }
    
    // Try case-insensitive match
    String payloadUpper = payload;
    payloadUpper.toUpperCase();
    for (const auto& option : validOptions_) {
        String optionUpper = option;
        optionUpper.toUpperCase();
        if (payloadUpper == optionUpper) {
            setter_(option);  // Use the valid option, not the payload
            publisher_();
            return;
        }
    }
    
    logWarn(String("Invalid option for select: ") + payload);
}

// ============================================================================
// Time String Command Handler
// ============================================================================

TimeStringCommandHandler::TimeStringCommandHandler(
    std::function<bool(const String&, uint16_t&)> parser,
    std::function<void(uint16_t)> setter,
    std::function<void()> publisher,
    const String& name)
    : parser_(parser), setter_(setter), publisher_(publisher), name_(name) {}

void TimeStringCommandHandler::handle(const String& payload) {
    uint16_t minutes = 0;
    if (!parser_(payload, minutes)) {
        logWarn(String("Invalid time string for ") + name_ + ": " + payload);
        return;
    }
    setter_(minutes);
    publisher_();
}

