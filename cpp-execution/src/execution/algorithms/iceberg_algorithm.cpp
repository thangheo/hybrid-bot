// iceberg_algorithm.cpp
#include "execution/algorithms/iceberg_algorithm.hpp"
#include <cmath>
#include <iostream>
#include <random>
#include <algorithm>

namespace trading {
namespace execution {
namespace algorithms {
     

IcebergAlgorithm::IcebergAlgorithm() = default;

bool IcebergAlgorithm::initialize(
    const std::string& tradingPair,
    models::OrderSide side,
    const IcebergConfig& config) {
    
    // Lock to prevent concurrent modification
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate configuration
    if (config.total_quantity <= 0) {
        setErrorState("Total quantity must be positive");
        return false;
    }
    
    if (config.visible_quantity <= 0) {
        setErrorState("Visible quantity must be positive");
        return false;
    }
    
    if (config.visible_quantity > config.total_quantity) {
        setErrorState("Visible quantity cannot be greater than total quantity");
        return false;
    }
    
    if (config.price <= 0) {
        setErrorState("Price must be positive");
        return false;
    }
    
    // Store parameters
    trading_pair_ = tradingPair;
    side_ = side;
    config_ = config;
    state_ = State::INITIALIZED;
    executed_quantity_ = 0.0;
    remaining_quantity_ = config.total_quantity;
    average_execution_price_ = 0.0;
    current_price_ = config.price;
    
    // Clear any existing child orders
    child_orders_.clear();
    current_order_.reset();
    
    return true;
}

bool IcebergAlgorithm::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::INITIALIZED && state_ != State::PAUSED) {
        return false;
    }
    
    state_ = State::RUNNING;
    
    // Create and send the first slice
    createAndSendNextSlice();
    
    return true;
}

bool IcebergAlgorithm::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::RUNNING) {
        return false;
    }
    
    state_ = State::PAUSED;
    return true;
}

bool IcebergAlgorithm::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::PAUSED) {
        return false;
    }
    
    state_ = State::RUNNING;
    
    // If we don't have an active order, create one
    if (!current_order_ || current_order_->getState() == models::OrderState::FILLED ||
        current_order_->getState() == models::OrderState::CANCELED ||
        current_order_->getState() == models::OrderState::REJECTED) {
        createAndSendNextSlice();
    }
    
    return true;
}

bool IcebergAlgorithm::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == State::COMPLETED || state_ == State::CANCELLED) {
        return false;
    }
    
    // Cancel active order
    if (current_order_ && (current_order_->getState() == models::OrderState::OPEN ||
                         current_order_->getState() == models::OrderState::PARTIALLY_FILLED)) {
        // In a real implementation, we would call the order cancellation API here
    }
    
    state_ = State::CANCELLED;
    
    // Notify completion with cancelled status
    if (completion_callback_) {
        completion_callback_(false, "Algorithm cancelled by user");
    }
    
    return true;
}
//execute a parent order using Iceberg algorithm
ExecutionResponse IcebergAlgorithm::execute(
    const models::Order& parentOrder,
    ExecutionService& execution_service) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate parent order
    if (parentOrder.getState() != models::OrderState::OPEN) {
        setErrorState("Parent order must be in OPEN state");
        return ExecutionResponse{
                parentOrder.getClientOrderId(),
                parentOrder.getExchangeOrderId(),
                false,
                "Parent order must be in OPEN state",
                std::chrono::system_clock::now(),
                "",
            };
    }
    
    // Initialize algorithm with parent order parameters
    trading_pair_ = parentOrder.getTradingPair();
    side_ = parentOrder.getSide();
    
    // Start the algorithm
    return start() ? ExecutionResponse{
            parentOrder.getClientOrderId(),
            parentOrder.getExchangeOrderId(),
            true,
            "Iceberg algorithm started",
            std::chrono::system_clock::now(),
            "RUNNING",
        } : ExecutionResponse{
            parentOrder.getClientOrderId(),
            parentOrder.getExchangeOrderId(),
            false,
            "Failed to start Iceberg algorithm: " + error_message_,
            std::chrono::system_clock::now(),
            "ERROR",
        };
}
void IcebergAlgorithm::setOrderCallback(const OrderCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    order_callback_ = callback;
}

void IcebergAlgorithm::setCompletionCallback(const CompletionCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    completion_callback_ = callback;
}

