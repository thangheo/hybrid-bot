/**
 * Interface for the component that maintains current positions across exchanges
 *
 * File: position_tracker.hpp
 * Created: 2025-04-29
 * Updated: 2025-05-24
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "../interfaces/data_interface.hpp"
#include "execution/models/execution_report.hpp"
#include "core/market_types.hpp"
#include "core/direction.hpp"
#include <map>
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>
#include <memory>
#include <functional>

namespace trading {
namespace risk {

// Type aliases for clarity
using ExchangeId = core::ExchangeId;
using Direction = core::Direction;
using MarketData = core::MarketData;
using ExecutionReport = trading::execution::models::ExecutionReport;

// Forward declaration for Fill struct from ExecutionReport
struct Fill {
    std::string fillId;
    std::string orderId;
    double quantity;
    double price;
    double fee;
    std::string feeCurrency;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * Position class representing a trading position
 */
class Position {
public:
    Position(const ExchangeId& exchangeId, const TradingPair& tradingPair, 
            const StrategyId& strategyId, double quantity, double averageEntryPrice);
    
    // Getters
    const ExchangeId& getExchangeId() const;
    const TradingPair& getTradingPair() const;
    const StrategyId& getStrategyId() const;
    double getQuantity() const;
    double getAverageEntryPrice() const;
    double getCurrentPrice() const;
    double getUnrealizedPnl() const;
    double getRealizedPnl() const;
    Timestamp getLastUpdated() const;
    
    // Setters
    void setCurrentPrice(double price);
    void setUnrealizedPnl(double pnl);
    void setRealizedPnl(double pnl);
    void setLastUpdated(Timestamp timestamp);
    
    // Operations
    void updatePosition(double quantity, double price);
    double getNotionalValue() const;
    
private:
    ExchangeId m_exchangeId;
    TradingPair m_tradingPair;
    StrategyId m_strategyId;
    double m_quantity;
    double m_averageEntryPrice;
    double m_currentPrice;
    double m_unrealizedPnl;
    double m_realizedPnl;
    Timestamp m_lastUpdated;
    
    // Helper method
    void updateUnrealizedPnl();
};

/**
 * Function type for position update callback
 */
using PositionUpdateCallback = std::function<void(const Position&, const std::string&)>;

/**
 * PositionTracker maintains current positions across exchanges.
 * It tracks position changes in real-time and provides position data to other components.
 */
class PositionTracker {
public:
    /**
     * Constructor
     * @param config Risk configuration
     * @param dataInterface Interface to access market data
     */
    PositionTracker(const RiskConfig& config, std::shared_ptr<DataInterface> dataInterface);
    // updatePositions
    void updatePositions(const ExecutionReport& executionReport);
    /**
     * Initialize the position tracker
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    // isHealthy
    bool isHealthy() const;
    /**
     * Process an execution report to update positions
     * @param executionReport The execution report to process
     */
    void processExecutionReport(const ExecutionReport& executionReport);
    // reconcilePositions
    // bool reconcilePositions();
    /**
     * Update market data for position valuation
     * @param marketData New market data
     */
    void updateMarketData(const MarketData& marketData);
    // reconcilePositions
    bool reconcilePositions();
    /**
     * Get all positions
     * @return Vector of all positions
     */
    std::vector<Position> getAllPositions() const;
    
    /**
     * Get positions for a specific exchange
     * @param exchangeId The exchange identifier
     * @return Vector of positions for the exchange
     */
    std::vector<Position> getExchangePositions(const ExchangeId& exchangeId) const;
    
    /**
     * Get position for a specific trading pair across all exchanges
     * @param tradingPair The trading pair
     * @return Vector of positions for the trading pair
     */
    std::vector<Position> getPositionsByTradingPair(const TradingPair& tradingPair) const;
    
    /**
     * Get a specific position
     * @param exchangeId The exchange identifier
     * @param tradingPair The trading pair
     * @return Optional position, or nullopt if not found
     */
    std::optional<Position> getPosition(const ExchangeId& exchangeId, const TradingPair& tradingPair) const;
    
    /**
     * Get positions for a specific strategy
     * @param strategyId The strategy identifier
     * @return Vector of positions for the strategy
     */
    std::vector<Position> getStrategyPositions(const StrategyId& strategyId) const;
    
    /**
     * Update position manually (for reconciliation)
     * @param exchangeId The exchange identifier
     * @param tradingPair The trading pair
     * @param quantity The new quantity
     * @param price Optional price for the position
     * @return true if update was successful, false otherwise
     */
    bool updatePosition(const ExchangeId& exchangeId, const TradingPair& tradingPair, 
                       double quantity, std::optional<double> price = std::nullopt);
    
    /**
     * Calculate total portfolio value
     * @return The total portfolio value
     */
    double calculatePortfolioValue() const;
    
    /**
     * Calculate total exposure
     * @return The total exposure
     */
    double calculateTotalExposure() const;
    
    /**
     * Register a callback for position updates
     * @param callback The function to call when positions are updated
     */
    void registerPositionUpdateCallback(const PositionUpdateCallback& callback);
    
    /**
     * Clear all positions (for testing or reset)
     */
    void clearPositions();

private:
    std::shared_ptr<DataInterface> m_dataInterface;
    RiskConfig m_config;
    
    // Position storage - key: <exchangeId, tradingPair>
    mutable std::mutex m_positionsMutex;
    std::map<std::string, Position> m_positions;
    
    // Market data cache - renamed from m_prices for clarity
    mutable std::mutex m_marketDataMutex;
    mutable std::unordered_map<std::string, double> m_priceCache;    
    // Callbacks
    mutable std::mutex m_callbackMutex;
    std::vector<PositionUpdateCallback> m_updateCallbacks;
    
    // Helper methods
    void notifyPositionUpdate(const Position& position, const std::string& updateType);
    bool shouldCreateNewPosition(const ExchangeId& exchangeId, const TradingPair& tradingPair, Direction direction);
    void updatePositionCurrentPrice(Position& position);
    std::string createPositionKey(const ExchangeId& exchangeId, const TradingPair& tradingPair) const;
    double getCurrentPrice(const TradingPair& tradingPair) const;
};

} // namespace risk
} // namespace trading