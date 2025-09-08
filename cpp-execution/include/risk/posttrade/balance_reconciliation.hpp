/**
 * @file balance_reconciliation.hpp
 * @brief Interface for verifying expected vs. actual balances
 * 
 * This component handles the reconciliation of positions between our internal
 * tracking system and the actual positions reported by exchanges.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

#pragma once

#include "../risk_types.hpp"
#include "../risk_events.hpp"
#include "../risk_config.hpp"
#include "../portfolio/position_tracker.hpp"
#include "../interfaces/data_interface.hpp"
#include "../interfaces/execution_interface.hpp"

#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <thread>
#include <string>

namespace trading {
namespace risk {

/**
 * @struct ReconciliationResult
 * @brief Contains the results of a reconciliation operation
 */
struct ReconciliationResult {
    bool success{false};                                    // Overall success status
    int matchedPositions{0};                               // Number of positions that matched
    int mismatchedPositions{0};                           // Number of mismatched positions
    std::vector<PositionReconciliationEvent> discrepancies; // List of discrepancies found
    Timestamp timestamp;                                   // When reconciliation was performed
    
    ReconciliationResult() : timestamp(std::chrono::system_clock::now()) {}
};

/**
 * @struct ReconciliationStatistics
 * @brief Provides statistics about reconciliation operations
 */
struct ReconciliationStatistics {
    int totalReconciliations{0};
    int successfulReconciliations{0};
    int failedReconciliations{0};
    int consecutiveFailures{0};
    Timestamp lastReconciliationTime;
    double averageDiscrepancyValue{0.0};
    int totalDiscrepanciesFound{0};
    
    double getSuccessRate() const {
        return totalReconciliations > 0 ? 
               static_cast<double>(successfulReconciliations) / totalReconciliations : 0.0;
    }
};

/**
 * @class BalanceReconciliation
 * @brief Manages the reconciliation of positions between internal tracking and exchanges
 * 
 * This class provides functionality to:
 * - Compare expected positions with actual exchange positions
 * - Detect and report discrepancies
 * - Automatically correct minor discrepancies when configured
 * - Schedule periodic reconciliations
 * - Generate reconciliation events and statistics
 */
class BalanceReconciliation {
public:
    /**
     * @brief Constructor
     * @param config Risk configuration containing reconciliation settings
     * @param positionTracker Shared pointer to position tracker
     * @param dataInterface Interface to access exchange data
     * @param executionInterface Interface to execution system
     */
    BalanceReconciliation(
        const RiskConfig& config,
        std::shared_ptr<PositionTracker> positionTracker,
        std::shared_ptr<DataInterface> dataInterface,
        std::shared_ptr<ExecutionInterface> executionInterface
    );
    
    /**
     * @brief Destructor - ensures cleanup of background threads
     */
    ~BalanceReconciliation();
    
    // Disable copy constructor and assignment operator
    BalanceReconciliation(const BalanceReconciliation&) = delete;
    BalanceReconciliation& operator=(const BalanceReconciliation&) = delete;
    
    /**
     * @brief Initialize the reconciliation system
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Shutdown the reconciliation system
     * @return true if shutdown successful, false otherwise
     */
    bool shutdown();
    
    /**
     * @brief Perform reconciliation for all exchanges and positions
     * @return Complete reconciliation result
     */
    ReconciliationResult reconcileAllPositions();
    
    /**
     * @brief Perform reconciliation for a specific exchange
     * @param exchangeId The exchange to reconcile
     * @return Reconciliation result for the exchange
     */
    ReconciliationResult reconcileExchangePositions(const core::ExchangeId& exchangeId);
    
    /**
     * @brief Perform reconciliation for a specific trading pair on an exchange
     * @param exchangeId The exchange
     * @param tradingPair The trading pair
     * @return Reconciliation result for the specific pair
     */
    ReconciliationResult reconcileTradingPair(
        const core::ExchangeId& exchangeId,
        const std::string& tradingPair
    );
    
    /**
     * @brief Start scheduled reconciliations
     * @return true if scheduling started successfully
     */
    bool startScheduledReconciliation();
    
    /**
     * @brief Stop scheduled reconciliations
     * @return true if scheduling stopped successfully
     */
    bool stopScheduledReconciliation();
    
