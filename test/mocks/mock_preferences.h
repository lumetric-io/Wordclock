#ifndef MOCK_PREFERENCES_H
#define MOCK_PREFERENCES_H

#include <map>
#include <string>
#include "mock_arduino.h"

/**
 * @brief Mock implementation of ESP32 Preferences for unit testing
 * 
 * Provides in-memory storage that mimics Preferences API without
 * requiring actual NVS flash operations.
 */
class Preferences {
public:
    Preferences() : readOnly_(false) {}
    
    bool begin(const char* name, bool readOnly = false) {
        namespace_ = name;
        readOnly_ = readOnly;
        return true;
    }
    
    void end() {
        namespace_ = "";
    }
    
    void clear() {
        if (!readOnly_) {
            storage_[namespace_].clear();
        }
    }
    
    bool remove(const char* key) {
        if (readOnly_) return false;
        auto it = storage_[namespace_].find(key);
        if (it != storage_[namespace_].end()) {
            storage_[namespace_].erase(it);
            return true;
        }
        return false;
    }
    
    // Getters
    uint8_t getUChar(const char* key, uint8_t defaultValue = 0) {
        auto& ns = storage_[namespace_];
        auto it = ns.find(key);
        return (it != ns.end()) ? static_cast<uint8_t>(std::stoi(it->second)) : defaultValue;
    }
    
    uint16_t getUShort(const char* key, uint16_t defaultValue = 0) {
        auto& ns = storage_[namespace_];
        auto it = ns.find(key);
        return (it != ns.end()) ? static_cast<uint16_t>(std::stoi(it->second)) : defaultValue;
    }
    
    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) {
        auto& ns = storage_[namespace_];
        auto it = ns.find(key);
        return (it != ns.end()) ? static_cast<uint32_t>(std::stoul(it->second)) : defaultValue;
    }
    
    bool getBool(const char* key, bool defaultValue = false) {
        auto& ns = storage_[namespace_];
        auto it = ns.find(key);
        return (it != ns.end()) ? (it->second == "1") : defaultValue;
    }
    
    String getString(const char* key, const String& defaultValue = "") {
        auto& ns = storage_[namespace_];
        auto it = ns.find(key);
        return (it != ns.end()) ? String(it->second.c_str()) : defaultValue;
    }
    
    // Setters
    size_t putUChar(const char* key, uint8_t value) {
        if (readOnly_) return 0;
        storage_[namespace_][key] = std::to_string(value);
        return sizeof(value);
    }
    
    size_t putUShort(const char* key, uint16_t value) {
        if (readOnly_) return 0;
        storage_[namespace_][key] = std::to_string(value);
        return sizeof(value);
    }
    
    size_t putUInt(const char* key, uint32_t value) {
        if (readOnly_) return 0;
        storage_[namespace_][key] = std::to_string(value);
        return sizeof(value);
    }
    
    size_t putBool(const char* key, bool value) {
        if (readOnly_) return 0;
        storage_[namespace_][key] = value ? "1" : "0";
        return sizeof(value);
    }
    
    size_t putString(const char* key, const String& value) {
        if (readOnly_) return 0;
        storage_[namespace_][std::string(key)] = value.c_str();
        return value.length();
    }
    
    bool isKey(const char* key) {
        auto& ns = storage_[namespace_];
        return ns.find(key) != ns.end();
    }
    
    // Test helpers
    static void reset() {
        storage_.clear();
    }
    
    static size_t getStorageSize() {
        size_t total = 0;
        for (const auto& ns : storage_) {
            total += ns.second.size();
        }
        return total;
    }

private:
    std::string namespace_;
    bool readOnly_;
    
    // Global storage: namespace -> (key -> value)
    static std::map<std::string, std::map<std::string, std::string>> storage_;
};

// Define static member
std::map<std::string, std::map<std::string, std::string>> Preferences::storage_;

#endif // MOCK_PREFERENCES_H

