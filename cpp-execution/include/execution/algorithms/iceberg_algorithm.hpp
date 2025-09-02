#pragma once

#include "../models/order.hpp"
#include "execution/execution_forward_decl.hpp"
#include "execution/execution_types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace trading {
namespace execution {
namespace algorithms {

/**
 * Configuration parameters for Iceberg algorithm
 */
struct IcebergConfig {
    double total_quantity;           // Total quantity to execute
    double visible_quantity;         // Quantity visible in each slice
    double price;                    // Limit price for orders
    double price_delta = 0.0;        // Optional price adjustment between slices
    bool adjust_price_on_fill = false; // Whether to adjust price after fills
    bool dynamic_visible_qty = false; // Whether to randomize visible quantity
    double rand_factor = 0.2;        // Randomization factor for visible quantity
};

/**
 * Iceberg execution algorithm
 * Executes a large order by exposing only a small portion to the market at a time
 */
class IcebergAlgorithm {
public:
    using OrderCallback = std::function<void(const models::OrderPtr&)>;
    using CompletionCallback = std::function<void(bool, const std::string&)>;
    
    // Constructor
    IcebergAlgorithm();
    
    // Initialize the algorithm with configuration
    bool initialize(
        const std::string& tradingPair,
        models::OrderSide side,
        const IcebergConfig& config);
    
    // Start algorithm execution
    bool start();
    
    // Pause execution
    bool pause();
    
    // Resume execution
    bool resume();
    
    // Cancel execution
    bool cancel();
    //execute a parent order using Iceberg algorithm
    ExecutionResponse execute(
        const models::Order& parentOrder,
        ExecutionService& execution_service);
    // Register callbacks
    void setOrderCallback(const OrderCallback& callback);
    void setCompletionCallback(const CompletionCallback& callback);
    void setVolumeCallback(
        const std::function<double(const std::string&, std::chrono::minutes)>& callback);
    // Handle order updates
    void handleOrderUpdate(const models::OrderPtr& order);
    
    // Get algorithm status
    bool isRunning() const;
    bool isComplete() const;
    bool isCancelled() const;
    
    // Get execution statistics
    double getTotalQuantity() const;
    double getExecutedQuantity() const;
    double getRemainingQuantity() const;
    double getAverageExecutionPrice() const;
    double getCurrentPrice() const;
    
    // Get current active order
    models::OrderPtr getCurrentOrder() const;
    
    // Get all child orders
    std::vector<models::OrderPtr> getChildOrders() const;
    
private:
    // Algorithm state
    enum class State {
        INITIALIZED,
        RUNNING,
        PAUSED,
        COMPLETED,
        CANCELLED,
        ERROR
    };
    
    // Internal implementation
    std::string trading_pair_;
    models::OrderSide side_;
    IcebergConfig config_;
    State state_ = State::INITIALIZED;
    std::string error_message_;
    
    // Execution tracking
    std::vector<models::OrderPtr> child_orders_;
    models::OrderPtr current_order_;
    double executed_quantity_ = 0.0;
    double remaining_quantity_ = 0.0;
    double average_execution_price_ = 0.0;
    double current_price_ = 0.0;
    int slice_count_ = 0;
    
    // Callbacks
    OrderCallback order_callback_;
    CompletionCallback completion_callback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Internal methods
    void createAndSendNextSlice();
    double calculateVisibleQuantity();
    double calculateNextPrice();
    void updateExecutionStatistics();
    void checkCompletion();
    void setErrorState(const std::string& error_message);
};

} // namespace algorithms
} // namespace execution
}