    /**
     * @brief Check if scheduled reconciliation is active
     * @return true if scheduled reconciliation is running
     */
    bool isScheduledReconciliationActive() const;
    
    /**
     * @brief Update reconciliation configuration
     * @param newConfig New reconciliation configuration
     * @return true if update successful
     */
    bool updateConfiguration(const BalanceReconciliationConfig& newConfig);
    
    /**
     * @brief Get current reconciliation configuration
     * @return Current configuration
     */
    BalanceReconciliationConfig getConfiguration() const;
    
    /**
     * @brief Register callback for reconciliation events
     * @param callback Function to call when reconciliation events occur
     */
    void registerReconciliationCallback(
        const std::function<void(const PositionReconciliationEvent&)>& callback
    );
    
    /**
     * @brief Get the most recent reconciliation result
     * @return Last reconciliation result
     */
    ReconciliationResult getLastReconciliationResult() const;
    
    /**
     * @brief Get reconciliation statistics
     * @return Current reconciliation statistics
     */
    ReconciliationStatistics getStatistics() const;
    
    /**
     * @brief Manually correct a position discrepancy
     * @param exchangeId The exchange
     * @param tradingPair The trading pair
     * @param correctQuantity The correct quantity to set
     * @return true if correction applied successfully
     */
    bool correctDiscrepancy(
        const core::ExchangeId& exchangeId,
        const TradingPair& tradingPair,
        double correctQuantity
    );
    
    /**
     * @brief Force reconciliation of all positions (ignores scheduling)
     * @return Reconciliation result
     */
    ReconciliationResult forceFullReconciliation();

private:
    // Dependencies
    std::shared_ptr<PositionTracker> m_positionTracker;
    std::shared_ptr<DataInterface> m_dataInterface;
    std::shared_ptr<ExecutionInterface> m_executionInterface;
    
    // Configuration
    BalanceReconciliationConfig m_config;
    
    // State management
    mutable std::mutex m_stateMutex;
    ReconciliationResult m_lastReconciliation;
    ReconciliationStatistics m_statistics;
    
    // Scheduling
    std::atomic<bool> m_isScheduledActive{false};
    std::atomic<bool> m_shouldStop{false};
    std::unique_ptr<std::thread> m_scheduledThread;
    std::chrono::system_clock::time_point m_nextScheduledTime;
    
    // Callbacks
    mutable std::mutex m_callbackMutex;
    std::vector<std::function<void(const PositionReconciliationEvent&)>> m_callbacks;
    
    // Helper methods
    
    /**
     * @brief Create a reconciliation event from discrepancy data
     */
    PositionReconciliationEvent createReconciliationEvent(
        const core::ExchangeId& exchangeId,
        const std::string& tradingPair,
        double expectedQuantity,
        double actualQuantity,
        double discrepancy,
        bool autoCorrect
    );
    
    /**
     * @brief Fetch actual positions from exchange
     */
    bool fetchActualPositions(
        const core::ExchangeId& exchangeId,
        std::map<std::string, double>& actualPositions
    );
    
    /**
     * @brief Check if discrepancy should be auto-corrected
     */
    bool shouldAutoCorrect(double discrepancyPercent) const;
    
    /**
     * @brief Apply position correction
     */
    bool applyCorrection(
        const core::ExchangeId& exchangeId,
        const TradingPair& tradingPair,
        double actualQuantity
    );
    
    /**
     * @brief Notify all registered callbacks of reconciliation event
     */
    void notifyCallbacks(const PositionReconciliationEvent& event);
    
    /**
     * @brief Calculate discrepancy percentage
     */
    double calculateDiscrepancyPercent(double expected, double actual) const;
    
    /**
     * @brief Check if discrepancy exceeds tolerance
     */
    bool exceedsTolerance(double discrepancy, double expected) const;
    
    /**
     * @brief Update reconciliation statistics
     */
    void updateStatistics(const ReconciliationResult& result);
    
    /**
     * @brief Background thread function for scheduled reconciliation
     */
    void scheduledReconciliationLoop();
    
    /**
     * @brief Calculate next scheduled reconciliation time
     */
    std::chrono::system_clock::time_point calculateNextReconciliationTime() const;
};

} // namespace risk
} // namespace trading