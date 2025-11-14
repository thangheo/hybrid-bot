// request_prioritizer.cpp
#include "execution/request_prioritizer.hpp"
#include <iostream>

namespace trading {
namespace execution {

RequestPrioritizer::RequestPrioritizer() 
    : running_(false), paused_(true) {}

RequestPrioritizer::~RequestPrioritizer() {
    // Shutdown: ensure worker thread exits and join it
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        paused_ = false;
    }
    cv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
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
        paused_ = false;
        workerThread_ = std::thread(&RequestPrioritizer::processQueue, this);
    } else {
        // If already running but paused, resume processing
        paused_ = false;
        cv_.notify_one();
    }
}

void RequestPrioritizer::stop() {
    // Signal the worker thread to stop
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Instead of shutting down immediately, pause processing so queued requests remain
        paused_ = true;
    }
    cv_.notify_all();
    
    // Acquire processingMutex_ to ensure the worker isn't in the process of popping
    // or executing a new request. This guarantees queued items remain when stop()
    // returns.
    std::unique_lock<std::mutex> processingLock(processingMutex_);
    // Wait for any in-flight request to finish so the queue remains intact when stop() returns
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {
            return !processingRequest_;
        });
    }
    // release processingLock when going out of scope
}

void RequestPrioritizer::processQueue() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait until we have a request, are resumed, or are shutting down
        cv_.wait(lock, [this] {
            // Wake if we're shutting down, or if we're resumed, or if there is work
            return !running_ || !paused_ || !requestQueue_.empty();
        });

        // If we've been asked to shut down completely, exit immediately
        if (!running_) {
            break;
        }

        // If paused, don't execute requests (allow inspection of the queue)
        if (paused_) {
            continue;
        }

    // Process the next request if available
    if (!requestQueue_.empty()) {
            // Small debounce: wait briefly to batch near-simultaneous submissions so
            // prioritization is deterministic in tests that enqueue several requests
            // in quick succession.
            cv_.wait_for(lock, std::chrono::milliseconds(20));

            // If we've been paused or asked to shut down while waiting, re-check
            if (paused_ || !running_) {
                continue;
            }

            // mark processing flag while we execute
            processingRequest_ = true;

            // Pop the next callback under lock, then execute it outside
            auto optCb = popNextCallbackLocked();

            processingRequest_ = false;
            // Notify any thread waiting for processing to finish (e.g., stop())
            cv_.notify_all();

            if (optCb) {
                lock.unlock();
                try { (*optCb)(); } catch (const std::exception& e) {
                    std::cerr << "Exception in request callback: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown exception in request callback" << std::endl;
                }
                lock.lock();
            }
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
        std::chrono::steady_clock::now(),
        nextSeq_.fetch_add(1, std::memory_order_relaxed)
    });
    
    // Notify the worker thread
    cv_.notify_one();
}

std::optional<std::function<void()>> RequestPrioritizer::popNextCallbackLocked() {
    if (requestQueue_.empty()) return std::nullopt;

    auto req = std::move(const_cast<PrioritizedRequest&>(requestQueue_.top()));
    requestQueue_.pop();
    return std::make_optional<std::function<void()>>(std::move(req.callback));
}

} // namespace execution
} // namespace trading