/**
 * @file reconstructor.cpp
 * @brief Implementation of order book reconstruction logic
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */

#include "data_ingestion/orderbook/reconstructor.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
 namespace trading{
 namespace data_ingestion {

 // Singleton instance
 OrderBookReconstructor& OrderBookReconstructor::getInstance() {
     static OrderBookReconstructor instance;
     return instance;
 }
 
 OrderBookReconstructor::OrderBookReconstructor()
     : initialized_(false) {
 }
 
 OrderBookReconstructor::~OrderBookReconstructor() {
     shutdown();
 }
 
 bool OrderBookReconstructor::initialize(std::shared_ptr<trading::data_ingestion::ZeroMQPublisher> publisher) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (initialized_) {
         return true; // Already initialized
     }
     
     publisher_ = publisher;
     if (!publisher_) {
         std::cerr << "Error: Null publisher provided to OrderBookReconstructor" << std::endl;
         return false;
     }
     
     initialized_ = true;
     return true;
 }
 
 void OrderBookReconstructor::shutdown() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (!initialized_) {
         return;
     }
     
     // Clear all order books
     order_books_.clear();
     
     // Release publisher
     publisher_.reset();
     
     initialized_ = false;
 }
 
 bool OrderBookReconstructor::processSnapshot(
     ExchangeId exchange, 
     const Symbol& symbol,
     const std::vector<PriceLevel>& bids,
     const std::vector<PriceLevel>& asks,
     uint64_t sequence_number,
     Timestamp timestamp) {
     
     if (!initialized_) {
         std::cerr << "Error: OrderBookReconstructor not initialized" << std::endl;
         return false;
     }
     
     // Get or create order book
     std::shared_ptr<OrderBook> order_book;
     {
         std::lock_guard<std::mutex> lock(mutex_);
         
         OrderBookKey key{exchange, symbol};
         auto it = order_books_.find(key);
         
         if (it == order_books_.end()) {
             // Create new order book
             order_book = std::make_shared<OrderBook>(exchange, symbol);
             order_books_[key] = order_book;
         } else {
             order_book = it->second;
         }
     }
     
     // Apply snapshot to order book
     if (!order_book->applySnapshot(bids, asks, sequence_number, timestamp)) {
         std::cerr << "Error: Failed to apply snapshot to order book" << std::endl;
         return false;
     }
     
     // Publish updated order book
     publishOrderBook(order_book);
     
     return true;
 }
 // In reconstructor.cpp

bool OrderBookReconstructor::processUpdate(
    ExchangeId exchange,
    const Symbol& symbol,
    const std::vector<PriceLevel>& bids,
    const std::vector<PriceLevel>& asks,
    uint64_t sequence_number,
    Timestamp timestamp) {
    
    if (!initialized_) {
        std::cerr << "Error: OrderBookReconstructor not initialized" << std::endl;
        return false;
    }
    
    // Find order book
    std::shared_ptr<OrderBook> order_book;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        OrderBookKey key{exchange, symbol};
        auto it = order_books_.find(key);
        
        if (it == order_books_.end()) {
            // Order book not found, cannot apply update
            std::cerr << "Error: Order book not found for update" << std::endl;
            return false;
        }
        
        order_book = it->second;
    }
    
    // Apply update to order book
    if (!order_book->applyUpdate(bids, asks, sequence_number, timestamp)) {
        std::cerr << "Error: Failed to apply update to order book" << std::endl;
        
        // If there's a sequence gap, request a snapshot
        if (order_book->getSequenceNumber() + 1 < sequence_number) {
            requestSnapshot(exchange, symbol);
        }
        
        return false;
    }
    
    // Publish updated order book
    publishOrderBook(order_book);
    
    return true;
}

void OrderBookReconstructor::registerLiquidityFlowMonitor(
    std::shared_ptr<strategy::LiquidityFlowMonitor> monitor) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Register the monitor with all order books
    for (auto& [key, order_book] : order_books_) {
        order_book->setDeltaCallback([monitor](const OrderBookDelta& delta) {
            monitor->processOrderBookDelta(delta);
        });
    }
}
 std::shared_ptr<OrderBook> OrderBookReconstructor::getOrderBook(
     ExchangeId exchange, const Symbol& symbol) const {
     
     std::lock_guard<std::mutex> lock(mutex_);
     
     OrderBookKey key{exchange, symbol};
     auto it = order_books_.find(key);
     
     if (it == order_books_.end()) {
         return nullptr;
     }
     
     return it->second;
 }
 
 std::vector<std::shared_ptr<OrderBook>> OrderBookReconstructor::getAllOrderBooks() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<std::shared_ptr<OrderBook>> result;
     result.reserve(order_books_.size());
     
     for (const auto& [key, order_book] : order_books_) {
         result.push_back(order_book);
     }
     
     return result;
 }
 
 std::vector<std::shared_ptr<OrderBook>> OrderBookReconstructor::getOrderBooks(
     ExchangeId exchange) const {
     
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<std::shared_ptr<OrderBook>> result;
     
     for (const auto& [key, order_book] : order_books_) {
         if (key.exchange == exchange) {
             result.push_back(order_book);
         }
     }
     
     return result;
 }
 
 bool OrderBookReconstructor::hasOrderBook(ExchangeId exchange, const Symbol& symbol) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     OrderBookKey key{exchange, symbol};
     return order_books_.find(key) != order_books_.end();
 }
 
 void OrderBookReconstructor::removeOrderBook(ExchangeId exchange, const Symbol& symbol) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     OrderBookKey key{exchange, symbol};
     order_books_.erase(key);
 }
 
 void OrderBookReconstructor::clearOrderBooks() {
     std::lock_guard<std::mutex> lock(mutex_);
     order_books_.clear();
 }
 
 bool OrderBookReconstructor::validateOrderBook(ExchangeId exchange, const Symbol& symbol) {
     auto order_book = getOrderBook(exchange, symbol);
     if (!order_book) {
         return false;
     }
     
     return order_book->isValid();
 }
 bool OrderBookReconstructor::requestSnapshot(ExchangeId exchange, const Symbol& symbol) {
    // In a real implementation, this would send a request to the exchange API
    // For now, we'll just log the request
    std::cout << "Requesting snapshot for "
              << exchange.toString() << ":" // Using string directly without casting
              << symbol.base << "/" << symbol.quote << std::endl;
    
    // Return true to indicate request was sent
    return true;
}
 
void OrderBookReconstructor::publishOrderBook(const std::shared_ptr<OrderBook>& order_book) {
     if (!publisher_ || !order_book) {
         return;
     }
     

    OrderBookUpdate update = order_book->getSnapshot();
    std::string payload = update.exchangeId.toString() + ":" + update.tradingPair + ":" +
        std::to_string(update.sequence_number) + ":" +
        std::to_string(update.exchange_timestamp) + ":" +
        std::to_string(update.receivedTimestamp);
    std::vector<unsigned char> binary_payload(payload.begin(), payload.end());
    // Serialize the order book data
    // This is a placeholder for actual serialization logic

    // Publish message
    publisher_->publish("orderbook", binary_payload);



 }
 
 } // namespace data_ingestion
} // namespace trading