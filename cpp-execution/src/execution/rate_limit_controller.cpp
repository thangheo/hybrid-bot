#include "execution/rate_limit_controller.hpp"
#include <algorithm>

namespace trading {
namespace execution {

RateLimitController::RateLimitController(const core::ExchangeId& exchangeId)
    : exchangeId_(exchangeId),
      running_(true)
{
    workerThread_ = std::thread(&RateLimitController::processQueue, this);
}

RateLimitController::~RateLimitController() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        running_ = false;
    }
    cv_.notify_all();

    if (workerThread_.joinable())
        workerThread_.join();
}

void RateLimitController::setRateLimitRules(
    const std::unordered_map<ExchangeRequestType, RateLimitRule>& rules)
{
    std::lock_guard<std::mutex> lk(mutex_);
    rules_ = rules;

    auto now = std::chrono::steady_clock::now();
    buckets_.clear();
    status_.clear();

    for (auto& [type, rule] : rules_) {
        buckets_[type] = {
            rule.maxRequests,
            rule.maxRequests,
            rule.windowSize,
            now
        };

        status_[type] = {
            rule.maxRequests,
            rule.windowSize,
            false
        };
    }
}

void RateLimitController::updateRateLimitStatus(
    ExchangeRequestType type,
    int remainingRequests,
    std::chrono::milliseconds reset)
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto now = std::chrono::steady_clock::now();

    // Override bucket immediately
    auto& b = buckets_[type];
    b.tokens = remainingRequests;
    b.capacity = std::max(b.capacity, remainingRequests); 
    b.window = reset;
    b.lastRefill = now;

    status_[type] = {
        remainingRequests,
        reset,
        remainingRequests <= 0
    };

    cv_.notify_all();
}

void RateLimitController::refillBucketIfNeeded(ExchangeRequestType type)
{
    auto it = buckets_.find(type);
    if (it == buckets_.end()) return;

    auto& bucket = it->second;
    if (bucket.window.count() == 0) return;

    auto now = std::chrono::steady_clock::now();
    if (now - bucket.lastRefill >= bucket.window) {
        bucket.tokens = bucket.capacity;
        bucket.lastRefill = now;

        auto& s = status_[type];
        s.remainingRequests = bucket.tokens;
        s.isLimited = false;
    }
}

bool RateLimitController::wouldBeRateLimited(ExchangeRequestType type) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = buckets_.find(type);
    if (it == buckets_.end()) return false;
    return it->second.tokens <= 0;
}

RateLimitStatus RateLimitController::getRateLimitStatus(ExchangeRequestType type) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = status_.find(type);
    if (it != status_.end()) return it->second;

    return {0, std::chrono::milliseconds(0), false};
}

int RateLimitController::getRemainingRequests(ExchangeRequestType type) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = buckets_.find(type);
    if (it != buckets_.end()) return it->second.tokens;

    auto rt = rules_.find(type);
    if (rt != rules_.end()) return rt->second.maxRequests;

    return 0;
}

std::chrono::milliseconds RateLimitController::getResetTime(ExchangeRequestType type) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = status_.find(type);
    if (it != status_.end()) return it->second.resetTime;

    return std::chrono::milliseconds(0);
}

bool RateLimitController::waitForPermission(ExchangeRequestType type)
{
    std::unique_lock<std::mutex> lk(mutex_);

    while (running_) {
        refillBucketIfNeeded(type);
        auto& b = buckets_[type];

        if (b.tokens > 0) {
            --b.tokens;
            auto& s = status_[type];
            s.remainingRequests = b.tokens;
            s.isLimited = (b.tokens == 0);
            return true;
        }

        auto next = b.lastRefill + b.window;
        cv_.wait_until(lk, next);
    }

    return false;
}

void RateLimitController::enqueueRequest(
    ExchangeRequestType type,
    std::function<void()> cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    queue_.push_back({type, std::move(cb)});
    cv_.notify_one();
}

void RateLimitController::processQueue()
{
    std::unique_lock<std::mutex> lk(mutex_);

    while (running_) {
        cv_.wait(lk, [this] {
            return !running_ || !queue_.empty();
        });

        if (!running_) break;
        if (queue_.empty()) continue;

        // Try to find the first request in the queue that has permission
        // (i.e., its bucket has tokens). If found, remove it and execute
        // outside the lock. If none are available, wait until the earliest
        // bucket refill or until notified.
        bool executed = false;

        // Refill all buckets that need it before scanning
        for (auto& [t, _] : buckets_) {
            refillBucketIfNeeded(t);
        }

        // Scan queue for first executable request
        size_t idx = 0;
        for (; idx < queue_.size(); ++idx) {
            auto& r = queue_[idx];
            auto it = buckets_.find(r.type);
            if (it == buckets_.end()) {
                // No rule for this type: treat as allowed
                break;
            }

            if (it->second.tokens > 0) {
                break; // found executable
            }
        }

        if (idx < queue_.size()) {
            // Pop the request at idx
            auto req = std::move(queue_[idx]);
            queue_.erase(queue_.begin() + idx);

            // Consume a token (if a bucket exists for the type)
            auto bit = buckets_.find(req.type);
            if (bit != buckets_.end()) {
                --(bit->second.tokens);
                auto& s = status_[req.type];
                s.remainingRequests = bit->second.tokens;
                s.isLimited = (bit->second.tokens == 0);
            }

            // Execute callback outside lock
            lk.unlock();
            try { req.callback(); } catch (...) {}
            lk.lock();
            executed = true;
        }

        if (!executed) {
            // No request could be executed now. Wait until the earliest
            // bucket refill time or until notified.
            std::chrono::steady_clock::time_point nextWake = std::chrono::steady_clock::time_point::max();

            for (auto& [t, b] : buckets_) {
                auto candidate = b.lastRefill + b.window;
                if (candidate < nextWake) nextWake = candidate;
            }

            if (nextWake == std::chrono::steady_clock::time_point::max()) {
                // No buckets with windows; just wait for notify
                cv_.wait(lk);
            } else {
                cv_.wait_until(lk, nextWake);
            }
        }
    }
}



} // namespace execution
} // namespace trading
