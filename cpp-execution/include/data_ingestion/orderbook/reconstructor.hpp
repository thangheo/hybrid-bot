/**
 * @file reconstructor.hpp
 * @brief Header for order book reconstruction logic
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * Refactored to use OrderBookManager for proper separation of concerns.
 * 
 * Created on: April 30, 2025
 * Refactored on: September 11, 2025
 */

#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>

#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
// #include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "communication/zmq_server.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/orderbook/order_book_manager.hpp"
#include "strategy/core_analysis/liquidity_flow_monitor.hpp"
#include "nlohmann/json.hpp"

using namespace trading::communication;
using namespace trading::core;
namespace trading {
namespace data_ingestion {

/**
 * @brief Singleton class for reconstructing order books from market data
 * 
 * The OrderBookReconstructor is responsible for:
 * - Processing raw market data from exchanges
 * - Converting data into standardized OrderBookUpdate format
 * - Delegating order book management to OrderBookManager
 * - Publishing order book updates via ZeroMQ
 * 
 * This class now acts as a data processor and delegates all order book
 * lifecycle management to OrderBookManager for proper separation of concerns.
 */
class OrderBookReconstructor {
public:
    /**
     * @brief Get the singleton instance
     * 
     * @return OrderBookReconstructor& 
     */
    static OrderBookReconstructor& getInstance();

    // Delete copy and move constructors/assignments
    OrderBookReconstructor(const OrderBookReconstructor&) = delete;
    OrderBookReconstructor& operator=(const OrderBookReconstructor&) = delete;
    OrderBookReconstructor(OrderBookReconstructor&&) = delete;
    OrderBookReconstructor& operator=(OrderBookReconstructor&&) = delete;

    /**
     * @brief Destroy the Order Book Reconstructor object
     */
    ~OrderBookReconstructor();

    /**
     * @brief Initialize the reconstructor with a publisher and order book manager
     * 
     * @param publisher Shared pointer to ZeroMQ publisher
     * @param manager Shared pointer to OrderBookManager
     * @return true if successful, false otherwise
     */
    bool initialize(
        std::shared_ptr<TradingCommunicationServer> publisher,
        std::shared_ptr<OrderBookManager> manager
    );
     
    /**
     * @brief Check if the reconstructor is initialized
     * 
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Shutdown the reconstructor
     */
    void shutdown();

    /**
     * @brief Process order book update from JSON data (Binance format)
     * 
     * @param exchangeId Exchange identifier
     * @param data JSON data containing order book updates
     * @param symbol Trading symbol
     */
    void updateOrderBookFromJson(ExchangeId exchangeId, const nlohmann::json& data, const core::Symbol& symbol);
     
    /**
     * @brief Process a market data snapshot
     * 
     * @param exchange Exchange identifier
     * @param symbol Trading symbol
     * @param bids Vector of bid price levels
     * @param asks Vector of ask price levels
     * @param sequenceNumber Sequence number of the snapshot
     * @param timestamp Timestamp of the snapshot
     * @return true if successful, false otherwise
     */
    bool processSnapshot(
        ExchangeId exchange,
        const core::Symbol& symbol,
        const std::vector<PriceLevel>& bids,
        const std::vector<PriceLevel>& asks,
        uint64_t sequenceNumber,
        core::Timestamp timestamp
    );
     
    /**
     * @brief Process a market data update
     * 
     * @param exchange Exchange identifier
     * @param symbol Trading symbol
     * @param bids Vector of bid price levels to update
     * @param asks Vector of ask price levels to update
     * @param sequenceNumber Sequence number of the update
     * @param timestamp Timestamp of the update
     * @return true if successful, false otherwise
     */
    bool processUpdate(
        ExchangeId exchange,
        const core::Symbol& symbol,
        const std::vector<PriceLevel>& bids,
        const std::vector<PriceLevel>& asks,
        uint64_t sequenceNumber,
        core::Timestamp timestamp
    );

    /**
     * @brief Register liquidity flow monitor for delta tracking
     * 
     * @param monitor Shared pointer to liquidity flow monitor
     */
    void registerLiquidityFlowMonitor(
        std::shared_ptr<strategy::LiquidityFlowMonitor> monitor
    );
    
    /**
     * @brief Get an order book (delegates to manager)
     * 
     * @param exchange Exchange identifier
     * @param symbol Trading symbol
     * @return std::shared_ptr<OrderBook> 
     */
    std::shared_ptr<OrderBook> getOrderBook(
        ExchangeId exchange, 
        const core::Symbol& symbol
    ) const;
     
    /**
     * @brief Get all order books for a specific exchange (delegates to manager)
     * 
     * @param exchange Exchange identifier
     * @return std::vector<core::Symbol> symbols available on exchange
     */
    std::vector<core::Symbol> getOrderBooksForExchange(ExchangeId exchange) const;
     
    /**
     * @brief Check if an order book exists (delegates to manager)
     * 
     * @param exchange Exchange identifier
     * @param symbol Trading symbol
     * @return true if exists, false otherwise
     */
    bool hasOrderBook(ExchangeId exchange, const core::Symbol& symbol) const;
     
    /**
     * @brief Remove an order book (delegates to manager)
     * 
     * @param exchange Exchange identifier
     * @param symbol Trading symbol
     */
    void removeOrderBook(ExchangeId exchange, const core::Symbol& symbol);
     
    /**
     * @brief Clear all order books (delegates to manager)
     */
    void clearOrderBooks();
     
    /**
     * @brief Validate an order book (delegates to manager)
     * 
     * @param exchange Exchange identifier
     * @param symbol Trading symbol
     * @return true if valid, false otherwise
     */
    bool validateOrderBook(ExchangeId exchange, const core::Symbol& symbol);
     
    /**
     * @brief Request a snapshot from an exchange
     * 
     * @param exchange Exchange identifier
     * @param symbol Trading symbol
     * @return true if request sent, false otherwise
     */
    bool requestSnapshot(ExchangeId exchange, const core::Symbol& symbol);

    /**
     * @brief Get order book manager statistics
     * 
     * @return OrderBookManagerStats current statistics
     */
    OrderBookManagerStats getStatistics() const;

    /**
     * @brief Get number of active order books
     * 
     * @return size_t number of order books
     */
    size_t getOrderBookCount() const;

private:
    /**
     * @brief Construct a new Order Book Reconstructor object (private for singleton)
     */
    OrderBookReconstructor();
     
    /**
     * @brief Publish an order book update via ZeroMQ
     * 
     * @param orderBook Order book to publish
     */
    void publishOrderBook(const std::shared_ptr<OrderBook>& orderBook);

    /**
     * @brief Convert milliseconds to Timestamp
     * 
     * @param millis Milliseconds since epoch
     * @return core::Timestamp
     */
    core::Timestamp timestampFromMillis(uint64_t millis) const;

    /**
     * @brief Convert Timestamp to milliseconds
     * 
     * @param timestamp Timestamp to convert
     * @return uint64_t milliseconds since epoch
     */
    uint64_t timestampToMillis(const core::Timestamp& timestamp) const;
     
    // Member variables
    bool initialized_;
    std::shared_ptr<TradingCommunicationServer> publisher_;
    std::shared_ptr<OrderBookManager> orderBookManager_;
    mutable std::mutex mutex_;
};
 
} // namespace data_ingestion
} // namespace trading