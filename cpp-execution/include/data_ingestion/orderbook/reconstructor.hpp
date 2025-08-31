/**
 * @file reconstructor.hpp
 * @brief Header for order book reconstruction logic
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "strategy/core_analysis/liquidity_flow_monitor.hpp"
// #include "data_ingestion/orderbook/level.hpp"
#include "config.hpp"
namespace trading {
namespace data_ingestion {

 /**
  * @brief Singleton class for reconstructing order books from market data
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
      * @brief Initialize the reconstructor with a publisher
      * 
      * @param publisher Shared pointer to a publisher
      * @return true if successful, false otherwise
      */
    //  bool initialize(std::shared_ptr<publisher::Publisher> publisher);
    // bool initialize(std::shared_ptr<trading::data_ingestion::Publisher> publisher);
    bool initialize(std::shared_ptr<trading::data_ingestion::ZeroMQPublisher> publisher);
     
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
         const Symbol& symbol,
         const std::vector<PriceLevel>& bids,
         const std::vector<PriceLevel>& asks,
         uint64_t sequenceNumber,
         Timestamp timestamp);
     
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
         const Symbol& symbol,
         const std::vector<PriceLevel>& bids,
         const std::vector<PriceLevel>& asks,
         uint64_t sequenceNumber,
         Timestamp timestamp);
     //registerLiquidityFlowMonitor
     void registerLiquidityFlowMonitor(
        std::shared_ptr<strategy::LiquidityFlowMonitor> monitor) ;
    
     /**
      * @brief Get an order book
      * 
      * @param exchange Exchange identifier
      * @param symbol Trading symbol
      * @return std::shared_ptr<OrderBook> 
      */
        std::shared_ptr<OrderBook> getOrderBook(
            ExchangeId exchange, const Symbol& symbol) const;

     /**
      * @brief Get all order books
      * 
      * @return std::vector<std::shared_ptr<OrderBook>> 
      */
     std::vector<std::shared_ptr<OrderBook>> getAllOrderBooks() const;
     
     /**
      * @brief Get all order books for a specific exchange
      * 
      * @param exchange Exchange identifier
      * @return std::vector<std::shared_ptr<OrderBook>> 
      */
     std::vector<std::shared_ptr<OrderBook>> getOrderBooks(
         ExchangeId exchange) const;
     
     /**
      * @brief Check if an order book exists
      * 
      * @param exchange Exchange identifier
      * @param symbol Trading symbol
      * @return true if exists, false otherwise
      */
     bool hasOrderBook(ExchangeId exchange, const Symbol& symbol) const;
     
     /**
      * @brief Remove an order book
      * 
      * @param exchange Exchange identifier
      * @param symbol Trading symbol
      */
     void removeOrderBook(ExchangeId exchange, const Symbol& symbol);
     
     /**
      * @brief Clear all order books
      */
     void clearOrderBooks();
     
     /**
      * @brief Validate an order book
      * 
      * @param exchange Exchange identifier
      * @param symbol Trading symbol
      * @return true if valid, false otherwise
      */
     bool validateOrderBook(ExchangeId exchange, const Symbol& symbol);
     
     /**
      * @brief Request a snapshot from an exchange
      * 
      * @param exchange Exchange identifier
      * @param symbol Trading symbol
      * @return true if request sent, false otherwise
      */
     bool requestSnapshot(ExchangeId exchange, const Symbol& symbol);
    
 private:
     /**
      * @brief Construct a new Order Book Reconstructor object (private for singleton)
      */
     OrderBookReconstructor();
     
     /**
      * @brief Publish an order book update
      * 
      * @param order_book Order book to publish
      */
     void publishOrderBook(const std::shared_ptr<OrderBook>& order_book);
     
     // Member variables
     bool initialized_;
    //  std::shared_ptr<publisher::Publisher> publisher_;
    std::shared_ptr<trading::data_ingestion::ZeroMQPublisher> publisher_;

     std::unordered_map<OrderBookKey, std::shared_ptr<OrderBook>, OrderBookKeyHash> order_books_;
     mutable std::mutex mutex_;
 };
 
}  // namespace data_ingestion
}  // namespace trading
