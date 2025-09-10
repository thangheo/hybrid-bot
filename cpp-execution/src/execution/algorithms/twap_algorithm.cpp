// twap_algorithm.cpp
#include "execution/algorithms/twap_algorithm.hpp"
#include "execution/algorithms/timer.hpp"
#include "execution/execution_service.hpp"
#include <cmath>
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>

namespace trading {
namespace execution {
namespace algorithms {

    


TwapAlgorithm::TwapAlgorithm() = default;

bool TwapAlgorithm::initialize(
    const core::Symbol& tradingPair,
    models::OrderSide side,
    const TwapConfig& config) {
    
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
    
    if (config.slice_interval.count() <= 0) {
        setErrorState("Slice interval must be positive");
        return false;
    }
    
    if (config.slice_interval > config.duration) {
        setErrorState("Slice interval cannot be greater than total duration");
        return false;
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

bool TwapAlgorithm::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::INITIALIZED && state_ != State::PAUSED) {
        return false;
    }
    
    // Setup execution timing
    start_time_ = std::chrono::system_clock::now();
    end_time_ = start_time_ + config_.duration;
    
    // Start slicing timer
    if (!timer_) {
        timer_ = std::make_unique<Timer>(
            [this]() { createAndSendSlice(); },
            std::chrono::duration_cast<std::chrono::milliseconds>(config_.slice_interval)
        );
    }
    
    state_ = State::RUNNING;
    timer_->start();
    
    // Create and send first slice immediately
    createAndSendSlice();
    
    return true;
}

bool TwapAlgorithm::pause() {
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

bool TwapAlgorithm::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::PAUSED) {
        return false;
    }
    
    if (timer_) {
        timer_->start();
    }
    
