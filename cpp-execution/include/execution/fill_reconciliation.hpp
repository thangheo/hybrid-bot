/**
 * @file fill_reconciliation.hpp
 * @brief models::Fill reconciliation between expected and actual executions
 * 
 * @details 
 * Reconciles expected fills with actual fills reported by exchanges,
 * detects discrepancies, and triggers correction processes.
 * 
 * Created on: May 5, 2025
 */
#pragma once

#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"
#include <core/market_types.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>

namespace trading {
namespace execution {

// Forward declarations
class FillReconciliation;

/**
 * @brief Represents a fill discrepancy
 */
struct FillDiscrepancy {
    std::string order_id;
    core::ExchangeId exchangeId;
    std::string tradingPair;
    double expected_quantity;
    double actual_quantity;
    double expected_price;
    double actual_price;
    std::string discrepancy_type;  // "PRICE", "QUANTITY", "MISSING", "EXTRA"
    std::chrono::system_clock::time_point detectionTime;
    
    std::string toString() const;
};

/**
 * @brief Callback for fill discrepancies
 */
using DiscrepancyCallback = std::function<void(const FillDiscrepancy&)>;

/**
 * @brief Reconciles expected fills with actual fills
 */
class FillReconciliation {
public:
    /**
     * @brief Constructor
     */
    FillReconciliation();
    
    /**
     * @brief Destructor
     */
    ~FillReconciliation() = default;
    bool initialize();
    
    void start() ;
    
    void stop() ;
    /**
     * @brief Record an expected fill
     * 
     * @param order_id The order ID
     * @param expected_fill The expected fill
     */
    void recordExpectedFill(const std::string& order_id, const models::Fill& expected_fill);
    
    /**
     * @brief Record an actual fill from the exchange
     * 
     * @param order_id The order ID
     * @param actual_fill The actual fill
     * @return True if reconciliation succeeded, false if a discrepancy was detected
     */
    bool recordActualFill(const std::string& order_id, const models::Fill& actual_fill);
    
    /**
     * @brief Check for missing fills (expected but not received)
     * 
     * @param timeout_ms Time after which a fill is considered missing
     * @return Vector of discrepancies
     */
    std::vector<FillDiscrepancy> checkForMissingFills(int64_t timeout_ms);
    
    /**
     * @brief Check for extra fills (received but not expected)
     * 
     * @return Vector of discrepancies
     */
    std::vector<FillDiscrepancy> checkForExtraFills();
    // getHealthStatus
    /**
     * @brief Get the health status of the fill reconciliation system
     * 
     * @return Health status as a string
     */
    std::string getHealthStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return "FillReconciliation: " + std::to_string(expected_fills_.size()) + " expected fills, " +
               std::to_string(actual_fills_.size()) + " actual fills.";
    }
    /**
     * @brief Register a callback for discrepancies
     * 
     * @param callback The callback function
     */
    void registerDiscrepancyCallback(DiscrepancyCallback callback);
    
    /**
     * @brief Get all reconciled fills for an order
     * 
     * @param order_id The order ID
     * @return Vector of reconciled fills
     */
    std::vector<models::Fill> getReconciledFills(const std::string& order_id) const;
    
    /**
     * @brief Get all unreconciled expected fills for an order
     * 
     * @param order_id The order ID
     * @return Vector of unreconciled expected fills
     */
    std::vector<models::Fill> getUnreconciledExpectedFills(const std::string& order_id) const;
    
    /**
     * @brief Get all unreconciled actual fills for an order
     * 
     * @param order_id The order ID
     * @return Vector of unreconciled actual fills
     */
    std::vector<models::Fill> getUnreconciledActualFills(const std::string& order_id) const;
    
    /**
     * @brief Purge old reconciliation data
     * 
     * @param age_threshold_ms Age threshold in milliseconds
     * @return Number of records purged
     */
    int purgeOldReconciliationData(int64_t age_threshold_ms);
    
private:
    // Data structures for tracking fills
    struct FillRecord {
        models::Fill fill;
        bool reconciled;
        std::chrono::system_clock::time_point timestamp;
    };
    
    std::unordered_map<std::string, std::vector<FillRecord>> expected_fills_;
    std::unordered_map<std::string, std::vector<FillRecord>> actual_fills_;
    std::unordered_map<std::string, std::vector<models::Fill>> reconciled_fills_;
    
    // Callbacks
    std::vector<DiscrepancyCallback> discrepancy_callbacks_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Helper methods
    void notifyDiscrepancy(const FillDiscrepancy& discrepancy);
    bool matchFills(const models::Fill& expected, const models::Fill& actual, double price_tolerance, double quantity_tolerance);
};

} // namespace execution
} // namespace trading