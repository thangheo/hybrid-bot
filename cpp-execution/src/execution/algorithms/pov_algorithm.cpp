// pov_algorithm.cpp implementation

#include "execution/algorithms/pov_algorithm.hpp"
#include "execution/execution_service.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace trading {
namespace execution {
namespace algorithms {

PovAlgorithm::PovAlgorithm() = default;

PovAlgorithm::~PovAlgorithm() {
    // Ensure the execution thread is properly stopped
    cancel();
    
    // Wait for thread to join if it's running
    if (execution_thread_.joinable()) {
        execution_thread_.join();
    }
}

bool PovAlgorithm::initialize(
    const ,
    models::OrderSide side,
    const PovConfig& config) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate configuration
    if (config.total_quantity <= 0) {
        setErrorState("Invalid total quantity");
        return false;
    }
    
    if (config.participation_rate <= 0 || config.participation_rate > 1.0) {
        setErrorState("Invalid participation rate (must be between 0 and 1)");
        return false;
    }
    
    // Store configuration
    trading_pair_ = tradingPair;
    side_ = side;
    config_ = config;
    
    // Reset execution state
    executed_quantity_ = 0.0;
    average_execution_price_ = 0.0;
    child_orders_.clear();
    state_ = State::INITIALIZED;
    error_message_.clear();
    
    return true;
}

bool PovAlgorithm::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == State::RUNNING) {
        return true; // Already running
    }
    
    if (state_ == State::ERROR) {
        return false; // Cannot start due to error
    }
    
    if (state_ == State::COMPLETED || state_ == State::CANCELLED) {
        return false; // Cannot restart completed or cancelled algorithm
    }
    
    // Set start time and end time
    start_time_ = std::chrono::system_clock::now();
    last_check_time_ = start_time_;
    
    if (config_.duration.count() > 0) {
        end_time_ = start_time_ + config_.duration;
    } else {
        // If no duration specified, set a very distant end time
        end_time_ = start_time_ + std::chrono::hours(24);
    }
    
    // Reset control flags
    should_stop_ = false;
    is_paused_ = false;
    
    // Start execution thread
    state_ = State::RUNNING;
    
    // Join any existing thread
    if (execution_thread_.joinable()) {
        execution_thread_.join();
    }
    
    // Start new execution thread
    execution_thread_ = std::thread(&PovAlgorithm::executionLoop, this);
    
    return true;
}

bool PovAlgorithm::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::RUNNING) {
        return false; // Can only pause if running
    }
    
    is_paused_ = true;
    state_ = State::PAUSED;
    return true;
}

bool PovAlgorithm::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::PAUSED) {
        return false; // Can only resume if paused
    }
    
    is_paused_ = false;
    state_ = State::RUNNING;
    
    // Notify execution thread to continue
    pause_cv_.notify_one();
    
    return true;
}

bool PovAlgorithm::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == State::COMPLETED || state_ == State::CANCELLED) {
        return true; // Already completed or cancelled
    }
    
    should_stop_ = true;
    is_paused_ = false;
    state_ = State::CANCELLED;
    
    // Notify execution thread to stop
    pause_cv_.notify_one();
    
    // Notify completion if callback is registered
    if (completion_callback_) {
        completion_callback_(false, "Algorithm cancelled");
    }
    
    return true;
}

ExecutionResponse PovAlgorithm::execute(
    const models::Order& parentOrder,
    ExecutionService& execution_service) {
    
    // This method would be called if the algorithm is executed directly
    // through the ExecutionAlgorithm interface. For POV, we typically
    // start the algorithm separately and then place child orders as needed.
    
    ExecutionResponse response;
    response.success = true;
    // response.message = "POV algorithm started";
    
    // Start the algorithm if not already running
    if (state_ != State::RUNNING) {
        if (!start()) {
            response.success = false;
            response.client_order_id = parentOrder.getClientOrderId();
            response.exchange_order_id = parentOrder.getExchangeOrderId();
            response.timestamp = std::chrono::system_clock::now();
            response.error_message = "Failed to start POV algorithm: " + error_message_;
            response.order_status = "ERROR";
            return response;
        }
    }
    
    return response;
}

void PovAlgorithm::setOrderCallback(const OrderCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    order_callback_ = callback;
}

void PovAlgorithm::setCompletionCallback(const CompletionCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    completion_callback_ = callback;
}

void PovAlgorithm::setMarketVolumeCallback(const MarketVolumeCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    market_volume_callback_ = callback;
}

void PovAlgorithm::handleOrderUpdate(const models::OrderPtr& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find the order in our child orders
    auto it = std::find_if(
        child_orders_.begin(),
        child_orders_.end(),
        [&order](const models::OrderPtr& child) {
            return child->getClientOrderId() == order->getClientOrderId();
        });
    
    if (it != child_orders_.end()) {
        // Update the child order with the new information
        *it = order;
        
        // Update execution statistics
        updateExecutionStatistics();
        
        // Check if we've completed the total quantity
        checkCompletion();
    }
}

