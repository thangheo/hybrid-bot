/**
 * Implementation of the position tracking component
 *
 * File: position_tracker.cpp
 * Created: 2025-04-29
 * Rewritten: 2025-05-24
 */

#include "portfolio/position_tracker.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <common/logger.hpp>
namespace trading {
namespace risk {

// Position class implementation
Position::Position(const ExchangeId& exchangeId, const TradingPair& tradingPair, 
                   const StrategyId& strategyId, double quantity, double averageEntryPrice)
    : m_exchangeId(exchangeId)
    , m_tradingPair(tradingPair)
    , m_strategyId(strategyId)
    , m_quantity(quantity)
    , m_averageEntryPrice(averageEntryPrice)
    , m_currentPrice(averageEntryPrice)
    , m_unrealizedPnl(0.0)
    , m_realizedPnl(0.0)
    , m_lastUpdated(std::chrono::system_clock::now()) {
    
    // Initialize unrealized PnL
    updateUnrealizedPnl();
}

// Position getters
const ExchangeId& Position::getExchangeId() const { return m_exchangeId; }
const TradingPair& Position::getTradingPair() const { return m_tradingPair; }
const StrategyId& Position::getStrategyId() const { return m_strategyId; }
double Position::getQuantity() const { return m_quantity; }
double Position::getAverageEntryPrice() const { return m_averageEntryPrice; }
double Position::getCurrentPrice() const { return m_currentPrice; }
double Position::getUnrealizedPnl() const { return m_unrealizedPnl; }
double Position::getRealizedPnl() const { return m_realizedPnl; }
Timestamp Position::getLastUpdated() const { return m_lastUpdated; }

// Position setters
void Position::setCurrentPrice(double price) { 
    if (price <= 0.0) {
        throw std::invalid_argument("Price must be positive");
    }
    m_currentPrice = price;
    updateUnrealizedPnl();
    m_lastUpdated = std::chrono::system_clock::now();
}

void Position::setUnrealizedPnl(double pnl) { 
    m_unrealizedPnl = pnl; 
    m_lastUpdated = std::chrono::system_clock::now();
}

void Position::setRealizedPnl(double pnl) { 
    m_realizedPnl = pnl; 
    m_lastUpdated = std::chrono::system_clock::now();
}

void Position::setLastUpdated(Timestamp timestamp) { 
    m_lastUpdated = timestamp; 
}

// Position operations
// void Position::updatePosition(double quantity, double price) {
//     if (quantity == 0.0 || price <= 0.0) {
//         return;
//     }
    
//     // Determine if we're adding to or reducing the position
//     bool sameDirection = (m_quantity >= 0 && quantity >= 0) || (m_quantity < 0 && quantity < 0);
    
//     if (sameDirection || m_quantity == 0.0) {
//         // Adding to position or establishing new position
//         double totalCost = (m_quantity * m_averageEntryPrice) + (quantity * price);
//         m_quantity += quantity;
        
//         if (std::abs(m_quantity) > 1e-8) { // Avoid division by very small numbers
//             m_averageEntryPrice = totalCost / m_quantity;
//         }
//     } else {
//         // Reducing or reversing position
//         double closeQuantity = std::min(std::abs(quantity), std::abs(m_quantity));
        
//         if (m_quantity > 0) {
//             closeQuantity = -closeQuantity; // We're selling
//         } else {
//             closeQuantity = closeQuantity; // We're buying back
//         }
        
//         // Calculate realized PnL for the closed portion
//         m_realizedPnl += closeQuantity * (price - m_averageEntryPrice);
        
//         // Update position
//         m_quantity += quantity;
        
//         // If position reversed, update average entry price for the new portion
//         if ((m_quantity > 0 && quantity > 0 && (quantity + closeQuantity) > 0) ||
//             (m_quantity < 0 && quantity < 0 && (quantity + closeQuantity) < 0)) {
//             double remainingQuantity = quantity + closeQuantity;
//             if (std::abs(remainingQuantity) > 1e-8) {
//                 m_averageEntryPrice = price;
//             }
//         }
//     }
    
//     m_currentPrice = price;
//     updateUnrealizedPnl();
//     m_lastUpdated = std::chrono::system_clock::now();
// }
void Position::updatePosition(double quantity, double price) {
    if (quantity == 0.0 || price <= 0.0) return;
    
    double oldQuantity = m_quantity;
    double newQuantity = m_quantity + quantity;
    
    // Case 1: Increasing position size (same direction)
    if ((oldQuantity >= 0 && newQuantity >= 0 && newQuantity > oldQuantity) ||
        (oldQuantity <= 0 && newQuantity <= 0 && newQuantity < oldQuantity)) {
        // Calculate weighted average
        double totalCost = (m_quantity * m_averageEntryPrice) + (quantity * price);
        m_quantity = newQuantity;
        m_averageEntryPrice = totalCost / m_quantity;
    }
    // Case 2: Reducing/closing position
    else if ((oldQuantity > 0 && newQuantity >= 0) ||
             (oldQuantity < 0 && newQuantity <= 0)) {
        double closedQuantity = quantity;
        m_realizedPnl += closedQuantity * (price - m_averageEntryPrice);
        m_quantity = newQuantity;
        // Average price stays the same (only closing)
    }
    // Case 3: Reversing position (cross zero)
    else {
        // Close entire old position
        m_realizedPnl += (-oldQuantity) * (price - m_averageEntryPrice);
        
        // Open new position in opposite direction
        m_quantity = newQuantity;
        m_averageEntryPrice = price;
    }
    
    m_currentPrice = price;
    updateUnrealizedPnl();
    m_lastUpdated = std::chrono::system_clock::now();
}

double Position::getNotionalValue() const {
    return std::abs(m_quantity) * m_currentPrice;
}

void Position::updateUnrealizedPnl() {
    if (std::abs(m_quantity) > 1e-8) {
        m_unrealizedPnl = m_quantity * (m_currentPrice - m_averageEntryPrice);
    } else {
        m_unrealizedPnl = 0.0;
    }
}

// PositionTracker implementation
PositionTracker::PositionTracker(const RiskConfig& config, std::shared_ptr<DataInterface> dataInterface)
    : m_dataInterface(dataInterface)
    , m_config(config) {
}
// updatePositions
void PositionTracker::updatePositions(const ExecutionReport& executionReport) {
    processExecutionReport(executionReport);
}
// isHealthy
bool PositionTracker::isHealthy() const {
    return m_dataInterface && m_dataInterface->isHealthy();
}

bool PositionTracker::initialize() {
    try {
        // Ensure initialize is idempotent
        bool expected = false;
        if (!m_initialized.compare_exchange_strong(expected, true)) {
            //  while (!m_initializationComplete.load()) {
            //     std::this_thread::sleep_for(std::chrono::milliseconds(10));
            // }
            // return m_initializationSuccessful.load();
            // Already initialized
            LOG_WARN("PositionTracker", "initialize() called, but already initialized");
            return true;
        }

        if (!m_dataInterface) {
            LOG_WARN("PositionTracker", "DataInterface is null - running in standalone mode");
            // Don't fail if DataInterface is null - allow standalone operation
            return true;
        }
        // check if data interface is healthy or connected
        if (!m_dataInterface->isConnected()) {
            LOG_WARN("PositionTracker", "DataInterface is not connected - limited functionality");
            // Don't fail on connection issues - may connect later
            return true;
        }
        //already initialized in main.cpp
        // if (!m_dataInterface->initialize()) {
        //     LOG_ERROR("PositionTracker", "Failed to initialize DataInterface");
        //     return false;
        // }
        
        // Register for market data updates if supported
        // Register a callback that captures a weak_ptr to avoid calling into
        // a PositionTracker that has been destroyed. This prevents use-after-free
        // when MarketDataInterface invokes callbacks from worker threads.
        // Register for market data updates if supported. Capture a weak_ptr to avoid
        // calling into a PositionTracker that has been destroyed.
        // --> already init in RiskManager.cpp so not needed here
        // try {
        //     std::weak_ptr<PositionTracker> weak_self = shared_from_this();
        //     m_dataInterface->registerMarketDataCallback(
        //         [weak_self](const MarketData& marketData) {
        //             if (auto self = weak_self.lock()) {
        //                 try {
        //                     self->updateMarketData(marketData);
        //                 } catch (...) {
        //                     // swallow exceptions from callback to avoid crashing caller
        //                 }
        //             }
        //         }
        //     );
        // } catch (const std::exception& e) {
        //     LOG_WARN("PositionTracker", std::format("Failed to register market data callback: {}", e.what()));
        // }
        
        
        return true;
    } catch (const std::exception& e) {
        // std::cerr << "Error initializing PositionTracker: " << e.what() << std::endl;
        LOG_ERROR(std::format("PositionTracker", "Error initializing: {}", e.what()));
        return false;
    }
}

bool PositionTracker::shutdown() {
    try {
        // If DataInterface has a shutdown method, call it
        // Make shutdown idempotent
        bool expected = true;
        if (!m_initialized.compare_exchange_strong(expected, false)) {
            // Was not initialized
            LOG_WARN("PositionTracker", "shutdown() called, but PositionTracker was not initialized");
            return true;
        }

        if (m_dataInterface) {
            try {
                m_dataInterface->shutdown();
                LOG_INFO("PositionTracker", "DataInterface shut down successfully");
            } catch (const std::exception& e) {
                LOG_WARN("PositionTracker", std::format("DataInterface shutdown threw: {}", e.what()));
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during PositionTracker shutdown: " << e.what() << std::endl;
        return false;
    }
}
 
void PositionTracker::processExecutionReport(const ExecutionReport& executionReport) {
    try {
        // Lightweight debug logging to help diagnose missing positions
        try {
            std::ostringstream _pt_oss;
            _pt_oss << "[POSITION_TRACKER_DBG] processExecutionReport: exchange=" << executionReport.exchangeId().toString()
                    << " symbol=" << executionReport.symbol().toString()
                    << " fills=" << executionReport.fills().size();
            LOG_INFO("PositionTracker", _pt_oss.str());
        } catch (...) {
            // fall through if logging formatting fails
        }
        // PRODUCTION FIX: Process FILLED orders even without fills array (Binance timeout/parsing issues)
        if (executionReport.fills().empty()) {
            // Only skip if order is not FILLED or has no filled quantity
            if (executionReport.status() != execution::models::OrderState::FILLED || executionReport.filledQuantity() <= 0.0) {
                return;
            }
            // For FILLED orders without fills array, create synthetic fill from order data
        }
        
        // Convert ExecutionReport data to our internal types
        const core::ExchangeId& exchangeId = executionReport.exchangeId();
        const core::Symbol& tradingPairSymbol = executionReport.symbol();
        
        // Create TradingPair from symbol string
        TradingPair tradingPair = TradingPair(tradingPairSymbol);
        if (tradingPair.isEmpty()) {
            std::cerr << "Invalid trading pair symbol: " << tradingPairSymbol << std::endl;
            return;
        }       
        
        // Create StrategyId - ExecutionReport doesn't have strategy info, use default
        StrategyId strategyId;
        strategyId.id = "default";
        strategyId.version = "1.0";
        
        // Calculate total fill quantity and weighted average price
        double totalFillQuantity = 0.0;
        double totalValue = 0.0;
        
        if (!executionReport.fills().empty()) {
            // Process actual fills
            for (const auto& fill : executionReport.fills()) {
                double signedQuantity = fill.quantity;
                // Convert OrderSide (which is core::Direction) to signed quantity
                if (executionReport.side() == core::Direction::SELL) {
                    signedQuantity = -signedQuantity;
                }
                
                totalFillQuantity += signedQuantity;
                totalValue += fill.quantity * fill.price;
            }
        } else {
            // PRODUCTION FIX: Create synthetic fill from order data for FILLED orders without fills array
            double fillQuantity = executionReport.filledQuantity();
            double fillPrice = executionReport.price().value_or(0.0);
            
            // CRITICAL: For MARKET orders without price, try to get current market price
            if (fillPrice <= 0.0) {
                // Try to get current market price from m_dataInterface
                if (m_dataInterface) {
                    try {
                        fillPrice = m_dataInterface->getCurrentPrice(tradingPairSymbol, 
                                                                    executionReport.exchangeId().toString());
                        if (fillPrice > 0.0) {
                            LOG_INFO("PositionTracker: Retrieved market price for MARKET order - symbol={} price={}", 
                                   tradingPairSymbol.toString(), fillPrice);
                        } else {
                            throw std::runtime_error("Market price is zero or negative");
                        }
                    } catch (const std::exception& e) {
                        LOG_WARN("PositionTracker: Failed to get market price for MARKET order - symbol={} qty={} error={} - SKIPPING position update", 
                               tradingPairSymbol.toString(), fillQuantity, e.what());
                        return;
                    }
                } else {
                    LOG_WARN("PositionTracker: No m_dataInterface available for MARKET order price - symbol={} qty={} - SKIPPING position update", 
                           tradingPairSymbol.toString(), fillQuantity);
                    return;
                }
            }
            
            if (executionReport.side() == core::Direction::SELL) {
                fillQuantity = -fillQuantity;
            }
            
            totalFillQuantity = fillQuantity;
            totalValue = std::abs(fillQuantity) * fillPrice; // Use abs for value calculation
            
            LOG_INFO("PositionTracker: Processing FILLED order without fills array - symbol={} qty={} price={}", 
                     tradingPairSymbol.toString(), fillQuantity, fillPrice);
        }
        
        if (std::abs(totalFillQuantity) < 1e-8) {
            return;
        }
        
        // SAFETY: Prevent division by zero in weighted average price calculation
        double weightedAvgPrice = (std::abs(totalFillQuantity) > 1e-8) ? 
            (totalValue / std::abs(totalFillQuantity)) : 0.0;
        
        // Update or create position
        std::optional<Position> toNotify;
        std::string notifyType;

        {
            std::lock_guard<std::mutex> lock(m_positionsMutex);
            std::string positionKey = createPositionKey(exchangeId, tradingPair);

            auto it = m_positions.find(positionKey);
            if (it != m_positions.end()) {
                // Update existing position
                it->second.updatePosition(totalFillQuantity, weightedAvgPrice);
                updatePositionCurrentPrice(it->second);
                try {
                    std::ostringstream _pt_upd;
                    _pt_upd << "[POSITION_TRACKER_DBG] updated position: key=" << positionKey
                            << " qty=" << it->second.getQuantity()
                            << " avgPrice=" << it->second.getAverageEntryPrice();
                    LOG_INFO("PositionTracker", _pt_upd.str());
                } catch (...) {}
                // make a copy to notify outside the lock
                toNotify = it->second;
                notifyType = "modified";
            } else {
                // Create new position
                Position newPosition(exchangeId, tradingPair, strategyId, 
                                   totalFillQuantity, weightedAvgPrice);
                updatePositionCurrentPrice(newPosition);
                auto [it2, inserted] = m_positions.emplace(positionKey, std::move(newPosition));
                if (inserted) {
                    try {
                        std::ostringstream _pt_new;
                        _pt_new << "[POSITION_TRACKER_DBG] created position: key=" << positionKey
                                << " qty=" << totalFillQuantity << " price=" << weightedAvgPrice;
                        LOG_INFO("PositionTracker", _pt_new.str());
                    } catch (...) {}
                    toNotify = it2->second;
                    notifyType = "new";
                }
            }
        }

        if (toNotify.has_value()) {
            notifyPositionUpdate(*toNotify, notifyType);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing execution report: " << e.what() << std::endl;
    }
}
// reconcilePositions
bool PositionTracker::reconcilePositions(ExchangeId exchangeId) {
    try {
        // Fetch latest positions from external source (e.g. exchange API)
        auto externalPositions = m_dataInterface->fetchCurrentPositions();
        
        std::lock_guard<std::mutex> lock(m_positionsMutex);
        
        // Clear current positions and rebuild from external data
        // m_positions.clear();
        // Only clear positions for THIS exchange
        auto it = m_positions.begin();
        while (it != m_positions.end()) {
            if (it->second.getExchangeId() == exchangeId) {
                it = m_positions.erase(it);
            } else {
                ++it;
            }
        }
        
        for (const auto& extPos : externalPositions) {
            Position position(
                extPos.getExchangeId(),
                extPos.getTradingPair(),
                extPos.getStrategyId(),
                extPos.getQuantity(),
                extPos.getAverageEntryPrice()
            );
            position.setCurrentPrice(extPos.getCurrentPrice());
            position.setUnrealizedPnl(extPos.getUnrealizedPnl());
            position.setRealizedPnl(extPos.getRealizedPnl());
            position.setLastUpdated(std::chrono::system_clock::now());

            std::string key = createPositionKey(extPos.getExchangeId(), extPos.getTradingPair());
            m_positions.emplace(key, std::move(position));
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error reconciling positions: " << e.what() << std::endl;
        return false;
    }
}
void PositionTracker::updateMarketData(const MarketData& marketData) {
    try {
        // Update price cache
        {
            std::lock_guard<std::mutex> lock(m_marketDataMutex);
            m_priceCache[marketData.tradingPair.toString()] = marketData.price;
        }
        // Update positions with new price, but collect notifications to send outside the lock
        std::vector<Position> notifyList;
        {
            std::lock_guard<std::mutex> lock(m_positionsMutex);
            for (auto& [key, position] : m_positions) {
                if (position.getTradingPair().toString() == marketData.tradingPair.toString() &&
                    position.getExchangeId() == marketData.exchangeId) {
                    position.setCurrentPrice(marketData.price);
                    notifyList.push_back(position);
                }
            }
        }

        for (const auto& p : notifyList) {
            notifyPositionUpdate(p, "price_update");
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error updating market data: " << e.what() << std::endl;
    }
}

std::vector<Position> PositionTracker::getAllPositions() const {
    std::lock_guard<std::mutex> lock(m_positionsMutex);
    
    std::vector<Position> positions;
    positions.reserve(m_positions.size());
    
    for (const auto& [key, position] : m_positions) {
        positions.push_back(position);
    }

    try {
        std::ostringstream _pt_gall;
        _pt_gall << "[POSITION_TRACKER_DBG] getAllPositions returning count=" << positions.size();
        LOG_INFO("PositionTracker", _pt_gall.str());
    } catch (...) {}
    
    return positions;
}

std::vector<Position> PositionTracker::getExchangePositions(const ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(m_positionsMutex);
    
    std::vector<Position> positions;
    for (const auto& [key, position] : m_positions) {
        if (position.getExchangeId() == exchangeId) {
            positions.push_back(position);
        }
    }
    
    return positions;
}

std::vector<Position> PositionTracker::getPositionsByTradingPair(const TradingPair& tradingPair) const {
    std::lock_guard<std::mutex> lock(m_positionsMutex);
    
    std::vector<Position> positions;
    for (const auto& [key, position] : m_positions) {
        if (position.getTradingPair() == tradingPair) {
            positions.push_back(position);
        }
    }
    
    return positions;
}

std::optional<Position> PositionTracker::getPosition(const ExchangeId& exchangeId, 
                                                   const TradingPair& tradingPair) const {
    std::lock_guard<std::mutex> lock(m_positionsMutex);
    
    std::string key = createPositionKey(exchangeId, tradingPair);
    auto it = m_positions.find(key);
    
    if (it != m_positions.end()) {
        return it->second;
    }
    
    return std::nullopt;
}

std::vector<Position> PositionTracker::getStrategyPositions(const StrategyId& strategyId) const {
    std::lock_guard<std::mutex> lock(m_positionsMutex);
    
    std::vector<Position> positions;
    for (const auto& [key, position] : m_positions) {
        if (position.getStrategyId() == strategyId) {
            positions.push_back(position);
        }
    }
    
    return positions;
}

bool PositionTracker::updatePosition(const ExchangeId& exchangeId, const TradingPair& tradingPair, 
                                   double quantity, std::optional<double> price) {
    try {
        double currentPrice = price.value_or(getCurrentPrice(tradingPair));
        if (currentPrice <= 0.0) {
            std::cerr << "Invalid price for position update: " << currentPrice << std::endl;
            return false;
        }
        
        std::optional<Position> toNotifyPos;
        std::string toNotifyType;

        {
            std::lock_guard<std::mutex> lock(m_positionsMutex);
            std::string key = createPositionKey(exchangeId, tradingPair);

            auto it = m_positions.find(key);
            if (it != m_positions.end()) {
                // Update existing position with direct quantity set
                Position& position = it->second;
                double quantityDelta = quantity - position.getQuantity();

                if (std::abs(quantityDelta) > 1e-8) {
                    position.updatePosition(quantityDelta, currentPrice);
                    toNotifyPos = position; // copy for notify
                    toNotifyType = "reconciliation";
                }
            } else if (std::abs(quantity) > 1e-8) {
                // Create new position for reconciliation
                StrategyId reconciliationStrategy;
                reconciliationStrategy.id = "reconciliation";
                reconciliationStrategy.version = "1.0";

                Position newPosition(exchangeId, tradingPair, reconciliationStrategy, quantity, currentPrice);
                updatePositionCurrentPrice(newPosition);
                auto [it2, inserted] = m_positions.emplace(key, std::move(newPosition));
                if (inserted) {
                    toNotifyPos = it2->second;
                    toNotifyType = "new_reconciliation";
                } else {
                    std::cerr << "Failed to insert new position during reconciliation" << std::endl;
                    return false;
                }
            }
        }

        if (toNotifyPos.has_value()) {
            notifyPositionUpdate(*toNotifyPos, toNotifyType);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error updating position: " << e.what() << std::endl;
        return false;
    }
}

double PositionTracker::calculatePortfolioValue() const {
    std::lock_guard<std::mutex> lock(m_positionsMutex);
    
    double totalValue = 0.0;
    for (const auto& [key, position] : m_positions) {
        totalValue += position.getNotionalValue();
    }
    
    return totalValue;
}

double PositionTracker::calculateTotalExposure() const {
    std::lock_guard<std::mutex> lock(m_positionsMutex);
    
    double totalExposure = 0.0;
    for (const auto& [key, position] : m_positions) {
        totalExposure += position.getNotionalValue();
    }
    
    return totalExposure;
}

double PositionTracker::getCachedPrice(const TradingPair& tradingPair) const {
    std::lock_guard<std::mutex> lk(m_marketDataMutex);
    const std::string key = tradingPair.toString();
    auto it = m_priceCache.find(key);
    if (it != m_priceCache.end()) return it->second;
    // Attempt to query data interface if available
    try {
        if (m_dataInterface) {
            double p = m_dataInterface->getCurrentPrice(tradingPair.toString());
            if (p > 0.0) return p;
        }
    } catch (...) {
        // ignore
    }
    return 0.0;
}

void PositionTracker::registerPositionUpdateCallback(const PositionUpdateCallback& callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_updateCallbacks.push_back(callback);
}

void PositionTracker::clearPositions() {
    std::lock_guard<std::mutex> lock(m_positionsMutex);
    m_positions.clear();
}

// Private helper methods
void PositionTracker::notifyPositionUpdate(const Position& position, const std::string& updateType) {
    // Copy callbacks under lock, then invoke outside the lock to avoid
    // potential deadlocks if callbacks call back into PositionTracker.
    std::vector<PositionUpdateCallback> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callbacksCopy = m_updateCallbacks;
    }

    for (const auto& callback : callbacksCopy) {
        try {
            callback(position, updateType);
        } catch (const std::exception& e) {
            std::cerr << "Error in position update callback: " << e.what() << std::endl;
        }
    }
}

void PositionTracker::updatePositionCurrentPrice(Position& position) {
    double currentPrice = getCurrentPrice(position.getTradingPair());
    if (currentPrice > 0.0) {
        position.setCurrentPrice(currentPrice);
    }
}

std::string PositionTracker::createPositionKey(const ExchangeId& exchangeId, 
                                             const TradingPair& tradingPair) const {
    // Convert ExchangeId to string - assuming it has conversion or is already string-like
    std::string exchangeStr = exchangeId.toString();
    return exchangeStr + ":" + tradingPair.toString();
}

double PositionTracker::getCurrentPrice(const TradingPair& tradingPair) const {
    // First check cache
    {
        std::lock_guard<std::mutex> lock(m_marketDataMutex);
        auto it = m_priceCache.find(tradingPair.toString());
        if (it != m_priceCache.end()) {
            return it->second;
        }
    }
    
    // If not in cache, try to get from data interface
    if (m_dataInterface) {
        try {
            core::Symbol sym(tradingPair.toString());
            auto marketData = m_dataInterface->getMarketData(sym);
            if (marketData.has_value()) {
                // Update cache
                std::lock_guard<std::mutex> lock(m_marketDataMutex);
                m_priceCache[tradingPair.toString()] = marketData->price;
                return marketData->price;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error getting current price from data interface: " << e.what() << std::endl;
        }
    }
    
    return 0.0; // Price not available
}

// Legacy method - not used in current implementation
bool PositionTracker::shouldCreateNewPosition(const ExchangeId& exchangeId, 
                                            const TradingPair& tradingPair, 
                                            Direction direction) {
    std::string key = createPositionKey(exchangeId, tradingPair);
    
    auto it = m_positions.find(key);
    return it == m_positions.end();
}

} // namespace risk
} // namespace trading