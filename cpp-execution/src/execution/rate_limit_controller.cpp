// rate_limit_controller.cpp
#include "execution/rate_limit_controller.hpp"
#include <algorithm>
#include <iostream>

namespace trading {
namespace execution {

RateLimitController::RateLimitController(const core::ExchangeId& exchangeId)
    : exchangeId_(exchangeId), running_(true) {
    
    // Start the worker thread
    workerThread_ = std::thread(&RateLimitController::processQueue, this);
}

RateLimitController::~RateLimitController() {
    // Signal the worker thread to stop
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    
    // Wait for the worker thread to finish
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void RateLimitController::setRateLimitRules(const std::unordered_map<ExchangeRequestType, RateLimitRule>& rules) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_ = rules;
    
    // Initialize status for each request type with full rate limits
    for (const auto& [type, rule] : rules) {
        if (status_.find(type) == status_.end()) {
            status_[type] = {
                rule.maxRequests,  // remainingRequests
                std::chrono::milliseconds(0),  // resetTime
                false  // isLimited
            };
        }
    }
}

void RateLimitController::updateRateLimitStatus(ExchangeRequestType requestType, 
                                             int remainingRequests, 
                                             std::chrono::milliseconds resetTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update the rate limit status for the given request type
    auto it = status_.find(requestType);
    if (it != status_.end()) {
        it->second.remainingRequests = remainingRequests;
        it->second.resetTime = resetTime;
        it->second.isLimited = (remainingRequests <= 0);
    } else {
        // If we don't have a status for this request type yet, create one
        status_[requestType] = {
            remainingRequests,
            resetTime,
            (remainingRequests <= 0)
        };
    }
    
    // If we now have capacity, notify waiting threads
    if (remainingRequests > 0) {
        cv_.notify_all();
    }
}

bool RateLimitController::wouldBeRateLimited(ExchangeRequestType requestType) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if we have rate limit status for this request type
    auto statusIt = status_.find(requestType);
    if (statusIt != status_.end()) {
        // Check if we're currently rate limited
        if (statusIt->second.isLimited) {
            return true;
        }
        
        // Check if we have any remaining requests
        return statusIt->second.remainingRequests <= 0;
    }
    
    // If we don't have status for this request type, assume we're not rate limited
    return false;
}

RateLimitStatus RateLimitController::getRateLimitStatus(ExchangeRequestType requestType) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = status_.find(requestType);
    if (it != status_.end()) {
        return it->second;
    }
    
    // If we don't have status for this request type, return a default status
    return {
        0,  // remainingRequests
        std::chrono::milliseconds(0),  // resetTime
        false  // isLimited
    };
}
bool RateLimitController::waitForPermission(ExchangeRequestType requestType) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Check if we're shutting down
    if (!running_) {
        return false;
    }
    
    // Wait until we're not rate limited for this request type
    cv_.wait(lock, [this, requestType] {
        // If we're shutting down, stop waiting
        if (!running_) {
            return true;
        }
        
        // Check if we have rate limit status for this request type
        auto statusIt = status_.find(requestType);
        if (statusIt == status_.end()) {
            // If no status exists, we're not rate limited
            return true;
        }
        
        // Check if we're currently rate limited or have no requests remaining
        return !statusIt->second.isLimited && statusIt->second.remainingRequests > 0;
    });
    
    // If we're shutting down, return false
    if (!running_) {
        return false;
    }
    
    // Decrement the remaining requests count
    auto statusIt = status_.find(requestType);
    if (statusIt != status_.end()) {
        statusIt->second.remainingRequests = std::max(0, statusIt->second.remainingRequests - 1);
        if (statusIt->second.remainingRequests == 0) {
            statusIt->second.isLimited = true;
        }
    }
    
    return true;
}
int RateLimitController::getRemainingRequests(ExchangeRequestType requestType) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = status_.find(requestType);
    if (it != status_.end()) {
        return it->second.remainingRequests;
    }
    
    // If we don't have status for this request type, 
    // check if we have a rule for it
    auto ruleIt = rules_.find(requestType);
    if (ruleIt != rules_.end()) {
        // Return the max requests from the rule
        return ruleIt->second.maxRequests;
    }
    
    // If no rule or status exists, return a default value
    return 0;
}

std::chrono::milliseconds RateLimitController::getResetTime(ExchangeRequestType requestType) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = status_.find(requestType);
    if (it != status_.end()) {
        return it->second.resetTime;
    }
    
    // If we don't have status for this request type, return zero
    return std::chrono::milliseconds(0);
}

void RateLimitController::processQueue() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait until we have a request or are shutting down
        cv_.wait(lock, [this] {
            return !running_ || !requestQueue_.empty();
        });
        
        // Check if we're shutting down
        if (!running_ && requestQueue_.empty()) {
            break;
        }
        
        // Try to execute the next request
        bool executed = tryExecuteNextRequest();
        
        // If we couldn't execute any requests, wait for rate limits to reset
        if (!executed && !requestQueue_.empty()) {
            // Find the earliest reset time
            auto minResetTime = std::chrono::steady_clock::now() + std::chrono::hours(1);
            for (const auto& [type, status] : status_) {
                if (status.isLimited) {
                    auto resetTime = std::chrono::steady_clock::now() + status.resetTime;
                    minResetTime = std::min(minResetTime, resetTime);
                }
            }
            
            // Wait until the earliest reset time or until a new request arrives
            cv_.wait_until(lock, minResetTime);
        }
    }
}

void RateLimitController::enqueueRequest(ExchangeRequestType requestType, std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Add the request to the queue
    requestQueue_.push_back({
        requestType,
        std::move(callback),
        std::chrono::steady_clock::now()
    });
    
    // Notify the worker thread
    cv_.notify_one();
}

bool RateLimitController::tryExecuteNextRequest() {
    // Note: Assumes mutex is already locked
    
    if (requestQueue_.empty()) {
        return false;
    }
    
    // Find the first request that's not rate limited
    auto it = requestQueue_.begin();
    while (it != requestQueue_.end()) {
        auto statusIt = status_.find(it->type);
        
        // Check if we have rate limit status for this request type
        if (statusIt == status_.end() || !statusIt->second.isLimited) {
            // Execute the request
            auto callback = std::move(it->callback);
            requestQueue_.erase(it);
            
            // Decrement the remaining requests count
            if (statusIt != status_.end()) {
                statusIt->second.remainingRequests = std::max(0, statusIt->second.remainingRequests - 1);
                if (statusIt->second.remainingRequests == 0) {
                    statusIt->second.isLimited = true;
                }
            }
            
            // Unlock before executing the callback to avoid deadlocks
            mutex_.unlock();
            callback();
            mutex_.lock();
            
            return true;
        }
        
        ++it;
    }
    
    return false;
}

} // namespace execution
} // namespace trading