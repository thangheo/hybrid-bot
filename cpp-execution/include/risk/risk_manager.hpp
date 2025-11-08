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
#include <functional>


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
     * Static factory method - USE THIS instead of constructor
     * @param execService The execution service
     * @param config The risk system configuration
     * @return Fully initialized RiskManager shared_ptr
     */
    static std::shared_ptr<RiskManager> create(
        std::shared_ptr<trading::execution::ExecutionService> execService, 
        const RiskConfig& config, 
        std::shared_ptr<trading::risk::MarketDataInterface> marketDataInterface,
        std::shared_ptr<zmq::context_t> zmqContext = nullptr
    ) {
        // Create using make_shared - this enables shared_from_this()
        auto manager = std::shared_ptr<RiskManager>(new RiskManager(execService, config, marketDataInterface, zmqContext));

        // Now safe to call methods that use shared_from_this()
        if (!manager->initialize()) {
            LOG_ERROR("RiskManager", "Failed to initialize RiskManager");
            return nullptr;
        }
        
        return manager;
    }
    /**
     * Constructor
     * @param config The risk system configuration
     */
    // explicit RiskManager(const RiskConfig& config);
    // RiskManager(std::shared_ptr<trading::execution::ExecutionService> execService);
    // explicit RiskManager(std::shared_ptr<trading::execution::ExecutionService> execService, const RiskConfig& config);
    /**
     * Destructor
     */
    ~RiskManager();
// new constructor
    RiskManager(std::shared_ptr<trading::execution::ExecutionService> execService, 
                const RiskConfig& config,
                std::shared_ptr<trading::risk::MarketDataInterface> marketDataInterface,
                std::shared_ptr<zmq::context_t> zmqContext = nullptr);   

    // Backwards-compatible constructor used by some unit tests which only pass a RiskConfig
    explicit RiskManager(const RiskConfig& config)
        : RiskManager(nullptr, config) {}

    // Testing hook: inject mocks or test doubles for internal subsystems.
    // RiskManager will take ownership of raw pointers passed here (use with care in tests).
    void injectTestMocks(
        PortfolioManager* portfolioManager,
        PreTradeRiskFilter* preTradeFilter,
        PostTradeMonitor* postTradeMonitor,
        StrategyInterface* strategyInterface,
        ExecutionInterface* executionInterface,
        DataInterface* dataInterface
    );

    /**
     * Initialize the risk manager and its subsystems
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    /**
     * Shutdown the risk manager and its subsystems
     */
    bool shutdown();
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
     * Register a signal checker callback for the post-trade monitor.
     * The checker should return true if there is a BUY signal for the given exchange and symbol.
     */
    using PostTradeSignalChecker = std::function<bool(const core::ExchangeId&, const core::Symbol&)>;
    void registerPostTradeSignalChecker(const PostTradeSignalChecker& checker);
    
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
    // getAllowedPositionSize
    double getAllowedPositionSize(const TradingPair& tradingPair) const;
    
    /**
     * Get the ExecutionInterface for order submission
     * @return shared_ptr to ExecutionInterface, may be nullptr if not initialized
     */
    std::shared_ptr<ExecutionInterface> getExecutionInterface() const;
    
    /**
     * Get cached position for fast O(1) lookup (avoids expensive API calls)
     * @param exchangeId The exchange identifier
     * @param tradingPair The trading pair
     * @return Optional position quantity, nullopt if not found
     */
    std::optional<double> getCachedPosition(const ExchangeId& exchangeId, const TradingPair& tradingPair) const;
private:
    std::shared_ptr<trading::execution::ExecutionService> m_execService;
    std::shared_ptr<trading::execution::ExecutionService> m_executionService;
        // marketDataInterface
    std::shared_ptr<trading::risk::MarketDataInterface> m_marketDataInterface;
    // std::shared_ptr<trading::execution::ExecutionService> m_executionService;
    /**
     * Private constructor - forces use of create() method
     * @param execService The execution service  
     * @param config The risk system configuration
     */
    explicit RiskManager(std::shared_ptr<trading::execution::ExecutionService> execService, 
                        const RiskConfig& config)
        : m_execService(execService)  // Store for later use
        , m_config(config)
        , m_mode(RiskManagerMode::NORMAL)
    {
        // DON'T call any methods that use shared_from_this() here!
        LOG_INFO("RiskManager", "RiskManager constructor completed");
    }
    // zmq context
    std::shared_ptr<zmq::context_t> zmqContext_;

    // just declare fucking idiots
    std::unique_ptr<CollateralManager> m_collateralManager;
    // Core subsystems
    std::unique_ptr<PreTradeRiskFilter> m_preTradeFilter;
    std::unique_ptr<PostTradeMonitor> m_postTradeMonitor;
    std::unique_ptr<PortfolioManager> m_portfolioManager;
    std::unique_ptr<RiskService> m_riskService;
    
    // Interfaces to external systems
    std::unique_ptr<StrategyInterface> m_strategyInterface;
    std::shared_ptr<DataInterface> m_dataInterface;
    // m_executionInterface
    std::shared_ptr<ExecutionInterface> m_executionInterface;
    
    // Configuration and state
    RiskConfig m_config;
    std::atomic<RiskManagerMode> m_mode;
    mutable std::mutex m_stateMutex;
    
    // Risk event callbacks
    std::vector<RiskEventCallback> m_eventCallbacks;
    mutable std::mutex m_callbackMutex;
    // Optional signal checker to be forwarded to PostTradeMonitor during init
    std::optional<PostTradeSignalChecker> postTradeSignalChecker_;
    
    // Private helper methods
    void notifyRiskEvent(const RiskEvent& event);
    // void loadConfiguration();
};

} // namespace risk
}
