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
// Forward declare ExecutionInterface to avoid pulling in the full header here
namespace trading { namespace risk { class ExecutionInterface; } }

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
    // new constructor with market data interface
    explicit PortfolioManager(const RiskConfig& config,
                     std::shared_ptr<DataInterface> dataInterface);
    // new constructor allowing injection of a shared PositionTracker to avoid duplicate instances
    PortfolioManager(const RiskConfig& config,
                     std::shared_ptr<DataInterface> dataInterface,
                     std::shared_ptr<PositionTracker> positionTracker);
    /**
     * Initialize the portfolio manager and its components
     * @return true if initialization was successful, false otherwise
     */
    virtual bool initialize();
    virtual bool shutdown();
    // isHealthy
    virtual bool isHealthy() const;
    /**
     * Process an execution report
     * @param executionReport The execution report to process (from execution component)
     */
    virtual void processExecutionReport(const trading::execution::models::ExecutionReport& executionReport);
    // reconcilePositions
    virtual bool reconcilePositions(ExchangeId exchangeId);
    /**
     * Update market data
     * @param marketData New market data
     */
    virtual void updateMarketData(const MarketData& marketData);
    
    /**
     * Get the current portfolio value
     * @return The portfolio value
     */
    virtual double getPortfolioValue() const;
    
    /**
     * Get the position tracker
     * @return Reference to the position tracker
     */
    virtual std::shared_ptr<PositionTracker> getPositionTracker() const;
    
    /**
     * Get the rebalancing engine
     * @return Reference to the rebalancing engine
     */
    virtual std::shared_ptr<RebalancingEngine> getRebalancingEngine() const;
    
    /**
     * Get the collateral manager
     * @return Reference to the collateral manager
     */
    virtual std::shared_ptr<CollateralManager> getCollateralManager() const;
    
    /**
     * Trigger portfolio rebalancing
     * @return true if rebalancing was initiated successfully, false otherwise
     */
    virtual bool triggerRebalancing();
    
    /**
     * Get the portfolio allocation
     * @return Map of asset identifier to allocation percentage
     */
    virtual std::unordered_map<std::string, double> getPortfolioAllocation() const;
    
    /**
     * Get positions by exchange
     * @param exchangeId Optional exchange ID to filter positions
     * @return Vector of positions
     */
    virtual std::vector<Position> getAllPositions(const std::optional<ExchangeId>& exchangeId = std::nullopt) const;
    
    /**
     * Get position by trading pair
     * @param tradingPair The trading pair
     * @param exchangeId Optional exchange ID to filter positions
     * @return Optional position, or nullopt if not found
     */
    virtual std::optional<Position> getPosition(const TradingPair& tradingPair, 
                                       const std::optional<ExchangeId>& exchangeId = std::nullopt) const;
    // getAllPositions
    // std::vector<Position> getAllPositions() const;
    /**
     * Update portfolio configuration
     * @param config New configuration
     * @return true if update was successful, false otherwise
     */
    virtual bool updateConfiguration(const RiskConfig& config);
    
    /**
     * Register a callback for portfolio events
     * @param callback The function to call when portfolio events occur
     */
    virtual void registerPortfolioCallback(const std::function<void(const std::string&, const std::string&)>& callback);
    // processExecution
    virtual void processExecution(const trading::execution::models::ExecutionReport& executionReport);

    // Virtual destructor to ensure derived mocks are destroyed correctly
    virtual ~PortfolioManager() = default;
private:
    // Sub-components
    std::shared_ptr<PositionTracker> m_positionTracker;
    std::shared_ptr<RebalancingEngine> m_rebalancingEngine;
    std::shared_ptr<CollateralManager> m_collateralManager;
    std::shared_ptr<DataInterface> m_dataInterface;
    // Optional injected ExecutionInterface - if present, use it instead of creating a new one
    std::shared_ptr<ExecutionInterface> m_executionInterface;
    
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
    // Allow RiskManager or tests to inject a shared ExecutionInterface to avoid duplicate ApiClientManagers
public:
    void setExecutionInterface(const std::shared_ptr<ExecutionInterface>& executionInterface) {
        m_executionInterface = executionInterface;
    }
};

} // namespace risk
} // namespace trading