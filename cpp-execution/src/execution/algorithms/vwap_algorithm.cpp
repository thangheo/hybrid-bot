// algorithms/vwap_algorithm.cpp
#include "execution/algorithms/vwap_algorithm.hpp"
#include "execution/algorithms/timer.hpp"
#include "execution/execution_service.hpp"
#include <cmath>
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>

namespace trading {
namespace execution {
namespace algorithms {


VwapAlgorithm::VwapAlgorithm() = default;

bool VwapAlgorithm::initialize(
    const core::Symbol& tradingPair,
    models::OrderSide side,
    const VwapConfig& config) {
    
    // Lock to prevent concurrent modification
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate configuration
    if (config.total_quantity <= 0) {
        setErrorState("Total quantity must be positive");
        return false;
    }
    
    if (config.duration.count() <= 0) {
        setErrorState("Duration must be positive");
        return false;
    }
    
    if (config.volume_profile.empty()) {
        setErrorState("Volume profile must not be empty");
        return false;
    }
    
    // Normalize volume profile if it doesn't sum to 1.0
    volume_profile_ = config.volume_profile;
    double sum = std::accumulate(volume_profile_.begin(), volume_profile_.end(), 0.0);
    if (std::abs(sum - 1.0) > 0.001) {
        for (auto& volume : volume_profile_) {
            volume /= sum;
        }
    }
    
    // Store parameters
    trading_pair_ = tradingPair;
    side_ = side;
    config_ = config;
    state_ = State::INITIALIZED;
    executed_quantity_ = 0.0;
    average_execution_price_ = 0.0;
    
    // Clear any existing child orders
    child_orders_.clear();
    
    return true;
}

void VwapAlgorithm::setVolumeProfile(const std::vector<double>& volume_profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (volume_profile.empty()) {
        return;
    }
    
    // Normalize if needed
    volume_profile_ = volume_profile;
    double sum = std::accumulate(volume_profile_.begin(), volume_profile_.end(), 0.0);
    if (std::abs(sum - 1.0) > 0.001) {
        for (auto& volume : volume_profile_) {
            volume /= sum;
        }
    }
    
    // Reinitialize time buckets if algorithm is already running
    if (state_ == State::RUNNING) {
        initializeTimeBuckets();
    }
}

void VwapAlgorithm::setVolumeCallback(const VolumeCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    volume_callback_ = callback;
}

bool VwapAlgorithm::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::INITIALIZED && state_ != State::PAUSED) {
        return false;
    }
    
    // Setup execution timing
    start_time_ = std::chrono::system_clock::now();
    end_time_ = start_time_ + config_.duration;
    
    // Initialize time buckets based on volume profile
    initializeTimeBuckets();
    
    // Start processing buckets
    state_ = State::RUNNING;
    scheduleNextBucket();
    
    return true;
}
//execute the algorithm with a parent order
ExecutionResponse VwapAlgorithm::execute(const models::Order& parentOrder, ExecutionService& execution_service) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::RUNNING) {
        return ExecutionResponse{
            parentOrder.getClientOrderId(),
            parentOrder.getExchangeOrderId(),
            false,
            "Algorithm not running",
            std::chrono::system_clock::now(),
            ""
        };
    }
    
    // Create child orders based on the volume profile
    for (const auto& bucket : time_buckets_) {
        if (!bucket.processed) {
            double target_quantity = bucket.target_quantity - bucket.executed_quantity;
            if (target_quantity > 0) {
                auto child_order = std::make_shared<models::Order>();
                child_order->setOrderId(parentOrder.getOrderId());
                child_order->setClientOrderId(parentOrder.getClientOrderId());
                child_order->setTradingPair(trading_pair_);
                child_order->setSide(side_);
                child_order->setQuantity(target_quantity);
                child_order->setPrice(config_.price_limit);
                
                // Place the order through the execution service
                execution_service.placeOrder(*child_order);
                
                // Store the order
                child_orders_.push_back(child_order);
                
                // Update executed quantity
                executed_quantity_ += target_quantity;
                
                // Call order callback if set
                if (order_callback_) {
                    order_callback_(child_order);
                }
            }
        }
    }
    
    return ExecutionResponse{
        parentOrder.getClientOrderId(),
        parentOrder.getExchangeOrderId(),
        true,
        "",
        std::chrono::system_clock::now(),
        "RUNNING"
    };
}

bool VwapAlgorithm::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::RUNNING) {
        return false;
    }
    
    if (timer_) {
        timer_->stop();
    }
    
    state_ = State::PAUSED;
    return true;
}

