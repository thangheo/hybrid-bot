/**
 * @file zmq_manager.hpp
 * @brief Centralized ZeroMQ context/endpoint manager
 */
#pragma once

#include <zmq.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace trading {
namespace communication {

class ZmqManager {
public:
    static ZmqManager& instance();

    // Get (and create if necessary) a shared context. The manager keeps
    // ownership; callers receive a shared_ptr to use.
    std::shared_ptr<zmq::context_t> getContext(int io_threads = 1);

    // Register an endpoint for binding. Returns false if endpoint already bound.
    bool registerEndpoint(const std::string& endpoint);

    // Unregister endpoint when it is unbound/closed
    void unregisterEndpoint(const std::string& endpoint);

    // Check if endpoint is bound
    bool isEndpointBound(const std::string& endpoint) const;

    // Compute exponential backoff delay (ms) for reconnect attempts
    std::chrono::milliseconds reconnectDelayMs(int attempt, std::chrono::milliseconds base = std::chrono::milliseconds(500), int capPow = 10) const;

private:
    ZmqManager() = default;
    ~ZmqManager() = default;
    ZmqManager(const ZmqManager&) = delete;
    ZmqManager& operator=(const ZmqManager&) = delete;

    mutable std::mutex mutex_;
    std::shared_ptr<zmq::context_t> context_;
    std::unordered_map<std::string, int> bound_endpoints_;
};

} // namespace communication
} // namespace trading
