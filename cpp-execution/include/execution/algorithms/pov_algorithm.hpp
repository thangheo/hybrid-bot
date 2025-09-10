#pragma once

#include "execution/models/order.hpp"
#include "execution/algorithms/pov_config.hpp"
#include "execution/execution_forward_decl.hpp"
#include "execution/execution_types.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <core/market_types.hpp>

namespace trading {
namespace execution {
namespace algorithms {

/**
 * Percentage of Volume (POV) execution algorithm
 * Executes orders as a percentage of market volume
 */
class PovAlgorithm {
public:
    using OrderPtr = std::shared_ptr<models::Order>;
    using OrderCallback = std::function<void(const OrderPtr&)>;
    using CompletionCallback = std::function<void(bool, const std::string&)>;
    using MarketVolumeCallback = std::function<double(const std::string&, std::chrono::minutes)>;
    
    // Constructor
    PovAlgorithm();
    ~PovAlgorithm();
    
    // Initialize the algorithm with configuration
    bool initialize(
        const core::Symbol& tradingPair,
        models::OrderSide side,
        const PovConfig& config);
    
    // Start algorithm execution
    bool start();
    
    // Pause execution
    bool pause();
    
    // Resume execution
    bool resume();
    
    // Cancel execution
    bool cancel();
    
    /**
     * @brief Execute a parent order using POV algorithm
     * 
     * @param parentOrder The parent order with POV parameters
     * @param execution_service Reference to the execution service for placing child orders
     * @return ExecutionResponse with initial execution status
     */
    ExecutionResponse execute(const models::Order& parentOrder, ExecutionService& execution_service);
    
    // Register callbacks
    void setOrderCallback(const OrderCallback& callback);
    void setCompletionCallback(const CompletionCallback& callback);
    void setMarketVolumeCallback(const MarketVolumeCallback& callback);
    
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
    
private:
    // Extract POV configuration from order parameters
    PovConfig extractConfigFromOrder(const models::Order& parentOrder);

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
    PovConfig config_;
    State state_ = State::INITIALIZED;
    std::string error_message_;
    
    // Execution tracking
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    std::chrono::system_clock::time_point last_check_time_;
    std::vector<models::OrderPtr> child_orders_;
    double executed_quantity_ = 0.0;
    double average_execution_price_ = 0.0;
    
    // Market data
    double last_market_volume_ = 0.0;
    
    // Execution control
    std::thread execution_thread_;
    std::atomic<bool> should_stop_{false};
    std::condition_variable pause_cv_;
    std::atomic<bool> is_paused_{false};
    
    // Callbacks
    OrderCallback order_callback_;
    CompletionCallback completion_callback_;
    MarketVolumeCallback market_volume_callback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Internal methods
    void executionLoop();
    double getMarketVolume();
    void placeChildOrder(double quantity);
    void updateExecutionStatistics();
    void checkCompletion();
    void setErrorState(const std::string& error_message);
};

} // namespace algorithms
} // namespace execution
} // namespace trading