bool VwapAlgorithm::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::PAUSED) {
        return false;
    }
    
    if (timer_) {
        timer_->start();
    } else {
        scheduleNextBucket();
    }
    
    state_ = State::RUNNING;
    return true;
}

bool VwapAlgorithm::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == State::COMPLETED || state_ == State::CANCELLED) {
        return false;
    }
    
    if (timer_) {
        timer_->stop();
    }
    
    // Cancel active orders
    for (const auto& order : child_orders_) {
        if (order->getState() == models::OrderState::OPEN ||
            order->getState() == models::OrderState::PARTIALLY_FILLED) {
            // In a real implementation, we would call the order cancellation API here
        }
    }
    
    state_ = State::CANCELLED;
    
    // Notify completion with cancelled status
    if (completion_callback_) {
        completion_callback_(false, "Algorithm cancelled by user");
    }
    
    return true;
}

void VwapAlgorithm::setOrderCallback(const OrderCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    order_callback_ = callback;
}

void VwapAlgorithm::setCompletionCallback(const CompletionCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    completion_callback_ = callback;
}

void VwapAlgorithm::handleOrderUpdate(const models::OrderPtr& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find the order in our child orders
    auto it = std::find_if(child_orders_.begin(), child_orders_.end(),
        [&order](const models::OrderPtr& child) {
            return child->getOrderId() == order->getOrderId();
        });
    
    if (it != child_orders_.end()) {
        // Update our copy with the latest state
        *it = order;
        
        // Update execution statistics
        updateExecutionStatistics();
        
        // Check if we need to create more orders or if we're done
        checkCompletion();
    }
}

bool VwapAlgorithm::isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::RUNNING;
}

bool VwapAlgorithm::isComplete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::COMPLETED;
}

bool VwapAlgorithm::isCancelled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::CANCELLED;
}

double VwapAlgorithm::getTotalQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.total_quantity;
}

double VwapAlgorithm::getExecutedQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return executed_quantity_;
}

double VwapAlgorithm::getRemainingQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.total_quantity - executed_quantity_;
}

double VwapAlgorithm::getAverageExecutionPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return average_execution_price_;
}

std::chrono::seconds VwapAlgorithm::getRemainingTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == State::COMPLETED || state_ == State::CANCELLED) {
        return std::chrono::seconds(0);
    }
    
    auto now = std::chrono::system_clock::now();
    if (now >= end_time_) {
        return std::chrono::seconds(0);
    }
    
    return std::chrono::duration_cast<std::chrono::seconds>(end_time_ - now);
}

double VwapAlgorithm::getVwapDeviationPercent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    double market_vwap = calculateMarketVwap();
    if (market_vwap <= 0 || average_execution_price_ <= 0) {
        return 0.0;
    }
    
    return (average_execution_price_ - market_vwap) / market_vwap * 100.0;
}

std::vector<models::OrderPtr> VwapAlgorithm::getChildOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return child_orders_;
}

void VwapAlgorithm::initializeTimeBuckets() {
    time_buckets_.clear();
    
    // Calculate duration of each bucket
    int num_buckets = static_cast<int>(volume_profile_.size());
    auto bucket_duration = config_.duration / num_buckets;
    
    // Create time buckets based on volume profile
    for (int i = 0; i < num_buckets; ++i) {
        TimeBucket bucket;
        bucket.start_time = start_time_ + (bucket_duration * i);
        bucket.end_time = start_time_ + (bucket_duration * (i + 1));
        bucket.target_quantity = config_.total_quantity * volume_profile_[i];
        bucket.executed_quantity = 0.0;
        bucket.processed = false;
        
        time_buckets_.push_back(bucket);
    }
}

void VwapAlgorithm::scheduleNextBucket() {
    // Find the next unprocessed bucket
    size_t next_bucket_index = time_buckets_.size();
    auto now = std::chrono::system_clock::now();
    
    for (size_t i = 0; i < time_buckets_.size(); ++i) {
        if (!time_buckets_[i].processed && now < time_buckets_[i].end_time) {
            next_bucket_index = i;
            break;
        }
    }
    
    if (next_bucket_index >= time_buckets_.size()) {
        // No more buckets to process
        checkCompletion();
        return;
    }
    
    // Schedule processing of the next bucket
    auto& bucket = time_buckets_[next_bucket_index];
    auto delay = bucket.start_time > now ? 
                 std::chrono::duration_cast<std::chrono::milliseconds>(bucket.start_time - now) :
                 std::chrono::milliseconds(0);
    
    // Create and start timer
    timer_ = std::make_unique<Timer>(
        [this, next_bucket_index]() { processBucket(next_bucket_index); },
        delay
    );
    timer_->start();
}