    state_ = State::RUNNING;
    return true;
}

bool TwapAlgorithm::cancel() {
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
// void TwapAlgorithm::checkCompletion() {
//     std::lock_guard<std::mutex> lock(mutex_);
    
//     if (state_ == State::RUNNING) {
//         if (executed_quantity_ >= config_.total_quantity) {
//             state_ = State::COMPLETED;
//             timer_->stop();
            
//             // Notify completion with success status
//             if (completion_callback_) {
//                 completion_callback_(true, "Algorithm completed successfully");
//             }
//         } else if (std::chrono::system_clock::now() >= end_time_) {
//             state_ = State::COMPLETED;
//             timer_->stop();
            
//             // Notify completion with timeout status
//             if (completion_callback_) {
//                 completion_callback_(false, "Algorithm timed out");
//             }
//         }
//     }
// }
// Add this helper method to TwapAlgorithm class
// Thêm implementation cho method này
void TwapAlgorithm::setVolumeCallback(
    const std::function<double(const std::string&, std::chrono::minutes)>& callback) {
    volume_callback_ = callback;
}
TwapConfig TwapAlgorithm::extractConfigFromOrder(const models::Order& parentOrder) {
    TwapConfig config;
    
    // Set total quantity from parent order
    config.total_quantity = parentOrder.getQuantity();
    
    // Set price limit from parent order for limit orders
    if (parentOrder.getType() == models::OrderType::LIMIT) {
        config.price_limit = parentOrder.getPrice();
    }
    
    // Extract additional parameters from extra_params
    const auto& params = parentOrder.getExtraParams();
    
    // Extract duration (in seconds)
    auto durationIt = params.find("twap_duration_seconds");
    if (durationIt != params.end() && !durationIt->second.empty()) {
        try {
            config.duration = std::chrono::seconds(std::stoi(durationIt->second));
        } catch (const std::exception& e) {
            // Use default if conversion fails
            config.duration = std::chrono::seconds(3600); // 1 hour default
        }
    } else {
        config.duration = std::chrono::seconds(3600); // 1 hour default
    }
    
    // Extract slice interval (in seconds)
    auto intervalIt = params.find("twap_slice_interval_seconds");
    if (intervalIt != params.end() && !intervalIt->second.empty()) {
        try {
            config.slice_interval = std::chrono::seconds(std::stoi(intervalIt->second));
        } catch (const std::exception& e) {
            // Use default if conversion fails
            config.slice_interval = std::chrono::seconds(300); // 5 minutes default
        }
    } else {
        config.slice_interval = std::chrono::seconds(300); // 5 minutes default
    }
    
    // Extract price deviation percentage
    auto deviationIt = params.find("twap_deviation_pct");
    if (deviationIt != params.end() && !deviationIt->second.empty()) {
        try {
            config.deviation_pct = std::stod(deviationIt->second);
        } catch (const std::exception& e) {
            config.deviation_pct = 0.0; // Default to no deviation tolerance
        }
    }
    
    // Extract allow partial setting
    auto partialIt = params.find("twap_allow_partial");
    if (partialIt != params.end()) {
        config.allow_partial = (partialIt->second == "true" || partialIt->second == "1");
    }
    
    return config;
}
ExecutionResponse TwapAlgorithm::execute(const models::Order& parentOrder, ExecutionService& execution_service) {
    // Lock to prevent concurrent modification
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Extract TWAP parameters from the parent order
    TwapConfig config;
    config.total_quantity = parentOrder.getQuantity();
    config.price_limit = parentOrder.getPrice();
    
    // Extract duration in seconds (default: 1 hour)
    std::string durationStr = parentOrder.getExtraParam("twap_duration_seconds");
    if (!durationStr.empty()) {
        try {
            config.duration = std::chrono::seconds(std::stoi(durationStr));
        } catch (const std::exception&) {
            config.duration = std::chrono::seconds(3600); // Default to 1 hour
        }
    } else {
        config.duration = std::chrono::seconds(3600); // Default to 1 hour
    }
    
    // Extract slice interval in seconds (default: 5 minutes)
    std::string intervalStr = parentOrder.getExtraParam("twap_slice_interval");
    if (!intervalStr.empty()) {
        try {
            config.slice_interval = std::chrono::seconds(std::stoi(intervalStr));
        } catch (const std::exception&) {
            config.slice_interval = std::chrono::seconds(300); // Default to 5 minutes
        }
    } else {
        config.slice_interval = std::chrono::seconds(300); // Default to 5 minutes
    }
    
    // Extract price deviation percentage (default: 0.0)
    std::string deviationStr = parentOrder.getExtraParam("deviation_pct");
    if (!deviationStr.empty()) {
        try {
            config.deviation_pct = std::stod(deviationStr);
        } catch (const std::exception&) {
            config.deviation_pct = 0.0;
        }
    } else {
        config.deviation_pct = 0.0;
    }
    
    // Extract allow partial flag (default: true)
    std::string allowPartialStr = parentOrder.getExtraParam("allow_partial");
    if (!allowPartialStr.empty()) {
        config.allow_partial = (allowPartialStr == "true" || allowPartialStr == "1");
    } else {
        config.allow_partial = true;
    }
    
    // Initialize the algorithm
    if (!initialize(parentOrder.getTradingPair(), parentOrder.getSide(), config)) {
        return ExecutionResponse{
            parentOrder.getClientOrderId(), 
            "", 
            false,
            "Failed to initialize TWAP algorithm: " + error_message_,
            std::chrono::system_clock::now(),
            "REJECTED", 
        };
    }
    
    // Keep track of the client order ID to use in our callbacks
    std::string clientOrderId = parentOrder.getClientOrderId();
    
    // Set callbacks for order creation and algorithm completion
    // Using value capture for the execution service to avoid dangling reference
    // Store only the client order ID, not the entire order
    setOrderCallback([&execution_service, clientOrderId](const models::OrderPtr& sliceOrder) {
        // Set parent order ID as reference in slice order
        sliceOrder->setExtraParam("parent_order_id", clientOrderId);
        
        // Execute the slice order
        execution_service.placeOrder(*sliceOrder);
    });
    
    // Set completion callback to log the final status
    setCompletionCallback([clientOrderId](bool success, const std::string& message) {
        // Log completion status
        std::cout << "TWAP execution for order " << clientOrderId 
                  << (success ? " completed successfully" : " failed")
                  << ": " << message << std::endl;
    });
    
    // Start the algorithm
    if (!start()) {
        return ExecutionResponse{
            parentOrder.getClientOrderId(), 
            "", 
            false,
            "Failed to start TWAP algorithm",
            std::chrono::system_clock::now(),
            "REJECTED"
        };
    }
    
    // Return initial response with ACCEPTED status
    return ExecutionResponse{
        parentOrder.getClientOrderId(),
        "",  // No exchange order ID for the parent order
        true,
        "",
        std::chrono::system_clock::now(),
        "ACCEPTED",  // Parent order is accepted for algorithmic execution

    };
}

void TwapAlgorithm::setErrorState(const std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::ERROR;
    error_message_ = error_message;
    
    // Notify completion with error status
    if (completion_callback_) {
        completion_callback_(false, error_message);
    }
}
void TwapAlgorithm::setOrderCallback(const OrderCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    order_callback_ = callback;
}

void TwapAlgorithm::setCompletionCallback(const CompletionCallback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    completion_callback_ = callback;
}

void TwapAlgorithm::handleOrderUpdate(const models::OrderPtr& order) {
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
        
        // Check if we need to create more slices or if we're done
        checkCompletion();
    }
}

bool TwapAlgorithm::isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::RUNNING;
}

bool TwapAlgorithm::isComplete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::COMPLETED;
}

bool TwapAlgorithm::isCancelled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::CANCELLED;
}

