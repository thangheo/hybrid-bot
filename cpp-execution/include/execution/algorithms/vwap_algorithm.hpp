#pragma once

#include "../models/order.hpp"
#include "../execution_forward_decl.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <map>
#include <functional>

namespace trading {
namespace execution {
namespace algorithms {

/**
 * Configuration parameters for VWAP algorithm
 */
struct VwapConfig {
    double total_quantity;                     // Total quantity to execute
    std::chrono::seconds duration;             // Total duration for execution
    std::vector<double> volume_profile;        // Historical volume profile (24 hours)
    double price_limit = 0.0;                  // Optional price limit, 0 for market orders
    double max_participation_rate = 0.1;       // Maximum percentage of market volume to take
    bool dynamic_sizing = true;                // Adjust slice sizes based on actual volume
    bool allow_partial = true;                 // Whether to allow partial fills
};

/**
 * Volume-Weighted Average Price (VWAP) execution algorithm
 * Executes an order following the expected volume profile of the market
 */
class VwapAlgorithm {
public:
    using OrderCallback = std::function<void(const models::OrderPtr&)>;
    using CompletionCallback = std::function<void(bool, const std::string&)>;
    using VolumeCallback = std::function<double(const std::string&, std::chrono::minutes)>;
    
    // Constructor
    VwapAlgorithm();
    
    // Initialize the algorithm with configuration
    bool initialize(
        const std::string& tradingPair,
        models::OrderSide side,
        const VwapConfig& config);
    
    // Set volume profile (can be updated during execution)
    void setVolumeProfile(const std::vector<double>& volume_profile);
    
    // Set callback for getting real-time volume data
    void setVolumeCallback(const VolumeCallback& callback);
    
    // Start algorithm execution
    bool start();
    //execute the algorithm with a parent order
    ExecutionResponse execute(const models::Order& parentOrder, ExecutionService& execution_service);
    
    // Pause execution
    bool pause();
    
    // Resume execution
    bool resume();
    
    // Cancel execution
    bool cancel();
    
    // Register callbacks
    void setOrderCallback(const OrderCallback& callback);
    void setCompletionCallback(const CompletionCallback& callback);
    
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
    double getVwapDeviationPercent() const;
    
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
    
    // Time bucket for volume profile
    struct TimeBucket {
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        double target_quantity;
        double executed_quantity = 0.0;
        bool processed = false;
    };
    
    // Internal implementation
    std::string trading_pair_;
    models::OrderSide side_;
    VwapConfig config_;
    State state_ = State::INITIALIZED;
    std::string error_message_;
    
    // Execution tracking
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    std::vector<models::OrderPtr> child_orders_;
    double executed_quantity_ = 0.0;
    double average_execution_price_ = 0.0;
    
    // Volume profile and buckets
    std::vector<double> volume_profile_;
    std::vector<TimeBucket> time_buckets_;
    
    // Timer for bucket processing
    std::unique_ptr<class Timer> timer_;
    
    // Callbacks
    OrderCallback order_callback_;
    CompletionCallback completion_callback_;
    VolumeCallback volume_callback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Internal methods
    void initializeTimeBuckets();
    void scheduleNextBucket();
    void processBucket(size_t bucket_index);
    double calculateOptimalOrderSize(const TimeBucket& bucket);
    void updateExecutionStatistics();
    void checkCompletion();
    void setErrorState(const std::string& error_message);
    
    // Calculate market VWAP
    double calculateMarketVwap() const;
};

} // namespace algorithms
} // namespace execution
} // namespace trading