void VwapAlgorithm::processBucket(size_t bucket_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::RUNNING || bucket_index >= time_buckets_.size()) {
        return;
    }
    
    auto& bucket = time_buckets_[bucket_index];
    if (bucket.processed) {
        return;
    }
    
    // Calculate optimal order size for this bucket
    double order_size = calculateOptimalOrderSize(bucket);
    if (order_size <= 0) {
        bucket.processed = true;
        scheduleNextBucket();
        return;
    }
    
    // Create a new order for this bucket
    auto order = std::make_shared<models::Order>();
    order->setTradingPair(trading_pair_);
    order->setSide(side_);
    
    if (config_.price_limit > 0) {
        order->setType(models::OrderType::LIMIT);
        order->setPrice(config_.price_limit);
    } else {
        order->setType(models::OrderType::MARKET);
    }
    
    order->setQuantity(order_size);
    
    // Add to our child orders
    child_orders_.push_back(order);
    
    // Mark bucket as processed
    bucket.processed = true;
    
    // Notify callback
    if (order_callback_) {
        order_callback_(order);
    }
    
    // Schedule next bucket
    scheduleNextBucket();
}

double VwapAlgorithm::calculateOptimalOrderSize(const TimeBucket& bucket) {
    // Base size on target quantity for this bucket
    double remaining_quantity = config_.total_quantity - executed_quantity_;
    double target_size = std::min(bucket.target_quantity, remaining_quantity);
    
    if (target_size <= 0) {
        return 0.0;
    }
    
    // If we have a volume callback and dynamic sizing is enabled, adjust based on actual market volume
    if (volume_callback_ && config_.dynamic_sizing) {
        // Get actual market volume for this bucket
        auto bucket_duration = std::chrono::duration_cast<std::chrono::minutes>(bucket.end_time - bucket.start_time);
        double market_volume = volume_callback_(trading_pair_.toString(), bucket_duration);

        if (market_volume > 0) {
            // Limit our participation based on max rate
            double max_order_size = market_volume * config_.max_participation_rate;
            target_size = std::min(target_size, max_order_size);
        }
    }
    
    return target_size;
}

void VwapAlgorithm::updateExecutionStatistics() {
    executed_quantity_ = 0.0;
    double total_filled_value = 0.0;
    
    for (const auto& order : child_orders_) {
        double filled_qty = order->getFilledQuantity();
        executed_quantity_ += filled_qty;
        total_filled_value += filled_qty * order->getAverageFillPrice();
    }
    
    if (executed_quantity_ > 0) {
        average_execution_price_ = total_filled_value / executed_quantity_;
    }
}

void VwapAlgorithm::checkCompletion() {
    auto now = std::chrono::system_clock::now();
    bool time_complete = now >= end_time_;
    double remaining_quantity = config_.total_quantity - executed_quantity_;
    bool quantity_complete = remaining_quantity <= 0.000001; // Small epsilon to handle floating point errors
    
    // Check if all orders are completed
    bool all_orders_complete = true;
    for (const auto& order : child_orders_) {
        auto state = order->getState();
        if (state != models::OrderState::FILLED &&
            state != models::OrderState::CANCELED &&
            state != models::OrderState::REJECTED) {
            all_orders_complete = false;
            break;
        }
    }
    
    // Check if all buckets are processed
    bool all_buckets_processed = true;
    for (const auto& bucket : time_buckets_) {
        if (!bucket.processed) {
            all_buckets_processed = false;
            break;
        }
    }
    
    // Algorithm is complete if we've reached the end time or executed all quantity
    // and all orders are in a terminal state and all buckets are processed
    if ((time_complete || quantity_complete) && all_orders_complete && all_buckets_processed) {
        if (state_ != State::COMPLETED && state_ != State::CANCELLED) {
            state_ = State::COMPLETED;
            
            if (timer_) {
                timer_->stop();
            }
            
            // Notify completion
            if (completion_callback_) {
                completion_callback_(true, "Algorithm completed successfully");
            }
        }
    }
}

void VwapAlgorithm::setErrorState(const std::string& error_message) {
    state_ = State::ERROR;
    error_message_ = error_message;
    
    // Notify completion with error
    if (completion_callback_) {
        completion_callback_(false, error_message);
    }
}

double VwapAlgorithm::calculateMarketVwap() const {
    // In a real implementation, this would query actual market data
    // For now, we'll just return our own average price if we have fills
    if (executed_quantity_ > 0) {
        return average_execution_price_;
    }
    
    return 0.0;
}

} // namespace algorithms
} // namespace execution
} // namespace trading