double TwapAlgorithm::getTotalQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.total_quantity;
}

double TwapAlgorithm::getExecutedQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return executed_quantity_;
}

double TwapAlgorithm::getRemainingQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.total_quantity - executed_quantity_;
}

double TwapAlgorithm::getAverageExecutionPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return average_execution_price_;
}

std::chrono::seconds TwapAlgorithm::getRemainingTime() const {
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

std::vector<models::OrderPtr> TwapAlgorithm::getChildOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return child_orders_;
}

void TwapAlgorithm::scheduleNextSlice() {
    // This is handled by the Timer class in this implementation
}

void TwapAlgorithm::createAndSendSlice() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != State::RUNNING) {
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    if (now >= end_time_) {
        // We've reached the end time, check completion
        checkCompletion();
        return;
    }
    
    // Calculate remaining time and quantity
    auto remaining_time = std::chrono::duration_cast<std::chrono::seconds>(end_time_ - now);
    double remaining_quantity = config_.total_quantity - executed_quantity_;
    
    if (remaining_quantity <= 0) {
        // All quantity has been executed
        checkCompletion();
        return;
    }
    
    // Calculate number of slices remaining and slice size
    int remaining_slices = static_cast<int>(
        std::ceil(static_cast<double>(remaining_time.count()) / 
                  static_cast<double>(config_.slice_interval.count())));
    
    // Ensure at least one slice
    remaining_slices = std::max(1, remaining_slices);
    
    // Calculate slice size
    double slice_size = remaining_quantity / remaining_slices;
    
    // Create a new order for this slice
    auto order = std::make_shared<models::Order>();
    order->setTradingPair(trading_pair_);
    order->setSide(side_);
    
    if (config_.price_limit > 0) {
        order->setType(models::OrderType::LIMIT);
        order->setPrice(config_.price_limit);
    } else {
        order->setType(models::OrderType::MARKET);
    }
    
    order->setQuantity(slice_size);
    
    // Add to our child orders
    child_orders_.push_back(order);
    
    // Notify callback
    if (order_callback_) {
        order_callback_(order);
    }
}

void TwapAlgorithm::updateExecutionStatistics() {
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
void TwapAlgorithm::checkCompletion() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == State::RUNNING) {
        auto now = std::chrono::system_clock::now();
        bool time_complete = now >= end_time_;
        double remaining_quantity = config_.total_quantity - executed_quantity_;
        bool quantity_complete = remaining_quantity <= 0.000001; // Small epsilon for floating point comparisons
        
        // Check if all orders are in terminal states
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
        
        // Complete if we've executed all quantity and all orders are done
        if (quantity_complete && all_orders_complete) {
            state_ = State::COMPLETED;
            timer_->stop();
            
            // Notify completion with success status
            if (completion_callback_) {
                completion_callback_(true, "Algorithm completed successfully");
            }
        } 
        // Complete if we've reached the end time and all orders are done
        else if (time_complete && all_orders_complete) {
            state_ = State::COMPLETED;
            timer_->stop();
            
            // Notify completion with timeout status
            if (completion_callback_) {
                completion_callback_(
                    quantity_complete, // Success if all quantity was executed
                    quantity_complete ? "Algorithm completed successfully" : "Algorithm timed out"
                );
            }
        }
    }
}
// void TwapAlgorithm::checkCompletion() {
//     auto now = std::chrono::system_clock::now();
//     bool time_complete = now >= end_time_;
//     double remaining_quantity = config_.total_quantity - executed_quantity_;
//     bool quantity_complete = remaining_quantity <= 0.000001; // Small epsilon to handle floating point errors
    
//     // Check if all orders are completed
//     bool all_orders_complete = true;
//     for (const auto& order : child_orders_) {
//         auto state = order->getState();
//         if (state != models::OrderState::FILLED &&
//             state != models::OrderState::CANCELED &&
//             state != models::OrderState::REJECTED) {
//             all_orders_complete = false;
//             break;
//         }
//     }
    
//     // Algorithm is complete if we've reached the end time or executed all quantity
//     // and all orders are in a terminal state
//     if ((time_complete || quantity_complete) && all_orders_complete) {
//         if (state_ != State::COMPLETED && state_ != State::CANCELLED) {
//             state_ = State::COMPLETED;
            
//             if (timer_) {
//                 timer_->stop();
//             }
            
//             // Notify completion
//             if (completion_callback_) {
//                 completion_callback_(true, "Algorithm completed successfully");
//             }
//         }
//     }
// }

// void TwapAlgorithm::setErrorState(const std::string& error_message) {
//     state_ = State::ERROR;
//     error_message_ = error_message;
    
//     // Notify completion with error
//     if (completion_callback_) {
//         completion_callback_(false, error_message);
//     }
// }

} // namespace algorithms
} // namespace execution
} // namespace trading