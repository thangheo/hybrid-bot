/**
 * @file event_bus.hpp
 * @brief Simple async event bus to eliminate deadlocks
 * 
 * @author Trading System Team
 * @date September 4, 2025
 */

#pragma once

#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <type_traits>
#include <typeinfo>
#include <typeindex>
#include "common/logger.hpp"

namespace trading {
namespace common {

/**
 * Base class for all events
 */
class Event {
public:
    virtual ~Event() = default;
};

/**
 * Simple async event bus that processes events in a background thread
 * Eliminates all deadlock possibilities by decoupling event processing
 */
class EventBus {
public:
    EventBus();
    ~EventBus();
    
    /**
     * Start the event processing thread
     */
    void start();
    
    /**
     * Stop the event processing thread
     */
    void stop();
    
    /**
     * Publish an event (thread-safe, non-blocking)
     */
    template<typename EventType>
    void publish(EventType&& event) {
        static_assert(std::is_base_of_v<Event, std::decay_t<EventType>>, 
                     "EventType must inherit from Event");
        
        // Allocate the event on the heap and capture a shared_ptr in the queued lambda.
        auto evtPtr = std::make_shared<std::decay_t<EventType>>(std::forward<EventType>(event));
    const std::type_index typeIdx = std::type_index(typeid(EventType));
    const char* typeName = typeid(EventType).name();

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            // Drop policy: if queue is full, drop the event to avoid unbounded memory
            // growth and blocking of the system. We track droppedEvents_ for observability.
            if (eventQueue_.size() >= maxQueueSize_) {
                ++droppedEvents_;
                LOG_WARN("EventBus queue full (size={} >= max={}), dropping event type='{}' (dropped={})",
                         eventQueue_.size(), maxQueueSize_, typeName, droppedEvents_.load());
            } else {
                // enqueue a type-erased processor that will forward to handler thread-pool
                eventQueue_.push([evtPtr, this, typeIdx, typeName]() {
                    this->processEventAny(evtPtr, typeIdx, typeName);
                });
                queueCondition_.notify_one();
            }
        }
    }
    
    /**
     * Subscribe to events of a specific type
     */
    template<typename EventType>
    size_t subscribe(std::function<void(const EventType&)> handler) {
        static_assert(std::is_base_of_v<Event, EventType>, 
                     "EventType must inherit from Event");
        
        std::lock_guard<std::mutex> lock(handlerMutex_);
        
        // Use typeid instead of creating instance
        const std::type_index typeIdx = std::type_index(typeid(EventType));
        size_t handlerId = nextHandlerId_++;
        
        // Type-erased wrapper for the handler
        auto wrapper = [handler = std::move(handler)](const Event& event) {
            const auto& typedEvent = static_cast<const EventType&>(event);
            handler(typedEvent);
        };
        
        handlers_[typeIdx][handlerId] = wrapper;
        return handlerId;
    }
    
    /**
     * Unsubscribe from events
     */
    template<typename EventType>
    bool unsubscribe(size_t handlerId) {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        
        // Use typeid instead of creating instance
        const std::type_index typeIdx = std::type_index(typeid(EventType));
        auto typeIt = handlers_.find(typeIdx);
        if (typeIt != handlers_.end()) {
            auto handlerIt = typeIt->second.find(handlerId);
            if (handlerIt != typeIt->second.end()) {
                typeIt->second.erase(handlerIt);
                return true;
            }
        }
        return false;
    }

    // Singleton access - moved to public
    static EventBus& getInstance();

private:
    // Event processing worker thread
    void workerThread();
    // Handler thread-loop
    void handlerThreadLoop();
    // Type-erased processing for events
    void processEventAny(const std::shared_ptr<Event>& evtPtr, const std::type_index& typeIdx, const char* rawTypeName);
    
    // Process a single event (calls all registered handlers)
    template<typename EventType>
    void processEventInternal(const EventType& event) {
        // Copy the handlers under lock, then invoke them outside the lock.
        // This prevents a slow/blocking handler from holding handlerMutex_
        // and stalling other operations (reduces deadlock risk).
        std::vector<std::function<void(const Event&)>> handlersToCall;
        {
            std::lock_guard<std::mutex> lock(handlerMutex_);
            const std::type_index typeIdx = std::type_index(typeid(EventType));
            auto typeIt = handlers_.find(typeIdx);
            if (typeIt != handlers_.end()) {
                handlersToCall.reserve(typeIt->second.size());
                for (const auto& [id, handler] : typeIt->second) {
                    handlersToCall.push_back(handler);
                }
            }
        }

        // Debug: report number of handlers we're about to call for this event
        if (!handlersToCall.empty()) {
            LOG_DEBUG("EventBus: processing event type '{}' with {} handler(s)", typeid(EventType).name(), handlersToCall.size());
        } else {
            LOG_DEBUG("EventBus: processing event type '{}' with 0 handlers", typeid(EventType).name());
        }

        for (const auto &handler : handlersToCall) {
            try {
                handler(event);
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in event handler: {}", e.what());
            } catch (...) {
                LOG_ERROR("Unknown exception in event handler");
            }
        }
    }
    
    // Thread management
    std::thread workerThread_;
    std::atomic<bool> running_{false};
    
    // Event queue
    std::queue<std::function<void()>> eventQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    // Maximum queue size before we start dropping events
    size_t maxQueueSize_{1000};
    // Dropped events counter
    std::atomic<size_t> droppedEvents_{0};
    
    // Handler thread-pool members
    std::vector<std::thread> handlerThreads_;
    std::queue<std::function<void()>> handlerTaskQueue_;
    std::mutex handlerQueueMutex_;
    std::condition_variable handlerQueueCondition_;
    
    // Handler management
    std::unordered_map<std::type_index, std::unordered_map<size_t, std::function<void(const Event&)>>> handlers_;
    std::mutex handlerMutex_;
    size_t nextHandlerId_{1};

public:
    // Observability helpers
    size_t getDroppedEvents() const { return droppedEvents_.load(); }
    size_t getQueueSize() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return eventQueue_.size();
    }
};

} // namespace common
} // namespace trading