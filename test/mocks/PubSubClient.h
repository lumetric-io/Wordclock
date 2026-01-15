#ifndef PUBSUBCLIENT_H
#define PUBSUBCLIENT_H

#include "mock_pubsubclient.h"

// In test environment, PubSubClient is MockPubSubClient
// We use a class alias to make MockPubSubClient work as PubSubClient
class PubSubClient : public MockPubSubClient {
public:
    PubSubClient() : MockPubSubClient() {}
    PubSubClient(const PubSubClient&) = delete;
    PubSubClient& operator=(const PubSubClient&) = delete;
};

#endif // PUBSUBCLIENT_H

