// rate_limit_controller.hpp
#pragma once

#include <string>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <future>
#include "exchange_api_client.hpp"
#include "core/market_types.hpp"
namespace trading {
namespace execution {

struct RateLimitRule {
    int maxRequests;
    std::chrono::milliseconds windowSize;
};

struct RateLimitStatus {
    int remainingRequests;
    std::chrono::milliseconds resetTime;
    bool isLimited;
};

class RateLimitController {
public:
    explicit RateLimitController(const core::ExchangeId& exchangeId);
    ~RateLimitController();
    
    // Set the rate limit rules for different request types
    void setRateLimitRules(const std::unordered_map<ExchangeRequestType, RateLimitRule>& rules);
    
    // Update rate limit status based on exchange response headers
    void updateRateLimitStatus(ExchangeRequestType requestType, int remainingRequests, 
                              std::chrono::milliseconds resetTime);
    
    // Check if a request would be rate limited
    bool wouldBeRateLimited(ExchangeRequestType requestType) const;
    /**
     * Waits until it's allowed to make a request of the specified type.
     * This method blocks the calling thread until permission is granted.
     * 
     * @param requestType The type of request to wait for permission for
     * @return true if permission is granted, false if the controller is shutting down
     */
    bool waitForPermission(ExchangeRequestType requestType);
    // Add these to execution/rate_limit_controller.hpp in the public section of the RateLimitController class

    /**
     * Gets the number of remaining requests for the specified request type.
     * 
     * @param requestType The type of request to check
     * @return The number of remaining requests allowed before being rate limited
     */
    int getRemainingRequests(ExchangeRequestType requestType) const;

    /**
     * Gets the time until the rate limit resets for the specified request type.
     * 
     * @param requestType The type of request to check
     * @return The time until the rate limit resets
     */
    std::chrono::milliseconds getResetTime(ExchangeRequestType requestType) const;
    // Get current rate limit status
    RateLimitStatus getRateLimitStatus(ExchangeRequestType requestType) const;
    
    // Execute a request with rate limiting
    template<typename Func, typename... Args>
    auto executeRequest(ExchangeRequestType requestType, Func&& func, Args&&... args)
        -> std::future<decltype(func(std::forward<Args>(args)...))> {
        
        using ReturnType = decltype(func(std::forward<Args>(args)...));
        
        // Create promise and future
        std::shared_ptr<std::promise<ReturnType>> promise = 
            std::make_shared<std::promise<ReturnType>>();
        std::future<ReturnType> future = promise->get_future();
        
        // Submit the request to the queue
        enqueueRequest(requestType, [promise, func = std::forward<Func>(func), 
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

private:
    core::ExchangeId exchangeId_;
    std::unordered_map<ExchangeRequestType, RateLimitRule> rules_;
    std::unordered_map<ExchangeRequestType, RateLimitStatus> status_;
    
    struct RequestInfo {
        ExchangeRequestType type;
        std::function<void()> callback;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    std::vector<RequestInfo> requestQueue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool running_;
    std::thread workerThread_;
    
    // Process the request queue
    void processQueue();
    
    // Add a request to the queue
    void enqueueRequest(ExchangeRequestType requestType, std::function<void()> callback);
    
    // Try to execute the next request in the queue
    bool tryExecuteNextRequest();
};

} // namespace execution
} // namespace trading  