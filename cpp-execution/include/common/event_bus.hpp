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
#include <atomic>
#include <memory>
#include <unordered_map>
#include <type_traits>
#include <typeinfo>

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
        
        {
            // Allocate the event on the heap and capture a shared_ptr in the queued lambda.
            // This avoids copying large event objects (and nested TradingSignal) into the
            // std::function capture, reducing heap churn.
            auto evtPtr = std::make_shared<std::decay_t<EventType>>(std::forward<EventType>(event));
            std::lock_guard<std::mutex> lock(queueMutex_);
            eventQueue_.push([evtPtr, this]() {
                // Process the event - this runs in the worker thread
                this->processEventInternal(*evtPtr);
            });
        }
        
        queueCondition_.notify_one();
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
        const char* typeName = typeid(EventType).name();
        size_t handlerId = nextHandlerId_++;
        
        // Type-erased wrapper for the handler
        auto wrapper = [handler = std::move(handler)](const Event& event) {
            const auto& typedEvent = static_cast<const EventType&>(event);
            handler(typedEvent);
        };
        
        handlers_[typeName][handlerId] = wrapper;
        return handlerId;
    }
    
    /**
     * Unsubscribe from events
     */
    template<typename EventType>
    bool unsubscribe(size_t handlerId) {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        
        // Use typeid instead of creating instance
        const char* typeName = typeid(EventType).name();
        auto typeIt = handlers_.find(typeName);
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
    
    // Process a single event (calls all registered handlers)
    template<typename EventType>
    void processEventInternal(const EventType& event) {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        
        // Use typeid instead of getTypeName()
        const char* typeName = typeid(EventType).name();
        auto typeIt = handlers_.find(typeName);
        if (typeIt != handlers_.end()) {
            for (const auto& [id, handler] : typeIt->second) {
                try {
                    handler(event);
                } catch (const std::exception& e) {
                    // Log error but continue processing other handlers
                }
            }
        }
    }
    
    // Thread management
    std::thread workerThread_;
    std::atomic<bool> running_{false};
    
    // Event queue
    std::queue<std::function<void()>> eventQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // Handler management
    std::unordered_map<std::string, std::unordered_map<size_t, std::function<void(const Event&)>>> handlers_;
    std::mutex handlerMutex_;
    size_t nextHandlerId_{1};
};

} // namespace common
} // namespace trading