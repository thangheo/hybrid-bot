/**
 * @file event_bus.cpp
 * @brief Implementation of async event bus
 */

#include "common/event_bus.hpp"
#include "common/logger.hpp"
#include <mutex>

namespace trading {
namespace common {

EventBus::EventBus() {
    LOG_DEBUG("EventBus created");
}

EventBus::~EventBus() {
    stop();
    LOG_DEBUG("EventBus destroyed");
}

void EventBus::start() {
    if (running_.load()) {
        LOG_WARN("EventBus already running");
        return;
    }
    
    running_ = true;
    workerThread_ = std::thread(&EventBus::workerThread, this);
    
    LOG_INFO("EventBus started");
}

void EventBus::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    queueCondition_.notify_all();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    LOG_INFO("EventBus stopped");
}

void EventBus::workerThread() {
    LOG_DEBUG("EventBus worker thread started");
    
    while (running_.load()) {
        std::function<void()> eventProcessor;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            
            // Wait for events or shutdown signal
            queueCondition_.wait(lock, [this] {
                return !eventQueue_.empty() || !running_.load();
            });
            
            if (!running_.load()) {
                break;
            }
            
            if (!eventQueue_.empty()) {
                eventProcessor = eventQueue_.front();
                eventQueue_.pop();
            }
        }
        
        // Process event outside the lock
        if (eventProcessor) {
            try {
                eventProcessor();
            } catch (const std::exception& e) {
                LOG_ERROR("Exception processing event: {}", e.what());
            }
        }
    }
    
    LOG_DEBUG("EventBus worker thread stopped");
}

// Static instance getter with auto-start
EventBus& EventBus::getInstance() {
    static EventBus instance;
    static std::once_flag startFlag;
    
    // Ensure the instance is started
    std::call_once(startFlag, [&instance]() {
        instance.start();
    });
    
    return instance;
}

} // namespace common
} // namespace trading