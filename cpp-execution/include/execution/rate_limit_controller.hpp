#pragma once

#include <unordered_map>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <vector>

#include "core/market_types.hpp"
#include "execution/request_prioritizer.hpp"

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

    void setRateLimitRules(
        const std::unordered_map<ExchangeRequestType, RateLimitRule>& rules);

    void updateRateLimitStatus(
        ExchangeRequestType requestType,
        int remainingRequests,
        std::chrono::milliseconds resetTime);

    bool wouldBeRateLimited(ExchangeRequestType type) const;
    bool waitForPermission(ExchangeRequestType type);

    int getRemainingRequests(ExchangeRequestType type) const;
    std::chrono::milliseconds getResetTime(ExchangeRequestType type) const;
    RateLimitStatus getRateLimitStatus(ExchangeRequestType type) const;

    template<typename Func, typename... Args>
    auto executeRequest(ExchangeRequestType requestType,
                        Func&& func,
                        Args&&... args)
        -> std::future<decltype(func(std::forward<Args>(args)...))>
    {
        using Ret = decltype(func(std::forward<Args>(args)...));

        auto promise = std::make_shared<std::promise<Ret>>();
        auto future = promise->get_future();

        // capture by value to ensure arguments live until invocation
        enqueueRequest(requestType, [promise, func = std::forward<Func>(func), args...]() mutable {
            try {
                if constexpr (std::is_void_v<Ret>) {
                    func(std::forward<Args>(args)...);
                    promise->set_value();
                } else {
                    promise->set_value(func(std::forward<Args>(args)...));
                }
            }
            catch (...) {
                promise->set_exception(std::current_exception());
            }
        });

        return future;
    }

private:

    struct TokenBucket {
        int tokens;
        int capacity;
        std::chrono::milliseconds window;
        std::chrono::steady_clock::time_point lastRefill;
    };

    struct RequestInfo {
        ExchangeRequestType type;
        std::function<void()> callback;
    };

    core::ExchangeId exchangeId_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool running_;

    std::thread workerThread_;

    std::unordered_map<ExchangeRequestType, RateLimitRule> rules_;
    std::unordered_map<ExchangeRequestType, TokenBucket> buckets_;
    std::unordered_map<ExchangeRequestType, RateLimitStatus> status_;

    std::vector<RequestInfo> queue_;

    void processQueue();
    void refillBucketIfNeeded(ExchangeRequestType type);
    void enqueueRequest(ExchangeRequestType type, std::function<void()> cb);
};

} // namespace execution
} // namespace trading
