#pragma once

#include "execution/models/order.hpp"
#include "execution/algorithms/twap_config.hpp" 
#include "execution/algorithms/timer.hpp"
// #include "execution/execution_service.hpp"
#include "execution/execution_forward_decl.hpp"
#include "execution/execution_types.hpp"
#include "execution/execution_algorithm_factory.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>
#include <mutex>

namespace trading {
namespace execution {

// Forward declarations
// struct ExecutionResponse;
// class ExecutionService;

namespace algorithms {


/**
 * Time-Weighted Average Price (TWAP) execution algorithm
 * Splits a large order into smaller equal-sized orders spread over time
 */
class TwapAlgorithm {
public:
        using OrderPtr = std::shared_ptr<models::Order>;
        using OrderCallback = std::function<void(const OrderPtr&)>;
        using CompletionCallback = std::function<void(bool, const std::string&)>;
        
    // Constructor
    TwapAlgorithm();    
    // Initialize the algorithm with configuration
    bool initialize(
        const ,
        models::OrderSide side,
        const algorithms::TwapConfig& config);
    
    // Start algorithm execution
    bool start();
    
    // Pause execution
    bool pause();
    
    // Resume execution
    bool resume();
    
    // Cancel execution
    bool cancel();
    
    /**
     * @brief Execute a parent order using TWAP algorithm
     * 
     * @param parentOrder The parent order with TWAP parameters
     * @param execution_service Reference to the execution service for placing child orders
     * @return ExecutionResponse with initial execution status
     */
    ExecutionResponse execute(const models::Order& parentOrder, ExecutionService& execution_service);
    
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
    std::chrono::seconds getRemainingTime() const;
    
    // Get all child orders
    std::vector<models::OrderPtr> getChildOrders() const;
    // Get volume for a specific trading pair and time duration
    double getVolume(const core::Symbol& tradingPair, std::chrono::minutes duration) const;
private:
    // Extract TWAP configuration from order parameters
    algorithms::TwapConfig extractConfigFromOrder(const models::Order& parentOrder);
    std::function<double(const std::string&, std::chrono::minutes)> volume_callback_;

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
    core::Symbol trading_pair_;
    models::OrderSide side_;
    algorithms::TwapConfig config_;
    State state_ = State::INITIALIZED;
    std::string error_message_;
    
    // Execution tracking
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    std::vector<models::OrderPtr> child_orders_;
    double executed_quantity_ = 0.0;
    double average_execution_price_ = 0.0;
    
    // Timer for slicing
    std::unique_ptr<class Timer> timer_;
    
    // Callbacks
    OrderCallback order_callback_;
    CompletionCallback completion_callback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Internal methods
    void scheduleNextSlice();
    void createAndSendSlice();
    void updateExecutionStatistics();
    void checkCompletion();
    void setErrorState(const std::string& error_message);
};

} // namespace algorithms
} // namespace execution
} // namespace trading