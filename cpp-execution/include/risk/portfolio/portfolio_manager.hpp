/**
 * Interface for the unified portfolio management system
 *
 * File: portfolio_manager.hpp
 * Created: 2025-04-29
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "position_tracker.hpp"
#include "rebalancing_engine.hpp"
#include "collateral_manager.hpp"
#include "../../execution/models/execution_report.hpp"

#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <functional>
#include <unordered_map>
#include <optional>

namespace trading {
namespace risk {

// Forward declarations for types that might not be fully defined yet
// struct MarketData;
struct RebalancingResult;

/**
 * PortfolioManager orchestrates portfolio-level operations.
 * It provides a unified portfolio view and manages portfolio-level risk constraints.
 */
class PortfolioManager {
public:
    /**
     * Constructor
     * @param config Risk configuration containing portfolio management settings
     */
    explicit PortfolioManager(const RiskConfig& config);
    
    /**
     * Initialize the portfolio manager and its components
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    // isHealthy
    bool isHealthy() const;
    /**
     * Process an execution report
     * @param executionReport The execution report to process (from execution component)
     */
    void processExecutionReport(const trading::execution::models::ExecutionReport& executionReport);
    // reconcilePositions
    bool reconcilePositions();
    /**
     * Update market data
     * @param marketData New market data
     */
    void updateMarketData(const MarketData& marketData);
    
    /**
     * Get the current portfolio value
     * @return The portfolio value
     */
    double getPortfolioValue() const;
    
    /**
     * Get the position tracker
     * @return Reference to the position tracker
     */
    std::shared_ptr<PositionTracker> getPositionTracker() const;
    
    /**
     * Get the rebalancing engine
     * @return Reference to the rebalancing engine
     */
    std::shared_ptr<RebalancingEngine> getRebalancingEngine() const;
    
    /**
     * Get the collateral manager
     * @return Reference to the collateral manager
     */
    std::shared_ptr<CollateralManager> getCollateralManager() const;
    
    /**
     * Trigger portfolio rebalancing
     * @return true if rebalancing was initiated successfully, false otherwise
     */
    bool triggerRebalancing();
    
    /**
     * Get the portfolio allocation
     * @return Map of asset identifier to allocation percentage
     */
    std::unordered_map<std::string, double> getPortfolioAllocation() const;
    
    /**
     * Get positions by exchange
     * @param exchangeId Optional exchange ID to filter positions
     * @return Vector of positions
     */
    std::vector<Position> getPositions(const std::optional<ExchangeId>& exchangeId = std::nullopt) const;
    
    /**
     * Get position by trading pair
     * @param tradingPair The trading pair
     * @param exchangeId Optional exchange ID to filter positions
     * @return Optional position, or nullopt if not found
     */
    std::optional<Position> getPosition(const TradingPair& tradingPair, 
                                       const std::optional<ExchangeId>& exchangeId = std::nullopt) const;
    
    /**
     * Update portfolio configuration
     * @param config New configuration
     * @return true if update was successful, false otherwise
     */
    bool updateConfiguration(const RiskConfig& config);
    
    /**
     * Register a callback for portfolio events
     * @param callback The function to call when portfolio events occur
     */
    void registerPortfolioCallback(const std::function<void(const std::string&, const std::string&)>& callback);
    // processExecution
    void processExecution(const trading::execution::models::ExecutionReport& executionReport);
private:
    // Sub-components
    std::shared_ptr<PositionTracker> m_positionTracker;
    std::shared_ptr<RebalancingEngine> m_rebalancingEngine;
    std::shared_ptr<CollateralManager> m_collateralManager;
    
    // Configuration
    RiskConfig m_config;
    
    // State
    mutable std::mutex m_stateMutex;
    double m_portfolioValue;
    std::unordered_map<std::string, double> m_portfolioAllocation;
    
    // Callbacks
    mutable std::mutex m_callbackMutex;
    std::vector<std::function<void(const std::string&, const std::string&)>> m_portfolioCallbacks;
    
    // Helper methods
    void recalculatePortfolioMetrics();
    void notifyPortfolioEvent(const std::string& eventType, const std::string& description);
};

} // namespace risk
} // namespace trading