bool PovAlgorithm::isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::RUNNING;
}

bool PovAlgorithm::isComplete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::COMPLETED;
}

bool PovAlgorithm::isCancelled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::CANCELLED;
}

double PovAlgorithm::getTotalQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.total_quantity;
}

double PovAlgorithm::getExecutedQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return executed_quantity_;
}

double PovAlgorithm::getRemainingQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.total_quantity - executed_quantity_;
}

double PovAlgorithm::getAverageExecutionPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return average_execution_price_;
}

std::chrono::seconds PovAlgorithm::getRemainingTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    if (now >= end_time_) {
        return std::chrono::seconds(0);
    }
    
    return std::chrono::duration_cast<std::chrono::seconds>(end_time_ - now);
}

std::vector<models::OrderPtr> PovAlgorithm::getChildOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return child_orders_;
}

void PovAlgorithm::executionLoop() {
    while (!should_stop_) {
        // Handle paused state
        if (is_paused_) {
            std::unique_lock<std::mutex> lock(mutex_);
            pause_cv_.wait(lock, [this] { return !is_paused_ || should_stop_; });
            continue;
        }
        
        // Check if we've reached the end time
        auto now = std::chrono::system_clock::now();
        if (now >= end_time_) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // If we still have quantity to execute, place one final order
            if (executed_quantity_ < config_.total_quantity) {
                double remaining = config_.total_quantity - executed_quantity_;
                placeChildOrder(remaining);
            }
            
            checkCompletion();
            
            // Stop the execution loop
            should_stop_ = true;
            continue;
        }
        
        // Check if it's time to place a new order based on check interval
        if (now - last_check_time_ >= config_.check_interval) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Get current market volume
            double market_volume = getMarketVolume();
            last_market_volume_ = market_volume;
            
            // Calculate order quantity based on market volume and participation rate
            if (market_volume > 0) {
                // Determine how much to execute based on participation rate
                double target_quantity = market_volume * config_.participation_rate;
                
                // Adjust for remaining quantity
                double remaining = config_.total_quantity - executed_quantity_;
                target_quantity = std::min(target_quantity, remaining);
                
                // Apply min/max order size constraints
                if (config_.min_order_size > 0) {
                    target_quantity = std::max(target_quantity, config_.min_order_size);
                }
                
                if (config_.max_order_size > 0) {
                    target_quantity = std::min(target_quantity, config_.max_order_size);
                }
                
                // Ensure we don't exceed the total quantity
                target_quantity = std::min(target_quantity, remaining);
                
                // Place the order if quantity is significant
                if (target_quantity > 0.000001) {
                    placeChildOrder(target_quantity);
                }
            }
            
            // Update last check time
            last_check_time_ = now;
        }
        
        // Sleep until next check interval
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

double PovAlgorithm::getMarketVolume() {
    // Call the market volume callback if registered
    if (market_volume_callback_) {
        return market_volume_callback_(trading_pair_.toString(), config_.volume_lookback);
    }
    
    // Default to a small volume if no callback is registered
    // This ensures some trading activity even without market data
    return 10.0;
}

void PovAlgorithm::placeChildOrder(double quantity) {
    // Create a new child order
    auto order = std::make_shared<models::Order>();
    
    // Set order parameters
    order->setClientOrderId("POV-" + std::to_string(child_orders_.size() + 1));
    // order->setOrderId("POV-" + std::to_string(child_orders_.size() + 1));
    order->setTradingPair(trading_pair_);
    order->setSide(side_);
    order->setType(models::OrderType::LIMIT);
    order->setQuantity(quantity);
    order->setPrice(config_.price_limit);
    
    // Add the order to our list of child orders
    child_orders_.push_back(order);
    
    // Notify through callback
    if (order_callback_) {
        order_callback_(order);
    }
}

void PovAlgorithm::updateExecutionStatistics() {
    executed_quantity_ = 0.0;
    double total_cost = 0.0;
    
    for (const auto& order : child_orders_) {
        double filled = order->getFilledQuantity();
        executed_quantity_ += filled;
        total_cost += filled * order->getAverageFillPrice();
    }
    
    if (executed_quantity_ > 0) {
        average_execution_price_ = total_cost / executed_quantity_;
    }
}

void PovAlgorithm::checkCompletion() {
    // Check if we've executed the full quantity
    if (std::abs(executed_quantity_ - config_.total_quantity) < 0.000001) {
        state_ = State::COMPLETED;
        should_stop_ = true;
        
        // Notify completion
        if (completion_callback_) {
            completion_callback_(true, "POV execution completed successfully");
        }
    }
}

void PovAlgorithm::setErrorState(const std::string& error_message) {
    state_ = State::ERROR;
    error_message_ = error_message;
    
    // Log the error
    std::cerr << "POV Algorithm Error: " << error_message << std::endl;
}

} // namespace algorithms
} // namespace execution
} // namespace trading