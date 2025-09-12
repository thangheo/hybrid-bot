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
void Position::updatePosition(double quantity, double price) {
    if (quantity == 0.0 || price <= 0.0) {
        return;
    }
    
    // Determine if we're adding to or reducing the position
    bool sameDirection = (m_quantity >= 0 && quantity >= 0) || (m_quantity < 0 && quantity < 0);
    
    if (sameDirection || m_quantity == 0.0) {
        // Adding to position or establishing new position
        double totalCost = (m_quantity * m_averageEntryPrice) + (quantity * price);
        m_quantity += quantity;
        
        if (std::abs(m_quantity) > 1e-8) { // Avoid division by very small numbers
            m_averageEntryPrice = totalCost / m_quantity;
        }
    } else {
        // Reducing or reversing position
        double closeQuantity = std::min(std::abs(quantity), std::abs(m_quantity));
        
        if (m_quantity > 0) {
            closeQuantity = -closeQuantity; // We're selling
        } else {
            closeQuantity = closeQuantity; // We're buying back
        }
        
        // Calculate realized PnL for the closed portion
        m_realizedPnl += closeQuantity * (price - m_averageEntryPrice);
        
        // Update position
        m_quantity += quantity;
        
        // If position reversed, update average entry price for the new portion
        if ((m_quantity > 0 && quantity > 0 && (quantity + closeQuantity) > 0) ||
            (m_quantity < 0 && quantity < 0 && (quantity + closeQuantity) < 0)) {
            double remainingQuantity = quantity + closeQuantity;
            if (std::abs(remainingQuantity) > 1e-8) {
                m_averageEntryPrice = price;
            }
        }
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
        m_dataInterface->registerMarketDataCallback(
            [this](const MarketData& marketData) {
                this->updateMarketData(marketData);
            }
        );
        
        
        return true;
    } catch (const std::exception& e) {
        // std::cerr << "Error initializing PositionTracker: " << e.what() << std::endl;
        LOG_ERROR(std::format("PositionTracker", "Error initializing: {}", e.what()));
        return false;
    }
}
// // reconcilePositions
// bool PositionTracker::reconcilePositions() {
//     try {
//         // Fetch latest positions from external source (e.g. exchange API)
//         auto externalPositions = m_dataInterface->fetchCurrentPositions();
        
//         std::lock_guard<std::mutex> lock(m_positionsMutex);
        
//         // Clear current positions and rebuild from external data
//         m_positions.clear();
        
//         for (const auto& extPos : externalPositions) {
//             Position position(
//                 extPos.exchangeId,
//                 extPos.tradingPair,
//                 extPos.strategyId,
//                 extPos.quantity,
//                 extPos.averageEntryPrice
//             );
//             position.setCurrentPrice(extPos.currentPrice);
//             position.setUnrealizedPnl(extPos.unrealizedPnl);
//             position.setRealizedPnl(extPos.realizedPnl);
//             position.setLastUpdated(std::chrono::system_clock::now());
            
//             std::string key = createPositionKey(extPos.exchangeId, extPos.tradingPair);
//             m_positions.emplace(key, std::move(position));
//         }
        
//         return true;
//     } catch (const std::exception& e) {
//         std::cerr << "Error reconciling positions: " << e.what() << std::endl;
//         return false;
//     }
// }
void PositionTracker::processExecutionReport(const ExecutionReport& executionReport) {
    try {
        if (executionReport.fills().empty()) {
            return;
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
        
        for (const auto& fill : executionReport.fills()) {
            double signedQuantity = fill.quantity;
            // Convert OrderSide (which is core::Direction) to signed quantity
            if (executionReport.side() == core::Direction::SELL) {
                signedQuantity = -signedQuantity;
            }
            
            totalFillQuantity += signedQuantity;
            totalValue += fill.quantity * fill.price;
        }
        
        if (std::abs(totalFillQuantity) < 1e-8) {
            return;
        }
        
        double weightedAvgPrice = totalValue / std::abs(totalFillQuantity);
        
        // Update or create position
        std::lock_guard<std::mutex> lock(m_positionsMutex);
        std::string positionKey = createPositionKey(exchangeId, tradingPair);
        
        auto it = m_positions.find(positionKey);
        if (it != m_positions.end()) {
            // Update existing position
            it->second.updatePosition(totalFillQuantity, weightedAvgPrice);
            updatePositionCurrentPrice(it->second);
            notifyPositionUpdate(it->second, "modified");
        } else {
            // Create new position
            Position newPosition(exchangeId, tradingPair, strategyId, 
                               totalFillQuantity, weightedAvgPrice);
            updatePositionCurrentPrice(newPosition);
            // m_positions.emplace(positionKey, std::move(newPosition));
            // notifyPositionUpdate(m_positions[positionKey], "new");
            auto [it, inserted] = m_positions.emplace(positionKey, std::move(newPosition));
            notifyPositionUpdate(it->second, "new");
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
        m_positions.clear();
        
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
        LOG_DEBUG(std::format("PositionTracker", "Updated market data: {} @ {}", 
                        marketData.tradingPair.toString(), marketData.price));
        // Update positions with new price
        std::lock_guard<std::mutex> lock(m_positionsMutex);
        for (auto& [key, position] : m_positions) {
            if (position.getTradingPair().toString() == marketData.tradingPair.toString() &&
                position.getExchangeId() == marketData.exchangeId) {
                position.setCurrentPrice(marketData.price);
                notifyPositionUpdate(position, "price_update");
            }
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
        
        std::lock_guard<std::mutex> lock(m_positionsMutex);
        std::string key = createPositionKey(exchangeId, tradingPair);
        
        auto it = m_positions.find(key);
        if (it != m_positions.end()) {
            // Update existing position with direct quantity set
            Position& position = it->second;
            double quantityDelta = quantity - position.getQuantity();
            
            if (std::abs(quantityDelta) > 1e-8) {
                position.updatePosition(quantityDelta, currentPrice);
                notifyPositionUpdate(position, "reconciliation");
            }
        } else if (std::abs(quantity) > 1e-8) {
            // Create new position for reconciliation
            StrategyId reconciliationStrategy;
            reconciliationStrategy.id = "reconciliation";
            reconciliationStrategy.version = "1.0";
            
            Position newPosition(exchangeId, tradingPair, reconciliationStrategy, quantity, currentPrice);
            updatePositionCurrentPrice(newPosition);
            // m_positions.emplace(key, std::move(newPosition));
            // notifyPositionUpdate(m_positions[key], "new_reconciliation");
            auto [it, inserted] = m_positions.emplace(key, std::move(newPosition));
            if (inserted) {
                notifyPositionUpdate(it->second, "new_reconciliation");
            } else {
                std::cerr << "Failed to insert new position during reconciliation" << std::endl;
                return false;
            }
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
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    for (const auto& callback : m_updateCallbacks) {
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
            auto marketData = m_dataInterface->getMarketData(tradingPair.toString());
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