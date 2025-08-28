/**
 * Implementation of the portfolio management component
 *
 * File: portfolio_manager.cpp
 * Created: 2025-04-29
 */

#include "portfolio/portfolio_manager.hpp"
#include "interfaces/data_interface.hpp"
#include "interfaces/execution_interface.hpp"
#include "../../execution/models/execution_report.hpp"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <sstream>

namespace trading {
namespace risk {

PortfolioManager::PortfolioManager(const RiskConfig& config)
    : m_config(config)
    , m_portfolioValue(0.0) {
}

bool PortfolioManager::initialize() {
    try {
        // Create shared interfaces for sub-components using factory function
        auto dataInterface = createDataInterface();
        if (!dataInterface->initialize()) {
            std::cerr << "Failed to initialize DataInterface" << std::endl;
            return false;
        }
        
        // Convert unique_ptr to shared_ptr for sharing among components
        std::shared_ptr<DataInterface> sharedDataInterface(dataInterface.release());
        
        // Create execution interface using factory function
        auto executionInterface = createExecutionInterface();
        if (!executionInterface->initialize()) {
            std::cerr << "Failed to initialize ExecutionInterface" << std::endl;
            return false;
        }
        
        // Convert unique_ptr to shared_ptr for sharing among components
        std::shared_ptr<ExecutionInterface> sharedExecutionInterface(executionInterface.release());
        
        // Initialize position tracker
        m_positionTracker = std::make_shared<PositionTracker>(m_config, sharedDataInterface);
        if (!m_positionTracker->initialize()) {
            std::cerr << "Failed to initialize PositionTracker" << std::endl;
            return false;
        }
        
        // Initialize collateral manager
        m_collateralManager = std::make_shared<CollateralManager>(
            m_config, m_positionTracker, sharedExecutionInterface, sharedDataInterface);
        if (!m_collateralManager->initialize()) {
            std::cerr << "Failed to initialize CollateralManager" << std::endl;
            return false;
        }
        
        // Initialize rebalancing engine
        m_rebalancingEngine = std::make_shared<RebalancingEngine>(
            m_config, m_positionTracker, sharedExecutionInterface, sharedDataInterface);
        if (!m_rebalancingEngine->initialize()) {
            std::cerr << "Failed to initialize RebalancingEngine" << std::endl;
            return false;
        }
        
        // Set up callbacks from position tracker
        m_positionTracker->registerPositionUpdateCallback(
            [this](const Position& position, const std::string& updateType) {
                recalculatePortfolioMetrics();
                notifyPortfolioEvent("position_update", 
                                   "Position updated: " + position.getTradingPair().symbol +
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

void PortfolioManager::processExecutionReport(const trading::execution::models::ExecutionReport& executionReport) {
    // Directly forward to position tracker - no conversion needed
    m_positionTracker->processExecutionReport(executionReport);
    
    // Portfolio metrics will be updated via callback
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

std::vector<Position> PortfolioManager::getPositions(const std::optional<ExchangeId>& exchangeId) const {
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
        Position aggregatedPosition(
            ExchangeId::all(),
            tradingPair,
            StrategyId{"AGGREGATED", "1.0"}, // Provide both id and version
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