/**
 * Implementation of the portfolio management component
 *
 * File: portfolio_manager.cpp
 * Created: 2025-04-29
 */

#include "portfolio/portfolio_manager.hpp"
#include "interfaces/data_interface.hpp"
#include "interfaces/execution_interface.hpp"
#include "execution/models/execution_report.hpp"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <set>

namespace trading {
namespace risk {

PortfolioManager::PortfolioManager(const RiskConfig& config)
    : m_config(config)
    , m_portfolioValue(0.0) {
}

PortfolioManager::PortfolioManager(const RiskConfig& config,
                                   std::shared_ptr<DataInterface> dataInterface)
    : m_config(config)
    , m_portfolioValue(0.0) {
    // Use the provided data interface in sub-components
    if (!dataInterface) {
        throw std::invalid_argument("DataInterface cannot be null");
    }
    
    m_dataInterface = std::move(dataInterface);
}

// New constructor that accepts an injected PositionTracker
PortfolioManager::PortfolioManager(const RiskConfig& config,
                                   std::shared_ptr<DataInterface> dataInterface,
                                   std::shared_ptr<PositionTracker> positionTracker)
    : m_config(config), m_portfolioValue(0.0), m_dataInterface(std::move(dataInterface)), m_positionTracker(std::move(positionTracker)) {
    if (!m_dataInterface) {
        throw std::invalid_argument("DataInterface cannot be null");
    }
}


//this overlap with risk_manager.cpp
bool PortfolioManager::initialize() {
    try {
        // Create shared interfaces for sub-components using factory function      
        
       
        // Use injected ExecutionInterface if provided (to share ApiClientManager/credentials)
        std::shared_ptr<ExecutionInterface> sharedExecutionInterface;
        if (m_executionInterface) {
            sharedExecutionInterface = m_executionInterface;
        } else {
            // Create execution interface using factory function
            auto executionInterface = createExecutionInterface(m_config);
            if (!executionInterface || !executionInterface->initialize()) {
                std::cerr << "Failed to initialize ExecutionInterface" << std::endl;
                return false;
            }

            // Convert unique_ptr to shared_ptr for sharing among components
            sharedExecutionInterface = std::shared_ptr<ExecutionInterface>(executionInterface.release());
        }
        
        // Initialize position tracker
        m_positionTracker = std::make_shared<PositionTracker>(m_config, m_dataInterface);
        if (!m_positionTracker->initialize()) {
            std::cerr << "Failed to initialize PositionTracker" << std::endl;
            return false;
        }
        
        // Initialize collateral manager
        m_collateralManager = std::make_shared<CollateralManager>(
            m_config, m_positionTracker, sharedExecutionInterface, m_dataInterface);
        if (!m_collateralManager->initialize()) {
            std::cerr << "Failed to initialize CollateralManager" << std::endl;
            return false;
        }
        
        // Initialize rebalancing engine
        m_rebalancingEngine = std::make_shared<RebalancingEngine>(
            m_config, m_positionTracker, sharedExecutionInterface, m_dataInterface);
        if (!m_rebalancingEngine->initialize()) {
            std::cerr << "Failed to initialize RebalancingEngine" << std::endl;
            return false;
        }
        
        // Set up callbacks from position tracker
        m_positionTracker->registerPositionUpdateCallback(
            [this](const Position& position, const std::string& updateType) {
                recalculatePortfolioMetrics();
                notifyPortfolioEvent("position_update", 
                                   "Position updated: " + position.getTradingPair().toString() +
                                   " (" + updateType + ")");
            }
        );
        
        // Calculate initial portfolio metrics
        recalculatePortfolioMetrics();
        
        // Schedule rebalancing checks if enabled
        if (m_config.portfolioConfig.autoRebalancingEnabled) {
            m_rebalancingEngine->scheduleRebalancingChecks();
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error initializing PortfolioManager: " << e.what() << std::endl;
        return false;
    }
}
bool PortfolioManager::shutdown() {
    try {
        // Shutdown rebalancing engine
        if (m_rebalancingEngine) {
            m_rebalancingEngine->shutdown();
            std::cout << "RebalancingEngine shut down successfully" << std::endl;
        }
        
        // Shutdown collateral manager
        if (m_collateralManager) {
            m_collateralManager->shutdown();
            std::cout << "CollateralManager shut down successfully" << std::endl;
        }
        
        // Shutdown position tracker
        if (m_positionTracker) {
            m_positionTracker->shutdown();
            std::cout << "PositionTracker shut down successfully" << std::endl;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error during PortfolioManager shutdown: " << e.what() << std::endl;
        return false;
    }
}

bool PortfolioManager::isHealthy() const{   
    // Check if all sub-components are initialized and healthy
    if (!m_positionTracker || !m_collateralManager || !m_rebalancingEngine) {
        return false;
    }
    // Additional health checks can be added here
    return true;
}

void PortfolioManager::processExecutionReport(const trading::execution::models::ExecutionReport& executionReport) {
    // Directly forward to position tracker - no conversion needed
    m_positionTracker->processExecutionReport(executionReport);
    
    // Portfolio metrics will be updated via callback
}
// reconcilePositions
bool PortfolioManager::reconcilePositions(ExchangeId exchangeId) {
    try {
        // PostTradeMonitor::reconcilePositions - reconcile all exchanges
        bool success = true;
        auto positions = m_positionTracker->getAllPositions();
        
        // Get unique exchange IDs from positions
        std::set<ExchangeId> uniqueExchangeIds;
        
        for (const auto& position : positions) {
            uniqueExchangeIds.insert(position.getExchangeId());
        }
        
        // Reconcile positions for each exchange
        for (const auto& exchange : uniqueExchangeIds) {
            bool exchangeSuccess = m_positionTracker->reconcilePositions(exchange);
            success = success && exchangeSuccess;
        }
        
        if (success) {
            notifyPortfolioEvent("reconciliation", "Position reconciliation completed successfully");
        } else {
            notifyPortfolioEvent("reconciliation_failed", "Position reconciliation encountered issues");
        }
        return success;
    } catch (const std::exception& e) {
        std::cerr << "Error during position reconciliation: " << e.what() << std::endl;
        notifyPortfolioEvent("reconciliation_error", std::string("Exception: ") + e.what());
        return false;
    }
}
void PortfolioManager::updateMarketData(const MarketData& marketData) {
    // Forward to position tracker
    m_positionTracker->updateMarketData(marketData);
    
    // Portfolio metrics will be updated via callback
}

double PortfolioManager::getPortfolioValue() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_portfolioValue;
}

std::shared_ptr<PositionTracker> PortfolioManager::getPositionTracker() const {
    return m_positionTracker;
}

std::shared_ptr<RebalancingEngine> PortfolioManager::getRebalancingEngine() const {
    return m_rebalancingEngine;
}

std::shared_ptr<CollateralManager> PortfolioManager::getCollateralManager() const {
    return m_collateralManager;
}

bool PortfolioManager::triggerRebalancing() {
    try {
        RebalancingResult result = m_rebalancingEngine->executeRebalancing();
        
        if (result.success) {
            notifyPortfolioEvent("rebalancing", "Rebalancing completed successfully with " + 
                               std::to_string(result.ordersCreated.size()) + " orders");
        } else {
            std::string errorMsg = "Rebalancing failed";
            if (!result.errors.empty()) {
                errorMsg += ": " + result.errors[0];
                if (result.errors.size() > 1) {
                    errorMsg += " (+" + std::to_string(result.errors.size() - 1) + " more errors)";
                }
            }
            notifyPortfolioEvent("rebalancing_failed", errorMsg);
        }
        
        return result.success;
    } catch (const std::exception& e) {
        std::cerr << "Error triggering rebalancing: " << e.what() << std::endl;
        notifyPortfolioEvent("rebalancing_error", std::string("Exception: ") + e.what());
        return false;
    }
}

std::unordered_map<std::string, double> PortfolioManager::getPortfolioAllocation() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_portfolioAllocation;
}

std::vector<Position> PortfolioManager::getAllPositions(const std::optional<ExchangeId>& exchangeId) const {
    if (exchangeId) {
        return m_positionTracker->getExchangePositions(*exchangeId);
    } else {
        return m_positionTracker->getAllPositions();
    }
}


std::optional<Position> PortfolioManager::getPosition(const TradingPair& tradingPair, 
                                                     const std::optional<ExchangeId>& exchangeId) const {
    if (exchangeId) {
        return m_positionTracker->getPosition(*exchangeId, tradingPair);
    } else {
        // Return the aggregated position across all exchanges
        auto positions = m_positionTracker->getPositionsByTradingPair(tradingPair);
        if (positions.empty()) {
            return std::nullopt;
        }
        
        // Combine all positions for this trading pair
        double totalQuantity = 0.0;
        double weightedPrice = 0.0;
        double totalNotional = 0.0;
        
        for (const auto& position : positions) {
            double quantity = position.getQuantity();
            double notional = std::abs(quantity) * position.getCurrentPrice();
            
            totalQuantity += quantity;
            totalNotional += notional;
            weightedPrice += position.getCurrentPrice() * notional;
        }
        
        if (totalNotional > 0) {
            weightedPrice /= totalNotional;
        } else {
            // If no notional value, use the price from the first position
            weightedPrice = positions[0].getCurrentPrice();
        }
        
        // Create a virtual position representing the aggregate
        // Need to properly construct StrategyId with both id and version
        // Position(const ExchangeId& exchangeId, const TradingPair& tradingPair, 
        //     const StrategyId& strategyId, double quantity, double averageEntryPrice);
    
        Position aggregatedPosition(
            ExchangeId::all(),
            tradingPair,
            // StrategyId{"AGGREGATED", "1.0"}, // Provide both id and version
            StrategyId{"AGGREGATED", "1.0"},
            totalQuantity,
            weightedPrice
        );

        return aggregatedPosition;
    }
}

bool PortfolioManager::updateConfiguration(const RiskConfig& config) {
    try {
        m_config = config;
        
        // Update sub-component configurations
        m_rebalancingEngine->updateConfiguration(config.getRebalancingConfig());
        m_collateralManager->updateConfiguration(config.getCollateralConfig());
        
        // Re-enable or disable automatic rebalancing
        if (config.portfolioConfig.autoRebalancingEnabled) {
            m_rebalancingEngine->scheduleRebalancingChecks();
        } else {
            m_rebalancingEngine->cancelScheduledRebalancingChecks();
        }
        
        // Recalculate metrics with new config
        recalculatePortfolioMetrics();
        
        notifyPortfolioEvent("config_updated", "Portfolio configuration updated");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error updating portfolio configuration: " << e.what() << std::endl;
        return false;
    }
}

void PortfolioManager::registerPortfolioCallback(
    const std::function<void(const std::string&, const std::string&)>& callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_portfolioCallbacks.push_back(callback);
}

void PortfolioManager::processExecution(const trading::execution::models::ExecutionReport& executionReport) {
    // Process the execution report and update portfolio metrics
    std::cerr << "[PortfolioManager] 🔄 Processing execution: " << executionReport.symbol().toString() 
              << " qty=" << executionReport.quantity() << " side=" << static_cast<int>(executionReport.side()) << std::endl;
    
    try {
            // Diagnostic: log position tracker pointer and timestamp before updating
            auto pt_ptr = m_positionTracker.get();
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            std::cout << "[TTL_DEBUG] PortfolioManager updating PositionTracker ptr=" << pt_ptr << " ts_ms=" << now_ms << std::endl;
            m_positionTracker->updatePositions(executionReport);
        std::cerr << "[PortfolioManager] ✅ PositionTracker updated successfully" << std::endl;
        
        // Verify position was updated
        auto position = m_positionTracker->getPosition(executionReport.exchangeId(), executionReport.symbol());
        if (position.has_value()) {
            std::cerr << "[PortfolioManager] 📊 Position after update: " << executionReport.symbol().toString() 
                      << " = " << position->getQuantity() << std::endl;
        } else {
            std::cerr << "[PortfolioManager] ⚠️ No position found after update!" << std::endl;
        }
        
        recalculatePortfolioMetrics();
    } catch (const std::exception& e) {
        std::cerr << "[PortfolioManager] 💥 Exception in processExecution: " << e.what() << std::endl;
    }
}

void PortfolioManager::recalculatePortfolioMetrics() {
    try {
        // Calculate portfolio value
        double portfolioValue = m_positionTracker->calculatePortfolioValue();
        
        // Calculate asset allocations
        std::unordered_map<std::string, double> assetValues;
        auto positions = m_positionTracker->getAllPositions();
        
        for (const auto& position : positions) {
            // Extract the base asset from the trading pair (e.g., "BTC" from "BTC/USD")
            std::string asset = position.getTradingPair().base;
            if (assetValues.find(asset) == assetValues.end()) {
                assetValues[asset] = 0.0;
            }
            
            double positionValue = position.getNotionalValue();
            assetValues[asset] += positionValue;
        }
        
        // Convert values to allocation percentages
        std::unordered_map<std::string, double> allocation;
        if (portfolioValue > 0) {
            for (const auto& [asset, value] : assetValues) {
                allocation[asset] = value / portfolioValue;
            }
        }
        
        // Update state
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_portfolioValue = portfolioValue;
            m_portfolioAllocation = allocation;
        }
        
        // Check if rebalancing is needed
        if (m_config.portfolioConfig.autoRebalancingEnabled && m_rebalancingEngine->isRebalancingNeeded()) {
            notifyPortfolioEvent("rebalancing_needed", "Portfolio allocation has drifted beyond thresholds");
            
            // Auto-execute rebalancing if configured
            if (m_config.portfolioConfig.autoExecuteRebalancing) {
                triggerRebalancing();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error recalculating portfolio metrics: " << e.what() << std::endl;
    }
}

void PortfolioManager::notifyPortfolioEvent(const std::string& eventType, const std::string& description) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    for (const auto& callback : m_portfolioCallbacks) {
        callback(eventType, description);
    }
}

} // namespace risk
} // namespace trading