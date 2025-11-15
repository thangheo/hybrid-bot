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
#include <thread>
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
        // CRASH FIX: Validate input data before processing
        if (executionReport.symbol().toString().empty()) {
            std::cerr << "PositionTracker: Empty symbol in execution report - skipping" << std::endl;
            return;
        }
        
        // Accept reports where the declared order quantity may be zero
        // (some execution adapters populate only filledQuantity for FILLED reports).
        // Only skip if both quantity and filledQuantity are invalid/zero.
        bool hasValidQuantity = std::isfinite(executionReport.quantity()) && executionReport.quantity() > 0.0;
        bool hasValidFilled = std::isfinite(executionReport.filledQuantity()) && executionReport.filledQuantity() > 0.0;
        if (!hasValidQuantity && !hasValidFilled) {
            std::cerr << "PositionTracker: Invalid quantity in execution report: " 
                      << executionReport.quantity() << " filled=" << executionReport.filledQuantity() << " - skipping" << std::endl;
            return;
        }
        
        if (!std::isfinite(executionReport.filledQuantity()) || executionReport.filledQuantity() < 0.0) {
            std::cerr << "PositionTracker: Invalid filled quantity in execution report: " 
                      << executionReport.filledQuantity() << " - skipping" << std::endl;
            return;
        }

        // Lightweight debug logging to help diagnose missing positions
        try {
            std::ostringstream _pt_oss;
            _pt_oss << "[POSITION_TRACKER_DBG] processExecutionReport: exchange=" << executionReport.exchangeId().toString()
                    << " symbol=" << executionReport.symbol().toString()
                    << " fills=" << executionReport.fills().size()
                    << " qty=" << executionReport.quantity()
                    << " filledQty=" << executionReport.filledQuantity();
            LOG_TRACE("PositionTracker", _pt_oss.str());
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
                if (!m_dataInterface) {
                    LOG_WARN("PositionTracker: No m_dataInterface available for MARKET order price - symbol={} qty={} - SKIPPING position update", 
                             tradingPairSymbol.toString(), fillQuantity);
                    return;
                }

                try {
                    // Prefer exchange-specific lookup if exchange is known
                    std::string exchStr = executionReport.exchangeId().toString();
                    if (!exchStr.empty() && exchStr != "UNKNOWN") {
                        try {
                            fillPrice = m_dataInterface->getCurrentPrice(tradingPairSymbol, exchStr);
                            if (fillPrice > 0.0) {
                                LOG_INFO("PositionTracker: Retrieved market price (exchange) for MARKET order - symbol={} exchange={} price={}",
                                         tradingPairSymbol.toString(), exchStr, fillPrice);
                            }
                        } catch (const std::exception& /*e*/) {
                            // fall through to generic lookup
                            fillPrice = 0.0;
                        }
                    }

                    // Fallback: try generic lookup (no exchange)
                    if (fillPrice <= 0.0) {
                        fillPrice = m_dataInterface->getCurrentPrice(tradingPairSymbol);
                        if (fillPrice > 0.0) {
                            LOG_INFO("PositionTracker: Retrieved market price (generic) for MARKET order - symbol={} price={}",
                                     tradingPairSymbol.toString(), fillPrice);
                        }
                    }

                    // Last resort: REST lookup which may be rate-limited
                    if (fillPrice <= 0.0) {
                        try {
                            fillPrice = m_dataInterface->getPriceFromRest(tradingPairSymbol, std::nullopt);
                            if (fillPrice > 0.0) {
                                LOG_INFO("PositionTracker: Retrieved market price (REST fallback) for MARKET order - symbol={} price={}",
                                         tradingPairSymbol.toString(), fillPrice);
                            }
                        } catch (const std::exception& /*e*/) {
                            // ignore and handle below
                        }
                    }

                    if (fillPrice <= 0.0) {
                        LOG_WARN("PositionTracker: Failed to get market price for MARKET order after fallbacks - symbol={} qty={} - SKIPPING position update",
                                 tradingPairSymbol.toString(), fillQuantity);
                        return;
                    }
                } catch (const std::exception& e) {
                    LOG_WARN("PositionTracker: Failed to get market price for MARKET order - symbol={} qty={} error={} - SKIPPING position update",
                             tradingPairSymbol.toString(), fillQuantity, e.what());
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

        bool needReconcile = false;
        {
            std::lock_guard<std::mutex> lock(m_positionsMutex);
            std::string positionKey = createPositionKey(exchangeId, tradingPair);

            auto it = m_positions.find(positionKey);
            if (it != m_positions.end()) {
                // Spot-only safeguard: if applying this fill would produce a negative net
                // position (i.e. prospective short) then don't apply the fill locally —
                // instead request reconciliation from exchange to get authoritative state.
                double currentQty = it->second.getQuantity();
                double prospectiveQty = currentQty + totalFillQuantity;
                if (prospectiveQty < -1e-8) {
                    try {
                        LOG_WARN("PositionTracker", "Prospective negative position detected for {} on {} (current={}, delta={}) - scheduling reconciliation and skipping update",
                                 tradingPairSymbol.toString(), exchangeId.toString(), currentQty, totalFillQuantity);
                    } catch (...) {}
                    needReconcile = true;
                } else {
                    // Safe to apply update
                    it->second.updatePosition(totalFillQuantity, weightedAvgPrice);
                    updatePositionCurrentPrice(it->second);
                    try {
                        std::ostringstream _pt_upd;
                        _pt_upd << "[POSITION_TRACKER_DBG] updated position: key=" << positionKey
                                << " qty=" << it->second.getQuantity()
                                << " avgPrice=" << it->second.getAverageEntryPrice();
                        LOG_TRACE("PositionTracker", _pt_upd.str());
                    } catch (...) {}
                    // make a copy to notify outside the lock
                    toNotify = it->second;
                    notifyType = "modified";
                }
            } else {
                // Create new position
                // If the fill itself is negative (a short) and we only trade spot, skip and reconcile
                if (totalFillQuantity < -1e-8) {
                    try {
                        LOG_WARN("PositionTracker", "Received negative fill for new spot position {} on {} (delta={}) - scheduling reconciliation and skipping creation",
                                 tradingPairSymbol.toString(), exchangeId.toString(), totalFillQuantity);
                    } catch(...) {}
                    needReconcile = true;
                } else {
                    Position newPosition(exchangeId, tradingPair, strategyId, 
                                       totalFillQuantity, weightedAvgPrice);
                    updatePositionCurrentPrice(newPosition);
                    auto [it2, inserted] = m_positions.emplace(positionKey, std::move(newPosition));
                    if (inserted) {
                        try {
                            std::ostringstream _pt_new;
                            _pt_new << "[POSITION_TRACKER_DBG] created position: key=" << positionKey
                                    << " qty=" << totalFillQuantity << " price=" << weightedAvgPrice;
                            LOG_TRACE("PositionTracker", _pt_new.str());
                        } catch (...) {}
                        toNotify = it2->second;
                        notifyType = "new";
                    }
                }
            }
        }

        // If we flagged a need to reconcile, perform reconciliation outside the positions mutex
        if (needReconcile) {
            try {
                if (m_dataInterface) {
                    // Best-effort: fetch authoritative positions for this exchange
                    reconcilePositions(exchangeId);
                } else {
                    try { LOG_WARN("PositionTracker", "No DataInterface available to reconcile positions for {}", exchangeId.toString()); } catch(...) {}
                }
            } catch (const std::exception& e) {
                try { LOG_WARN("PositionTracker", "reconcilePositions failed for {}: {}", exchangeId.toString(), e.what()); } catch(...) {}
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
        // No local cache needed - MarketDataInterface has its own cache
        // Just update positions with new price
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
        LOG_TRACE("PositionTracker", _pt_gall.str());
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
    const std::string key = tradingPair.toString();
    
    
    // Use MarketDataInterface cache directly (5s TTL from config)
    try {
        if (m_dataInterface) {
            double p = m_dataInterface->getCurrentPrice(tradingPair.toString());
            // LOG_WARN("PRICE_FROM_INTERFACE: pair={} price={}", key, p);
            if (p > 0.0) return p;
        }
    } catch (const std::exception& e) {
        LOG_WARN("PRICE_INTERFACE_ERROR: pair={} error={}", key, e.what());
    } 
    
    LOG_WARN("PRICE_NOT_FOUND: pair={}", key);
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

    // THREAD EXPLOSION FIX: Execute callbacks synchronously to prevent 
    // runaway thread creation that was causing system crashes.
    // The old async approach with detached threads was creating hundreds
    // of threads after each order execution, causing resource exhaustion.
    for (const auto& callback : callbacksCopy) {
        try {
            callback(position, updateType);
        } catch (const std::exception& e) {
            std::cerr << "Error in position update callback: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error in position update callback" << std::endl;
        }
    }
}

void PositionTracker::updatePositionCurrentPrice(Position& position) {
    try {
        double currentPrice = getCurrentPrice(position.getTradingPair());
        if (currentPrice > 0.0 && std::isfinite(currentPrice)) {
            position.setCurrentPrice(currentPrice);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error updating position current price for " << position.getTradingPair().toString() 
                  << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error updating position current price for " << position.getTradingPair().toString() << std::endl;
    }
}

std::string PositionTracker::createPositionKey(const ExchangeId& exchangeId, 
                                             const TradingPair& tradingPair) const {
    // Convert ExchangeId to string - assuming it has conversion or is already string-like
    std::string exchangeStr = exchangeId.toString();
    return exchangeStr + ":" + tradingPair.toString();
}

double PositionTracker::getCurrentPrice(const TradingPair& tradingPair) const {
    try {
        // Use MarketDataInterface directly (has 5s cache + orderbook data)
        if (m_dataInterface) {
            return m_dataInterface->getCurrentPrice(tradingPair.toString());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error getting current price from data interface for " 
                  << tradingPair.toString() << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error getting current price from data interface for " 
                  << tradingPair.toString() << std::endl;
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