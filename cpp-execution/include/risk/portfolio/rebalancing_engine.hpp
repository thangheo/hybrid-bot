/**
 * Interface for inventory management across venues
 *
 * File: rebalancing_engine.hpp
 * Created: 2025-04-29
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "position_tracker.hpp"
#include "../interfaces/execution_interface.hpp"
#include "../interfaces/data_interface.hpp"
#include <risk/portfolio/position_tracker.hpp>
#include <map>
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace trading {
namespace risk {

/**
 * Structure to represent a rebalancing order
 */
struct RebalancingOrder {
    ExchangeId sourceExchangeId;
    ExchangeId targetExchangeId;
    TradingPair tradingPair;
    double quantity;
    Direction direction;
    double estimatedPrice;
    std::string reason;
};

/**
 * Structure to represent rebalancing result
 */
struct RebalancingResult {
    bool success;
    std::vector<RebalancingOrder> ordersCreated;
    std::vector<std::string> errors;
    Timestamp timestamp;
};

/**
 * RebalancingEngine manages inventory across venues.
 * It identifies and executes rebalancing opportunities to minimize exchange-specific risks.
 */
class RebalancingEngine {
public:
    /**
     * Constructor
     * @param config Risk configuration containing rebalancing settings
     * @param positionTracker Reference to position tracker
     * @param executionInterface Interface to execution system
     * @param dataInterface Interface to market data
     */
    RebalancingEngine(const RiskConfig& config, 
                     std::shared_ptr<PositionTracker> positionTracker,
                     std::shared_ptr<ExecutionInterface> executionInterface,
                     std::shared_ptr<DataInterface> dataInterface);
    
    /**
     * Destructor - properly stops rebalancing thread
     */
    ~RebalancingEngine();

    /**
     * Initialize the rebalancing engine
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    bool shutdown();
    /**
     * Check if rebalancing is needed
     * @return true if rebalancing is needed, false otherwise
     */
    bool isRebalancingNeeded() const;
    
    /**
     * Calculate rebalancing needs
     * @return Vector of rebalancing orders
     */
    std::vector<RebalancingOrder> calculateRebalancingNeeds() const;
    
    /**
     * Execute rebalancing
     * @param orders Optional vector of rebalancing orders (if not provided, will calculate automatically)
     * @return Rebalancing result
     */
    RebalancingResult executeRebalancing(const std::optional<std::vector<RebalancingOrder>>& orders = std::nullopt);
    
    /**
     * Schedule regular rebalancing checks based on configuration
     * @return true if scheduling was successful, false otherwise
     */
    bool scheduleRebalancingChecks();
    
    /**
     * Cancel scheduled rebalancing checks
     * @return true if cancellation was successful, false otherwise
     */
    bool cancelScheduledRebalancingChecks();
    
    /**
     * Update rebalancing configuration
     * @param rebalancingConfig New rebalancing configuration
     * @return true if update was successful, false otherwise
     */
    bool updateConfiguration(const RebalancingConfig& rebalancingConfig);
    
    /**
     * Get the current rebalancing configuration
     * @return The current configuration
     */
    RebalancingConfig getConfiguration() const;
    
    /**
     * Register a callback for rebalancing events
     * @param callback The function to call when rebalancing events occur
     */
    void registerRebalancingCallback(const std::function<void(const RebalancingResult&)>& callback);
    
    /**
     * Get the most recent rebalancing result
     * @return The most recent rebalancing result
     */
    RebalancingResult getLastRebalancingResult() const;
    
    /**
     * Check if automatic rebalancing is enabled
     * @return true if enabled, false otherwise
     */
    bool isAutoRebalancingEnabled() const;
    
    /**
     * Enable or disable automatic rebalancing
     * @param enabled Whether to enable automatic rebalancing
     * @return true if update was successful, false otherwise
     */
    bool setAutoRebalancing(bool enabled);

private:
    /**
     * Stop the rebalancing thread safely
     */
    void stopRebalancing();
    
    // Thread management
    std::thread m_rebalancingThread;
    std::condition_variable m_cv;
    std::atomic<bool> m_stopRequested{false};
    
    // Core dependencies
    std::shared_ptr<PositionTracker> m_positionTracker;
    std::shared_ptr<ExecutionInterface> m_executionInterface;
    std::shared_ptr<DataInterface> m_dataInterface;
    RiskConfig m_config;
    RebalancingConfig m_rebalancingConfig;
    
    // Configuration members
    std::unordered_map<ExchangeId, double> m_exchangeTransferFees;
    double m_minimumRebalancingValue;
    double m_maximumRebalancingValue;
    
    // Rebalancing state
    mutable std::mutex m_stateMutex;
    RebalancingResult m_lastRebalancing;
    std::chrono::system_clock::time_point m_nextScheduledRebalancing;
    std::atomic<bool> m_rebalancingScheduled{false};
    std::atomic<bool> m_autoRebalancingEnabled{false};
    
    // Target allocations
    mutable std::mutex m_allocationMutex;
    std::unordered_map<std::string, double> m_targetAllocations;
    
    // Callbacks
    mutable std::mutex m_callbackMutex;
    std::vector<std::function<void(const RebalancingResult&)>> m_rebalancingCallbacks;
    
    // Helper methods
    double calculateExchangeAllocation(const ExchangeId& exchangeId, const TradingPair& tradingPair) const;
    std::vector<ExchangeId> getPreferredExchanges() const;
    bool executeRebalancingOrder(const RebalancingOrder& order);
    void notifyRebalancingEvent(const RebalancingResult& result);
    double getAssetPrice(const TradingPair& tradingPair) const;
    double calculateTransferFees(const ExchangeId& sourceExchangeId, const ExchangeId& targetExchangeId, 
                               const TradingPair& tradingPair, double quantity) const;
    bool shouldRebalance(double currentAllocation, double targetAllocation) const;
};
    
} // namespace risk
}