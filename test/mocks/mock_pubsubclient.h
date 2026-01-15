#ifndef MOCK_PUBSUBCLIENT_H
#define MOCK_PUBSUBCLIENT_H

#include "../mocks/mock_arduino.h"
#include <map>
#include <string>

/**
 * @brief Mock PubSubClient for testing MQTT Discovery Builder
 */
class MockPubSubClient {
public:
    struct PublishedMessage {
        String topic;
        String payload;
        bool retained;
    };
    
    MockPubSubClient() : bufferSize_(256) {}
    
    bool publish(const char* topic, const char* payload, bool retained = false) {
        PublishedMessage msg;
        msg.topic = String(topic);
        msg.payload = String(payload);
        msg.retained = retained;
        publishedMessages_.push_back(msg);
        return true; // Always succeed for testing
    }
    
    void setBufferSize(uint16_t size) {
        bufferSize_ = size;
    }
    
    // Test helpers
    const std::vector<PublishedMessage>& getPublishedMessages() const {
        return publishedMessages_;
    }
    
    bool wasPublished(const String& topic) const {
        for (const auto& msg : publishedMessages_) {
            if (msg.topic == topic) {
                return true;
            }
        }
        return false;
    }
    
    String getPublishedPayload(const String& topic) const {
        for (const auto& msg : publishedMessages_) {
            if (msg.topic == topic) {
                return msg.payload;
            }
        }
        return String("");
    }
    
    uint16_t getBufferSize() const {
        return bufferSize_;
    }
    
    void clear() {
        publishedMessages_.clear();
        bufferSize_ = 256;
    }
    
    size_t getPublishedCount() const {
        return publishedMessages_.size();
    }
    
private:
    std::vector<PublishedMessage> publishedMessages_;
    uint16_t bufferSize_;
};

// Forward declaration for PubSubClient type alias
// In tests, we'll use MockPubSubClient directly

#endif // MOCK_PUBSUBCLIENT_H

