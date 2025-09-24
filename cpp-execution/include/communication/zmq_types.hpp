#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <atomic>

namespace trading {
namespace communication {

enum class MessagePriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

enum class ServerState {
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR
};

struct ServerStatistics {
    std::atomic<uint64_t> messages_published{0};
    std::atomic<uint64_t> messages_queued{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> requests_processed{0};
    std::atomic<uint64_t> requests_failed{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> queue_overflows{0};
    std::atomic<uint64_t> connection_errors{0};
    std::atomic<uint64_t> send_failures{0};
    std::atomic<uint64_t> send_eagain_count{0};
    std::chrono::steady_clock::time_point start_time;

    ServerStatistics() : start_time(std::chrono::steady_clock::now()) {}

    void reset() {
        messages_published = 0;
        messages_queued = 0;
        messages_dropped = 0;
        requests_processed = 0;
        requests_failed = 0;
        bytes_sent = 0;
        bytes_received = 0;
        queue_overflows = 0;
        connection_errors = 0;
        start_time = std::chrono::steady_clock::now();
    }
};

struct GenericMessage {
    std::string topic;
    std::string data;
    MessagePriority priority;
    std::chrono::steady_clock::time_point timestamp;
    std::string correlation_id;
    std::unordered_map<std::string, std::string> metadata;

    GenericMessage() = default;
    GenericMessage(const std::string& t, const std::string& d,
                   MessagePriority p = MessagePriority::NORMAL)
        : topic(t), data(d), priority(p), timestamp(std::chrono::steady_clock::now()) {}

    static std::string generateCorrelationId();
};

using RequestHandler = std::function<std::string(const std::string& request_data,
                                                const std::unordered_map<std::string, std::string>& metadata)>;

} // namespace communication
} // namespace trading
