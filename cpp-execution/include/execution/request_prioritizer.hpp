// request_prioritizer.hpp
#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <atomic>
#include <future>
#include <optional>
#include "exchange_api_client.hpp"

namespace trading {
namespace execution {

enum class RequestPriority {
    Critical,  // Highest priority (e.g., emergency cancellations)
    High,      // High priority (e.g., regular order cancellations)
    Normal,    // Normal priority (e.g., order placements)
    Low        // Low priority (e.g., status queries, balance checks)
};

struct PrioritizedRequest {
    RequestPriority priority;
    ExchangeRequestType type;
    std::function<void()> callback;
    std::chrono::steady_clock::time_point timestamp;
    uint64_t seq;
    
    bool operator<(const PrioritizedRequest& other) const {
        // Higher priority comes first
        if (priority != other.priority) {
            return priority > other.priority;
        }
        // Within same priority, preserve FIFO by sequence number
        return seq > other.seq;
    }
};

class RequestPrioritizer {
public:
    RequestPrioritizer();
    ~RequestPrioritizer();
    
    // Submit a request with specified priority
    template<typename Func, typename... Args>
    auto submitRequest(RequestPriority priority, ExchangeRequestType requestType, 
                      Func&& func, Args&&... args)
        -> std::future<decltype(func(std::forward<Args>(args)...))> {
        
        using ReturnType = decltype(func(std::forward<Args>(args)...));
        
        // Create promise and future
        std::shared_ptr<std::promise<ReturnType>> promise = 
            std::make_shared<std::promise<ReturnType>>();
        std::future<ReturnType> future = promise->get_future();
        
        // Submit the request to the queue
        enqueueRequest(priority, requestType, [promise, func = std::forward<Func>(func), 
                      args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            try {
                if constexpr (std::is_void_v<ReturnType>) {
                    std::apply(func, args);
                    promise->set_value();
                } else {
                    promise->set_value(std::apply(func, args));
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
        
        return future;
    }
    
    // Check if there are any pending requests
    bool hasPendingRequests() const;
    
    // Get current queue sizes
    std::unordered_map<RequestPriority, size_t> getQueueSizes() const;
    
    // Start and stop the processing thread
    void start();
    void stop();

private:
    std::priority_queue<PrioritizedRequest> requestQueue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
    // When true, the worker thread is paused and will not start executing new requests.
    std::atomic<bool> paused_{false};
    // True while a request callback is being executed.
    std::atomic<bool> processingRequest_{false};
    // Mutex used to synchronize the start/stop of request processing so stop()
    // can reliably prevent new requests from being popped and executed.
    std::mutex processingMutex_;
    std::thread workerThread_;
    // Sequence counter to preserve FIFO ordering among equal-priority requests
    std::atomic<uint64_t> nextSeq_{0};
    
    // Process the request queue
    void processQueue();
    
    // Add a request to the queue
    void enqueueRequest(RequestPriority priority, ExchangeRequestType requestType, 
                        std::function<void()> callback);
    
    // Execute the next request in the queue
    // Pop the next callback while the mutex is locked. Returns an empty optional
    // if the queue is empty. The returned callback can be executed outside
    // the lock.
    std::optional<std::function<void()>> popNextCallbackLocked();
};

} // namespace execution
} // namespace trading