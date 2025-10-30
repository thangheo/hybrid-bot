#pragma once

#include <string>
#include <vector>

namespace trading::risk::alerting {

class WebhookClient {
public:
    explicit WebhookClient(const std::vector<std::string>& endpoints);
    ~WebhookClient();

    // Send a simple JSON payload (string) to all endpoints; returns true if at least one succeeded
    bool send(const std::string& payload);

private:
    std::vector<std::string> endpoints_;
};

} // namespace trading::risk::alerting
