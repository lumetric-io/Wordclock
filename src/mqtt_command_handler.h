#ifndef MQTT_COMMAND_HANDLER_H
#define MQTT_COMMAND_HANDLER_H

#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>

/**
 * @brief Base class for MQTT command handlers
 * 
 * Implements the Command Pattern to replace the large if-else chain
 * in handleMessage(). Each handler encapsulates the logic for one
 * specific MQTT topic/command.
 */
class MqttCommandHandler {
public:
    virtual ~MqttCommandHandler() = default;
    
    /**
     * @brief Handle an incoming MQTT message
     * @param payload The message payload as a string
     */
    virtual void handle(const String& payload) = 0;
};

/**
 * @brief Registry of MQTT command handlers
 * 
 * Maps MQTT topics to their corresponding handlers.
 * Implements Singleton pattern for global access.
 */
class MqttCommandRegistry {
public:
    static MqttCommandRegistry& instance();
    
    /**
     * @brief Register a handler for a specific topic
     * @param topic MQTT topic to handle
     * @param handler Handler instance (ownership transferred to registry)
     */
    void registerHandler(const String& topic, MqttCommandHandler* handler);
    
    /**
     * @brief Handle an incoming MQTT message
     * @param topic MQTT topic
     * @param payload Message payload
     */
    void handleMessage(const String& topic, const String& payload);
    
    /**
     * @brief Helper for simple lambda handlers
     * @param topic MQTT topic
     * @param handler Lambda function to handle the message
     */
    void registerLambda(const String& topic, 
                       std::function<void(const String&)> handler);
    
    /**
     * @brief Clear all registered handlers (useful for testing)
     */
    void clear();
    
private:
    MqttCommandRegistry() = default;
    ~MqttCommandRegistry();
    
    std::map<String, MqttCommandHandler*> handlers_;
    std::map<String, std::function<void(const String&)>> lambdaHandlers_;
};

/**
 * @brief Handler for light commands (JSON with state/brightness/color)
 * 
 * Handles complex JSON payloads for controlling the LED light.
 */
class LightCommandHandler : public MqttCommandHandler {
public:
    void handle(const String& payload) override;
};

/**
 * @brief Generic handler for simple switch commands (ON/OFF)
 * 
 * Handles boolean on/off commands with callback functions.
 */
class SwitchCommandHandler : public MqttCommandHandler {
public:
    SwitchCommandHandler(const String& name, 
                        std::function<void(bool)> setter,
                        std::function<void()> publisher);
    void handle(const String& payload) override;
    
private:
    String name_;
    std::function<void(bool)> setter_;
    std::function<void()> publisher_;
};

/**
 * @brief Generic handler for number commands with validation
 * 
 * Handles numeric inputs with min/max constraints.
 */
class NumberCommandHandler : public MqttCommandHandler {
public:
    NumberCommandHandler(int min, int max,
                        std::function<void(int)> setter,
                        std::function<void()> publisher);
    void handle(const String& payload) override;
    
private:
    int min_, max_;
    std::function<void(int)> setter_;
    std::function<void()> publisher_;
};

/**
 * @brief Generic handler for select/enum commands
 * 
 * Handles selection from a list of valid options.
 */
class SelectCommandHandler : public MqttCommandHandler {
public:
    SelectCommandHandler(const std::vector<String>& validOptions,
                        std::function<void(const String&)> setter,
                        std::function<void()> publisher);
    void handle(const String& payload) override;
    
private:
    std::vector<String> validOptions_;
    std::function<void(const String&)> setter_;
    std::function<void()> publisher_;
};

/**
 * @brief Handler for time string commands (HH:MM format)
 * 
 * Handles time inputs with validation and parsing.
 */
class TimeStringCommandHandler : public MqttCommandHandler {
public:
    TimeStringCommandHandler(std::function<bool(const String&, uint16_t&)> parser,
                            std::function<void(uint16_t)> setter,
                            std::function<void()> publisher,
                            const String& name);
    void handle(const String& payload) override;
    
private:
    std::function<bool(const String&, uint16_t&)> parser_;
    std::function<void(uint16_t)> setter_;
    std::function<void()> publisher_;
    String name_;
};

#endif // MQTT_COMMAND_HANDLER_H