void IcebergAlgorithm::handleOrderUpdate(const models::OrderPtr& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if this is our current order
    if (current_order_ && order->getOrderId() == current_order_->getOrderId()) {
        // Update our copy with the latest state
        current_order_ = order;
        
        // Add to child orders if not already there
        auto it = std::find_if(child_orders_.begin(), child_orders_.end(),
            [&order](const models::OrderPtr& child) {
                return child->getOrderId() == order->getOrderId();
            });
        
        if (it == child_orders_.end()) {
            child_orders_.push_back(order);
        } else {
            *it = order;
        }
        
        // Update execution statistics
        updateExecutionStatistics();
        
        // Check order state to see if we need to create a new slice
        if (state_ == State::RUNNING &&
            (order->getState() == models::OrderState::FILLED ||
             order->getState() == models::OrderState::CANCELED ||
             order->getState() == models::OrderState::REJECTED)) {
            // Create next slice if we have remaining quantity
            if (remaining_quantity_ > 0) {
                createAndSendNextSlice();
            } else {
                checkCompletion();
            }
        } else {
            checkCompletion();
        }
    }
}

bool IcebergAlgorithm::isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::RUNNING;
}

bool IcebergAlgorithm::isComplete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::COMPLETED;
}

bool IcebergAlgorithm::isCancelled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::CANCELLED;
}

double IcebergAlgorithm::getTotalQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.total_quantity;
}

double IcebergAlgorithm::getExecutedQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return executed_quantity_;
}

double IcebergAlgorithm::getRemainingQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return remaining_quantity_;
}

double IcebergAlgorithm::getAverageExecutionPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return average_execution_price_;
}

double IcebergAlgorithm::getCurrentPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_price_;
}

models::OrderPtr IcebergAlgorithm::getCurrentOrder() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_order_;
}

std::vector<models::OrderPtr> IcebergAlgorithm::getChildOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return child_orders_;
}

void IcebergAlgorithm::createAndSendNextSlice() {
    // Don't create a new slice if we're not running or have no remaining quantity
    if (state_ != State::RUNNING || remaining_quantity_ <= 0) {
        checkCompletion();
        return;
    }
    
    // Calculate visible quantity for this slice
    double visible_qty = calculateVisibleQuantity();
    visible_qty = std::min(visible_qty, remaining_quantity_);
    
    // Calculate price for this slice
    current_price_ = calculateNextPrice();
    
    // Create a new order for this slice
    auto order = std::make_shared<models::Order>();
    order->setTradingPair(trading_pair_);
    order->setSide(side_);
    order->setType(models::OrderType::LIMIT);
    order->setPrice(current_price_);
    order->setQuantity(visible_qty);
    
    // Set as current order
    current_order_ = order;
    
    // Increment slice count
    slice_count_++;
    
    // Notify callback
    if (order_callback_) {
        order_callback_(order);
    }
}

double IcebergAlgorithm::calculateVisibleQuantity() {
    double visible_qty = config_.visible_quantity;
    
    // Apply randomization if enabled
    if (config_.dynamic_visible_qty) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(1.0 - config_.rand_factor, 1.0 + config_.rand_factor);
        visible_qty *= dis(gen);
    }
    
    // Ensure visible quantity is not greater than remaining quantity
    return std::min(visible_qty, remaining_quantity_);
}

double IcebergAlgorithm::calculateNextPrice() {
    double next_price = config_.price;
    
    // If price adjustment is enabled and we've already placed at least one order
    if (config_.price_delta != 0.0 && slice_count_ > 0) {
        // Apply price delta
        if (side_ == models::OrderSide::BUY) {
            // For buy orders, negative delta moves price down
            next_price -= config_.price_delta * slice_count_;
        } else {
            // For sell orders, negative delta moves price up
            next_price += config_.price_delta * slice_count_;
        }
    }
    
    // If we adjust price based on fills and have executed some quantity
    if (config_.adjust_price_on_fill && executed_quantity_ > 0) {
        // In a real implementation, this would use market data to adjust the price
        // based on current market conditions and recent fills
    }
    
    // Ensure price is positive
    return std::max(next_price, 0.000001);
}

void IcebergAlgorithm::updateExecutionStatistics() {
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
    
    remaining_quantity_ = config_.total_quantity - executed_quantity_;
}

void IcebergAlgorithm::checkCompletion() {
    double epsilon = 0.000001; // Small epsilon to handle floating point errors
    bool quantity_complete = remaining_quantity_ <= epsilon;
    
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
    
    // Algorithm is complete if we've executed all quantity and all orders are in a terminal state
    if (quantity_complete && all_orders_complete) {
        if (state_ != State::COMPLETED && state_ != State::CANCELLED) {
            state_ = State::COMPLETED;
            
            // Notify completion
            if (completion_callback_) {
                completion_callback_(true, "Algorithm completed successfully");
            }
        }
    }
}

void IcebergAlgorithm::setErrorState(const std::string& error_message) {
    state_ = State::ERROR;
    error_message_ = error_message;
    
    // Notify completion with error
    if (completion_callback_) {
        completion_callback_(false, error_message);
    }
}

} // namespace algorithms
} // namespace execution
} // namespace trading