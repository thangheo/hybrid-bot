/**
 * @file event_bus.cpp
 * @brief Implementation of async event bus
 */

#include "common/event_bus.hpp"
#include "common/logger.hpp"
#include <mutex>
#include <functional>
#include <vector>
 #

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
    // start handler thread-pool
    size_t poolSize = std::max<size_t>(1, std::thread::hardware_concurrency() / 2);
    for (size_t i = 0; i < poolSize; ++i) {
        handlerThreads_.emplace_back([this]() { this->handlerThreadLoop(); });
    }
    
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
        
        // Enqueue processing task for handler threads (non-blocking)
        if (eventProcessor) {
            try {
                {
                    std::lock_guard<std::mutex> lock(handlerQueueMutex_);
                    handlerTaskQueue_.push(std::move(eventProcessor));
                }
                handlerQueueCondition_.notify_one();
            } catch (const std::exception& e) {
                LOG_ERROR("Exception enqueuing handler task: {}", e.what());
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

// New methods for type-erased processing and handler thread-pool
void EventBus::handlerThreadLoop() {
    while (running_.load()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(handlerQueueMutex_);
            handlerQueueCondition_.wait(lock, [this] {
                return !handlerTaskQueue_.empty() || !running_.load();
            });

            if (!running_.load()) break;

            if (!handlerTaskQueue_.empty()) {
                task = std::move(handlerTaskQueue_.front());
                handlerTaskQueue_.pop();
            }
        }

        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in handler task: {}", e.what());
            } catch (...) {
                LOG_ERROR("Unknown exception in handler task");
            }
        }
    }
}

// Type-erased helper to process an event pointer; will call processEventInternal with proper type
void EventBus::processEventAny(const std::shared_ptr<Event>& evtPtr, const char* typeName) {
    // We don't have runtime RTTI to cast to the exact type here, so rely on handlers being
    // registered under the same typeid name; processEventInternal is templated, but we can't
    // instantiate it here. The queued lambda in publish captured typed pointer, so we instead
    // forward the call to the existing processEventInternal via a std::invoke stored earlier.
    // For simplicity, we will invoke a type-erased handler call stored in handlers_ map.

    // Build a snapshot of handlers for this type and invoke them with the base Event reference.
    std::vector<std::function<void(const Event&)>> handlersToCall;
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        auto it = handlers_.find(typeName);
        if (it != handlers_.end()) {
            for (const auto& [id, handler] : it->second) {
                handlersToCall.push_back(handler);
            }
        }
    }

    if (handlersToCall.empty()) {
        LOG_DEBUG("EventBus: no handlers for type '{}'", typeName);
        return;
    }

    for (const auto& h : handlersToCall) {
        try {
            h(*evtPtr);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in event handler: {}", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in event handler");
        }
    }
}

} // namespace common
} // namespace trading