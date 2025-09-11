/**
 * Main risk management orchestrator that coordinates all risk subsystems
 *
 * File: risk_manager.hpp
 * Created: 2025-04-29
 */

#pragma once

#include "strategy/types/performance_metrics.hpp"
#include "risk_types.hpp"
#include "risk_events.hpp"
#include "risk_config.hpp"
#include "pretrade/pre_trade_risk_filter.hpp"
#include "posttrade/post_trade_monitor.hpp"
#include "portfolio/portfolio_manager.hpp"
#include "service/risk_service.hpp"
#include "interfaces/strategy_interface.hpp"
#include "interfaces/execution_interface.hpp"
#include "execution/execution_service.hpp"
#include "execution/models/execution_report.hpp"
#include "interfaces/strategy_interface.hpp"
#include "service/risk_validation_service.hpp"
#include "risk/interfaces/market_data_interface.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>


namespace trading   {
namespace risk {
using namespace trading::execution::models;
using namespace trading::strategy;
/**
 * The Risk Manager is the central coordinating component for all risk subsystems.
 * It orchestrates risk checks, monitors trading activity, and enforces risk limits.
 */

// class RiskManager {
class RiskManager : public std::enable_shared_from_this<RiskManager> {
public:
    bool checkSystemHealth();
    void loadConfiguration();
    /**
     * Constructor
     * @param config The risk system configuration
     */
    explicit RiskManager(const RiskConfig& config);
    RiskManager(std::shared_ptr<trading::execution::ExecutionService> execService);
    /**
     * Destructor
     */
    ~RiskManager();

    /**
     * Initialize the risk manager and its subsystems
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    
    /**
     * Validate a trading signal before it becomes an order
     * @param signal The trading signal to validate
     * @return ValidationResult containing approval status and any violations
     */
    ValidationResult validateSignal(const TradingSignal& signal);
    
    /**
     * Process execution reports for post-trade analysis
     * @param executionReport The execution report to process
     */
    void processExecutionReport(const ExecutionReport& executionReport);
    
    /**
     * Update market data for risk calculations
     * @param marketData The latest market data
     */
    void updateMarketData(const MarketData& marketData);
    
    /**
     * Get the current risk manager mode
     * @return The current RiskManagerMode
     */
    RiskManagerMode getMode() const;
    
    /**
     * Set the risk manager mode
     * @param mode The new RiskManagerMode
     */
    void setMode(RiskManagerMode mode);
    
    /**
     * Get the current risk limits
     * @param exchangeId The exchange ID
     * @param tradingPair The trading pair
     * @param strategyId The strategy ID
     * @return RiskParameters containing current risk limits
     */
    RiskParameters getRiskParameters(const ExchangeId& exchangeId, 
                                    const TradingPair& tradingPair,
                                    const StrategyId& strategyId) const;
    
    /**
     * Update risk parameters
     * @param params The new risk parameters
     * @return true if update was successful, false otherwise
     */
    bool updateRiskParameters(const RiskParameters& params);
    
    /**
     * Register a callback for risk events
     * @param callback The function to call when risk events occur
     */
    void registerRiskEventCallback(const RiskEventCallback& callback);
    
    /**
     * Force a position reconciliation
     * @param exchangeId Optional exchange ID to limit reconciliation
     * @return true if reconciliation was successful, false otherwise
     */
    bool reconcilePositions(const std::optional<ExchangeId>& exchangeId = std::nullopt);
    
    /**
     * Get the latest performance metrics
     * @return PerformanceMetrics containing the latest metrics
     */
    PerformanceMetrics getPerformanceMetrics() const;
    
    /**
     * Perform emergency risk actions (halt trading, cancel orders)
     * @param reason The reason for emergency action
     * @return true if action was successful, false otherwise
     */
    bool emergencyAction(const std::string& reason);
   
private:
// config_
    RiskConfig m_config;
    // just declare fucking idiots
    std::shared_ptr<trading::execution::ExecutionService> m_executionService;
    std::shared_ptr<ExecutionInterface> m_executionInterface;
    std::unique_ptr<CollateralManager> m_collateralManager;
    // Core subsystems
    std::unique_ptr<PreTradeRiskFilter> m_preTradeFilter;
    std::unique_ptr<PostTradeMonitor> m_postTradeMonitor;
    std::unique_ptr<PortfolioManager> m_portfolioManager;
    std::unique_ptr<RiskService> m_riskService;
    
    // Interfaces to external systems
    std::unique_ptr<StrategyInterface> m_strategyInterface;
    std::unique_ptr<ExecutionInterface> m_executionInterface;
    std::unique_ptr<DataInterface> m_dataInterface;
    
    // Configuration and state
    RiskConfig m_config;
    std::atomic<RiskManagerMode> m_mode;
    mutable std::mutex m_stateMutex;
    
    // Risk event callbacks
    std::vector<RiskEventCallback> m_eventCallbacks;
    mutable std::mutex m_callbackMutex;
    
    // Private helper methods
    void notifyRiskEvent(const RiskEvent& event);
    // void loadConfiguration();
};

} // namespace risk
}
