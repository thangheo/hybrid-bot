// request_prioritizer.cpp
#include "execution/request_prioritizer.hpp"
#include <iostream>

namespace trading {
namespace execution {

RequestPrioritizer::RequestPrioritizer() : running_(true) {
    // Start the worker thread
    start();
}

RequestPrioritizer::~RequestPrioritizer() {
    // Stop the worker thread
    stop();
}

bool RequestPrioritizer::hasPendingRequests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !requestQueue_.empty();
}

std::unordered_map<RequestPriority, size_t> RequestPrioritizer::getQueueSizes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::unordered_map<RequestPriority, size_t> result;
    
    // Count requests by priority
    // This is inefficient for a priority queue, but we only use it for metrics
    std::priority_queue<PrioritizedRequest> tempQueue = requestQueue_;
    while (!tempQueue.empty()) {
        const auto& request = tempQueue.top();
        result[request.priority]++;
        tempQueue.pop();
    }
    
    return result;
}

void RequestPrioritizer::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Only start if not already running
    if (!running_) {
        running_ = true;
        workerThread_ = std::thread(&RequestPrioritizer::processQueue, this);
    }
}

void RequestPrioritizer::stop() {
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

void RequestPrioritizer::processQueue() {
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
        
        // Process the next request if available
        if (!requestQueue_.empty()) {
            executeNextRequest();
        }
    }
}

void RequestPrioritizer::enqueueRequest(RequestPriority priority, ExchangeRequestType requestType, 
                                     std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Add the request to the priority queue
    requestQueue_.push({
        priority,
        requestType,
        std::move(callback),
        std::chrono::steady_clock::now()
    });
    
    // Notify the worker thread
    cv_.notify_one();
}

void RequestPrioritizer::executeNextRequest() {
    // Note: Assumes mutex is already locked
    
    if (requestQueue_.empty()) {
        return;
    }
    
    // Get the highest priority request
    auto request = std::move(const_cast<PrioritizedRequest&>(requestQueue_.top()));
    requestQueue_.pop();
    
    // Unlock the mutex before executing the callback to avoid deadlocks
    mutex_.unlock();
    
    try {
        // Execute the callback
        request.callback();
    } catch (const std::exception& e) {
        std::cerr << "Exception in request callback: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in request callback" << std::endl;
    }
    
    // Lock the mutex again
    mutex_.lock();
}

} // namespace execution
} // namespace trading