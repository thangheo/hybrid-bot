#include "communication/zmq_manager.hpp"
#include <cmath>

namespace trading {
namespace communication {

ZmqManager& ZmqManager::instance() {
    static ZmqManager mgr;
    return mgr;
}

std::shared_ptr<zmq::context_t> ZmqManager::getContext(int io_threads) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!context_) {
        // create context with specified IO threads
        context_ = std::make_shared<zmq::context_t>(io_threads);
        // set non-blocking context termination where supported
        try {
            context_->set(zmq::ctxopt::blocky, false);
        } catch (...) {
            // ignore if option unsupported
        }
    }
    return context_;
}

bool ZmqManager::registerEndpoint(const std::string& endpoint) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = bound_endpoints_.find(endpoint);
    if (it != bound_endpoints_.end()) {
        // already bound
        return false;
    }
    bound_endpoints_[endpoint] = 1;
    return true;
}

void ZmqManager::unregisterEndpoint(const std::string& endpoint) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = bound_endpoints_.find(endpoint);
    if (it != bound_endpoints_.end()) {
        bound_endpoints_.erase(it);
    }
}

bool ZmqManager::isEndpointBound(const std::string& endpoint) const {
    std::lock_guard<std::mutex> lk(mutex_);
    return bound_endpoints_.find(endpoint) != bound_endpoints_.end();
}

std::chrono::milliseconds ZmqManager::reconnectDelayMs(int attempt, std::chrono::milliseconds base, int capPow) const {
    int pow = std::min(attempt, capPow);
    long long delay = static_cast<long long>(base.count()) * (1LL << pow);
    return std::chrono::milliseconds(delay);
}

} // namespace communication
} // namespace trading
