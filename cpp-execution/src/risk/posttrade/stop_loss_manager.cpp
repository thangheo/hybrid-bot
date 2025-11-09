/**
 * @file stop_loss_manager.cpp
 * @brief Implementation of modern C++20 Stop Loss Manager
 * 
 * @author Trading System Team
 * @date May 26, 2025
 */

#include "risk/posttrade/stop_loss_manager.hpp"
#include "risk/sync/position_synchronizer.hpp"
#include <algorithm>
#include <sstream>
#include <ranges>
#include <optional>
// TODO: Consider using trading::risk::sync::PositionSynchronizer cache for
// low-latency position lookups instead of querying PositionTracker directly.
// Integration point: RiskManager provides a synchronized cache accessible
// when initialized (see risk/sync/position_synchronizer.hpp).
namespace trading::risk {

StopLossManager::StopLossManager(
    const RiskConfig& config,
    std::shared_ptr<PositionTracker> positionTracker,
    std::shared_ptr<DataInterface> dataInterface,
    std::shared_ptr<ExecutionInterface> executionInterface
) : positionTracker_(std::move(positionTracker)),
    dataInterface_(std::move(dataInterface)),
    executionInterface_(std::move(executionInterface)) {
    
    // Extract enhanced config from main config
    config_.enableDefaultStopLoss = config.getStopLossConfig().enableDefaultStopLoss;
    config_.defaultStopLossPercent = config.getStopLossConfig().maxLossPercentage;
    config_.useTrailingStopByDefault = config.getStopLossConfig().enableTrailingStop;
    config_.defaultTrailingDistance = config.getStopLossConfig().trailingStopDistance;
    config_.autoLiquidateOnTrigger = config.getStopLossConfig().forceLiquidation;
    config_.liquidationTimeoutSeconds = config.getStopLossConfig().liquidationDelay;
}

bool StopLossManager::initialize() {
    if (!positionTracker_ || !dataInterface_ || !executionInterface_) {
        return false;
    }
    
    if (!config_.isValid()) {
        return false;
    }
    
    // Initialize stop losses for existing positions
    try {
        // Helper that prefers positionSync_ if injected, falling back to PositionTracker
        auto getAllPositions = [this]() {
            if (positionSync_) return positionSync_->getAllPositions();
            return positionTracker_->getAllPositions();
        };

        auto positions = getAllPositions();
        
        for (const auto& position : positions) {
            if (std::abs(position.getQuantity()) < 1e-8) {
                continue; // Skip zero positions
            }
            
            if (config_.enableDefaultStopLoss && shouldCreateDefaultStopLoss(
                position.getExchangeId(), 
                position.getTradingPair().toString(),
                position.getQuantity())) {
                
                auto stopPrice = calculateDefaultStopPrice(
                    position.getAverageEntryPrice(),
                    position.getQuantity()
                );
                
                setStopLoss(
                    position.getExchangeId(),
                    position.getTradingPair().toString(),
                    StopPrice(stopPrice),
                    config_.useTrailingStopByDefault,
                    config_.useTrailingStopByDefault ? 
                        std::optional(TrailingDistance(config_.defaultTrailingDistance)) : 
                        std::nullopt
                );
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void StopLossManager::processExecutionReport(const execution::models::ExecutionReport& executionReport) {
    const auto& exchangeId = executionReport.exchangeId();
    const auto& tradingPair = executionReport.symbol();
    
    // Check if position was closed
    if (std::abs(executionReport.remainingQuantity()) < 1e-8) {
        removeStopLoss(exchangeId, tradingPair);
        return;
    }
    
    // Get current position
    auto getPositionOpt = [&](const core::ExchangeId& ex, const core::Symbol& pair) -> std::optional<Position> {
        if (positionSync_) {
            try {
                auto pos = positionSync_->getPosition(pair.toString(), ex.toString());
                if (pos.has_value()) return pos;
            } catch (...) {}
        }
        return positionTracker_->getPosition(ex, TradingPair(pair));
    };

    // Get current position
    auto position = getPositionOpt(exchangeId, tradingPair);
    if (!position) {
        return;
    }
    
    const std::string positionKey = createPositionKey(exchangeId, tradingPair);
    
    std::unique_lock lock(stopLossMutex_);
    auto it = stopLosses_.find(positionKey);
    
    if (it == stopLosses_.end() && config_.enableDefaultStopLoss) {
        // Create new stop loss
        lock.unlock();
        
        auto stopPrice = calculateDefaultStopPrice(
            position->getAverageEntryPrice(),
            position->getQuantity()
        );
        
        setStopLoss(
            exchangeId,
            tradingPair,
            StopPrice(stopPrice),
            config_.useTrailingStopByDefault,
            config_.useTrailingStopByDefault ? 
                std::optional(TrailingDistance(config_.defaultTrailingDistance)) : 
                std::nullopt
        );
    } else if (it != stopLosses_.end()) {
        // Update existing stop loss if position direction changed
        auto& stopLoss = *it->second;
        double currentPrice = position->getCurrentPrice();
        
        if (currentPrice > 0.0) {
            stopLoss.updateTrailingStop(currentPrice, position->getQuantity() > 0);
        }
    }
}

std::optional<Position> StopLossManager::getPositionOpt(const core::ExchangeId& exchangeId, const TradingPair& tradingPair) const {
    if (positionSync_) {
        try {
            auto pos = positionSync_->getPosition(tradingPair.toString(), exchangeId.toString());
            
            if (pos.has_value()) return pos;
        } catch (...) {}
    }
    return positionTracker_->getPosition(exchangeId, tradingPair);
}

void StopLossManager::updateMarketData(const core::MarketData& marketData) {
    // Update price cache
    {
        std::unique_lock lock(marketDataMutex_);
        priceCache_[marketData.tradingPair.toString()] = marketData.lastTradePrice;
    }
    
    // Update trailing stops
    updateTrailingStops(marketData.tradingPair, marketData.lastTradePrice);
}

bool StopLossManager::setStopLoss(
    const core::ExchangeId& exchangeId,
    const core::Symbol& tradingPair,
    StopPrice stopPrice,
    bool isTrailing,
    std::optional<TrailingDistance> trailingDistance) {
    
    if (!validateStopLossParameters(stopPrice, isTrailing, trailingDistance)) {
        return false;
    }
    // auto getPositionOpt = [&](const core::ExchangeId& ex, const core::Symbol& pair) -> std::optional<Position> {
    // Get current position to validate
    auto position = getPositionOpt(exchangeId, TradingPair(tradingPair));
    if (!position || std::abs(position->getQuantity()) < 1e-8) {
        return false;
    }
    
    // Get current price
    auto currentPriceOpt = getCurrentPrice(tradingPair);
    if (!currentPriceOpt) {
        auto marketData = dataInterface_->getMarketData(tradingPair, exchangeId.toString());
        if (marketData) {
            currentPriceOpt = marketData->lastTradePrice;
        } 
        if (!currentPriceOpt) {
            return false;
        }
    }
    
    double currentPrice = *currentPriceOpt;
    const std::string positionKey = createPositionKey(exchangeId, tradingPair);
    
    // Create stop loss info
    auto stopLossInfo = std::make_unique<StopLossInfo>();
    stopLossInfo->stopPrice.store(stopPrice.value);
    stopLossInfo->isTrailing.store(isTrailing);
    stopLossInfo->trailingDistance.store(trailingDistance ? trailingDistance->percentage : 0.0);
    stopLossInfo->active.store(true);
    stopLossInfo->setLastUpdated(core::now());
    
    if (isTrailing) {
        if (position->getQuantity() > 0) {
            stopLossInfo->highestPrice.store(currentPrice);
            stopLossInfo->lowestPrice.store(0.0);
        } else {
            stopLossInfo->lowestPrice.store(currentPrice);
            stopLossInfo->highestPrice.store(0.0);
        }
    }
    
    // Store the stop loss
    {
        std::unique_lock lock(stopLossMutex_);
        stopLosses_[positionKey] = std::move(stopLossInfo);
    }
    
    return true;
}

bool StopLossManager::removeStopLoss(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair) {
    
    const std::string positionKey = createPositionKey(exchangeId, tradingPair);
    
    std::unique_lock lock(stopLossMutex_);
    return stopLosses_.erase(positionKey) > 0;
} 
// Update your method signature and implementation
std::optional<StopLossData> StopLossManager::getStopLoss(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair) const {
    
    const std::string positionKey = createPositionKey(exchangeId, tradingPair);
    
    std::shared_lock lock(stopLossMutex_);
    auto it = stopLosses_.find(positionKey);
    
    if (it != stopLosses_.end() && it->second) {
        // Create a non-atomic copy
        StopLossData data;
        data.stopPrice = it->second->stopPrice.load();
        data.isTrailing = it->second->isTrailing.load();
        data.trailingDistance = it->second->trailingDistance.load();
        data.active = it->second->active.load();
        data.highestPrice = it->second->highestPrice.load();
        data.lowestPrice = it->second->lowestPrice.load();
        data.adjustmentCount = it->second->adjustmentCount.load();
        data.lastUpdated = it->second->getLastUpdated();
        
        return data;  // Automatic conversion to std::optional
    }
    
    return std::nullopt;
}
std::vector<EnhancedStopLossEvent> StopLossManager::checkStopLosses() {
    totalChecks_.fetch_add(1);
    
    std::vector<EnhancedStopLossEvent> triggeredEvents;
    
    // Get all active stop losses
    std::map<std::string, std::unique_ptr<StopLossInfo>> activeStopLosses;
    {
        std::shared_lock lock(stopLossMutex_);
        for (const auto& [key, stopLoss] : stopLosses_) {
            if (stopLoss && stopLoss->active.load()) {
                // Create a deep copy for processing
                auto copy = std::make_unique<StopLossInfo>();
                copy->stopPrice.store(stopLoss->stopPrice.load());
                copy->isTrailing.store(stopLoss->isTrailing.load());
                copy->trailingDistance.store(stopLoss->trailingDistance.load());
                copy->active.store(stopLoss->active.load());
                copy->highestPrice.store(stopLoss->highestPrice.load());
                copy->lowestPrice.store(stopLoss->lowestPrice.load());
                copy->adjustmentCount.store(stopLoss->adjustmentCount.load());
                copy->setLastUpdated(stopLoss->getLastUpdated());
                
                activeStopLosses[key] = std::move(copy);
            }
        }
    }
    
    // Process each stop loss
    for (const auto& [positionKey, stopLoss] : activeStopLosses) {
        // Parse position key to extract exchange and trading pair
        auto delimiterPos = positionKey.find(':');
        if (delimiterPos == std::string::npos) continue;
        
        core::ExchangeId exchangeId = core::ExchangeId::fromString(positionKey.substr(0, delimiterPos));
        core::Symbol tradingPair = positionKey.substr(delimiterPos + 1);
        
        // Get current price
        auto currentPriceOpt = getCurrentPrice(tradingPair);
        if (!currentPriceOpt) {
            auto marketData = dataInterface_->getMarketData(tradingPair, exchangeId.toString());
            if (marketData) {
                currentPriceOpt = marketData->lastTradePrice;
            } 
            if (!currentPriceOpt) {
                continue; // Skip if no price available
            }
        }
        
        if (!currentPriceOpt) {
            continue; // Skip if no price available
        }
        
        double currentPrice = *currentPriceOpt;
        
        // Get current position
        auto position = positionTracker_->getPosition(exchangeId, TradingPair(tradingPair));
        if (!position || std::abs(position->getQuantity()) < 1e-8) {
            // Position doesn't exist, remove stop loss
            removeStopLoss(exchangeId, tradingPair);
            continue;
        }
        
        // Check if stop loss is triggered
        if (isStopLossTriggered(*stopLoss, currentPrice, position->getQuantity())) {
            totalTriggers_.fetch_add(1);
            
            // Create stop loss event
            auto event = createStopLossEvent(
                exchangeId, tradingPair, *stopLoss,
                position->getQuantity(), position->getAverageEntryPrice(), currentPrice
            );
            
            // Execute liquidation if enabled
            if (config_.autoLiquidateOnTrigger) {
                auto liquidationOrderId = liquidatePosition(exchangeId, tradingPair);
                if (liquidationOrderId) {
                    event.liquidationInitiated = true;
                    event.liquidationOrderId = *liquidationOrderId;
                    successfulLiquidations_.fetch_add(1);
                } else {
                    event.liquidationInitiated = false;
                }
                totalLiquidations_.fetch_add(1);
            }
            
            triggeredEvents.push_back(event);
            
            // Notify callbacks
            notifyCallbacks(event);
            
            // Deactivate stop loss
            {
                std::unique_lock lock(stopLossMutex_);
                auto it = stopLosses_.find(positionKey);
                if (it != stopLosses_.end() && it->second) {
                    it->second->active.store(false);
                }
            }
        }
    }
    
    return triggeredEvents;
}

std::optional<std::string> StopLossManager::liquidatePosition(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair) {
    
    auto position = positionTracker_->getPosition(exchangeId, TradingPair(tradingPair));
    if (!position || std::abs(position->getQuantity()) < 1e-8) {
        return std::nullopt;
    }
    
    // Create liquidation order
    OrderRequest liquidationOrder;
    liquidationOrder.exchangeId = exchangeId;
    liquidationOrder.tradingPair = tradingPair;
    liquidationOrder.type = OrderType::MARKET;
    liquidationOrder.side = position->getQuantity() > 0 ? core::Direction::SELL : core::Direction::BUY;
    liquidationOrder.quantity = std::abs(position->getQuantity());
    liquidationOrder.timeInForce = "IOC";
    liquidationOrder.timestamp = core::now();
    
    // Generate unique client order ID
    std::ostringstream oss;
    oss << "STOPLOSS_" << exchangeId.toString() << "_" << tradingPair << "_"
        << std::chrono::duration_cast<std::chrono::nanoseconds>(
            core::now().time_since_epoch()).count();
    liquidationOrder.clientOrderId = oss.str();
    
    // Submit order
    try {
        std::string orderId = executionInterface_->submitOrder(liquidationOrder);
        return orderId.empty() ? std::nullopt : std::optional(orderId);
    } catch (...) {
        return std::nullopt;
    }
}

bool StopLossManager::updateConfiguration(const EnhancedStopLossConfig& config) {
    if (!config.isValid()) {
        return false;
    }
    
    std::unique_lock lock(configMutex_);
    config_ = config;
    return true;
}

EnhancedStopLossConfig StopLossManager::getConfiguration() const {
    std::shared_lock lock(configMutex_);
    return config_;
}

void StopLossManager::registerStopLossCallback(const StopLossEventCallback& callback) {
    std::unique_lock lock(callbackMutex_);
    callbacks_.push_back(callback);
}

std::map<std::string, double> StopLossManager::getStatistics() const {
    return {
        {"total_checks", static_cast<double>(totalChecks_.load())},
        {"total_triggers", static_cast<double>(totalTriggers_.load())},
        {"total_liquidations", static_cast<double>(totalLiquidations_.load())},
        {"successful_liquidations", static_cast<double>(successfulLiquidations_.load())},
        {"success_rate", totalLiquidations_.load() > 0 ? 
            static_cast<double>(successfulLiquidations_.load()) / totalLiquidations_.load() : 0.0},
        {"active_stop_losses", static_cast<double>(stopLosses_.size())}
    };
}
std::map<StopLossManager::PositionKey, StopLossData> StopLossManager::getAllStopLosses() const {
    std::map<PositionKey, StopLossData> result;
    
    std::shared_lock lock(stopLossMutex_);
    for (const auto& [positionKey, stopLoss] : stopLosses_) {
        if (!stopLoss) continue;
        
        // Parse position key
        auto delimiterPos = positionKey.find(':');
        if (delimiterPos == std::string::npos) continue;
        
        core::ExchangeId exchangeId = core::ExchangeId::fromString(positionKey.substr(0, delimiterPos));
        core::Symbol tradingPair = positionKey.substr(delimiterPos + 1);
        
        // Create non-atomic copy of stop loss data
        StopLossData data;
        data.stopPrice = stopLoss->stopPrice.load();
        data.isTrailing = stopLoss->isTrailing.load();
        data.trailingDistance = stopLoss->trailingDistance.load();
        data.active = stopLoss->active.load();
        data.highestPrice = stopLoss->highestPrice.load();
        data.lowestPrice = stopLoss->lowestPrice.load();
        data.adjustmentCount = stopLoss->adjustmentCount.load();
        data.lastUpdated = stopLoss->getLastUpdated();

        result[{exchangeId, tradingPair.toString()}] = data;
    }
    
    return result;
}
// std::map<StopLossManager::PositionKey, StopLossInfo> StopLossManager::getAllStopLosses() const {
//     std::map<PositionKey, StopLossInfo> result;
    
//     std::shared_lock lock(stopLossMutex_);
//     for (const auto& [positionKey, stopLoss] : stopLosses_) {
//         if (!stopLoss) continue;
        
//         // Parse position key
//         auto delimiterPos = positionKey.find(':');
//         if (delimiterPos == std::string::npos) continue;
        
//         core::ExchangeId exchangeId = core::ExchangeId::fromString(positionKey.substr(0, delimiterPos));
//         core::Symbol tradingPair = positionKey.substr(delimiterPos + 1);
        
//         // Create copy of stop loss info
//         StopLossInfo copy;
//         copy.stopPrice.store(stopLoss->stopPrice.load());
//         copy.isTrailing.store(stopLoss->isTrailing.load());
//         copy.trailingDistance.store(stopLoss->trailingDistance.load());
//         copy.active.store(stopLoss->active.load());
//         copy.highestPrice.store(stopLoss->highestPrice.load());
//         copy.lowestPrice.store(stopLoss->lowestPrice.load());
//         copy.adjustmentCount.store(stopLoss->adjustmentCount.load());
//         copy.setLastUpdated(stopLoss->getLastUpdated());
        
//         result[{exchangeId, tradingPair}] = std::move(copy);
//     }
    
//     return result;
// }

void StopLossManager::recalculateTrailingStops() {
    std::shared_lock priceLock(marketDataMutex_);
    auto pricesCopy = priceCache_;
    priceLock.unlock();
    
    for (const auto& [symbol, price] : pricesCopy) {
        updateTrailingStops(symbol, price);
    }
}

bool StopLossManager::isHealthy() const noexcept {
    return positionTracker_ && dataInterface_ && executionInterface_ && config_.isValid();
}

std::map<std::string, std::string> StopLossManager::getDiagnostics() const {
    std::map<std::string, std::string> diagnostics;
    
    diagnostics["component_name"] = "StopLossManager";
    diagnostics["is_healthy"] = isHealthy() ? "true" : "false";
    diagnostics["total_stop_losses"] = std::to_string(stopLosses_.size());
    diagnostics["total_checks"] = std::to_string(totalChecks_.load());
    diagnostics["total_triggers"] = std::to_string(totalTriggers_.load());
    diagnostics["config_valid"] = config_.isValid() ? "true" : "false";
    
    // Component availability
    diagnostics["position_tracker_available"] = positionTracker_ ? "true" : "false";
    diagnostics["data_interface_available"] = dataInterface_ ? "true" : "false";
    diagnostics["execution_interface_available"] = executionInterface_ ? "true" : "false";
    
    return diagnostics;
}

// Private helper methods implementation

// std::string StopLossManager::createPositionKey(
std::string StopLossManager::createPositionKey(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair) const {
    return exchangeId.toString() + ":" + tradingPair.toString();
}
std::optional<double> StopLossManager::getCurrentPrice(const core::Symbol& tradingPair) const {
    std::shared_lock lock(marketDataMutex_);
    auto it = priceCache_.find(tradingPair.toString());
    return it != priceCache_.end() ? std::optional(it->second) : std::nullopt;
}

bool StopLossManager::isStopLossTriggered(
    const StopLossInfo& stopLoss, 
    double currentPrice, 
    double positionSize) const noexcept {
    
    double stopPrice = stopLoss.stopPrice.load();
    
    if (positionSize > 0) {
        // Long position: triggered when price falls below stop
        return currentPrice <= stopPrice;
    } else {
        // Short position: triggered when price rises above stop
        return currentPrice >= stopPrice;
    }
}

EnhancedStopLossEvent StopLossManager::createStopLossEvent(
    const core::ExchangeId& exchangeId,
    const core::Symbol& tradingPair,
    const StopLossInfo& stopLoss,
    double positionSize,
    double entryPrice,
    double currentPrice) const {
    
    EnhancedStopLossEvent event;
    event.exchangeId = exchangeId;
    event.tradingPair = tradingPair;
    event.positionSize = positionSize;
    event.entryPrice = entryPrice;
    event.triggerPrice = currentPrice;
    event.stopPrice = stopLoss.stopPrice.load();
    event.lossPercentage = std::abs((currentPrice - entryPrice) / entryPrice);
    event.wasTrailing = stopLoss.isTrailing.load();
    event.adjustmentCount = stopLoss.adjustmentCount.load();
    event.timestamp = core::now();
    
    return event;
}

void StopLossManager::updateTrailingStops(const core::Symbol& symbol, double price) {
    std::shared_lock lock(stopLossMutex_);
    
    for (const auto& [positionKey, stopLoss] : stopLosses_) {
        if (!stopLoss || !stopLoss->active.load() || !stopLoss->isTrailing.load()) {
            continue;
        }
        
        // Extract trading pair from position key
        auto delimiterPos = positionKey.find(':');
        if (delimiterPos == std::string::npos) continue;
        
        core::Symbol tradingPair = positionKey.substr(delimiterPos + 1);
        if (tradingPair != symbol) continue;
        
        // Get position to determine direction
        core::ExchangeId exchangeId = core::ExchangeId::fromString(positionKey.substr(0, delimiterPos));
        auto position = positionTracker_->getPosition(exchangeId, TradingPair(tradingPair));
        if (!position) continue;
        
        // Update trailing stop
        stopLoss->updateTrailingStop(price, position->getQuantity() > 0);
    }
}

void StopLossManager::notifyCallbacks(const EnhancedStopLossEvent& event) {
    std::shared_lock lock(callbackMutex_);
    
    for (const auto& callback : callbacks_) {
        try {
            callback(event);
        } catch (...) {
            // Log error but continue with other callbacks
        }
    }
}

bool StopLossManager::shouldCreateDefaultStopLoss(
    const core::ExchangeId& exchangeId,
    const core::Symbol& tradingPair,
    double positionSize) const {
    
    // Don't create for zero positions
    if (std::abs(positionSize) < 1e-8) {
        return false;
    }
    
    // Check if stop loss already exists
    const std::string positionKey = createPositionKey(exchangeId, tradingPair);
    std::shared_lock lock(stopLossMutex_);
    return stopLosses_.find(positionKey) == stopLosses_.end();
}

double StopLossManager::calculateDefaultStopPrice(double entryPrice, double positionSize) const noexcept {
    if (positionSize > 0) {
        // Long position: stop below entry
        return entryPrice * (1.0 - config_.defaultStopLossPercent);
    } else {
        // Short position: stop above entry
        return entryPrice * (1.0 + config_.defaultStopLossPercent);
    }
}

bool StopLossManager::validateStopLossParameters(
    StopPrice stopPrice,
    bool isTrailing,
    std::optional<TrailingDistance> trailingDistance) const noexcept {
    
    if (stopPrice.value <= 0.0) {
        return false;
    }
    
    if (isTrailing) {
        if (!trailingDistance || trailingDistance->percentage <= 0.0) {
            return false;
        }
        if (trailingDistance->percentage < config_.minTrailingDistance) {
            return false;
        }
    }
    
    return true;
}

} // namespace trading::risk