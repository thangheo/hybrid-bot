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

using namespace trading::core;
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
 void OrderBookReconstructor::UpdateOrderBookFromJson(ExchangeId exchange_id, const nlohmann::json& data, const Symbol& symbol) {
    try {
        static std::atomic<int> debug_updates{0};
        bool is_debug = debug_updates.fetch_add(1) < 3;
        
        // Get or CREATE order book
        std::shared_ptr<OrderBook> orderbook;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            OrderBookKey key{exchange_id, symbol};
            auto it = order_books_.find(key);
            
            if (it == order_books_.end()) {
                // Create new order book if it doesn't exist
                orderbook = std::make_shared<OrderBook>(exchange_id, symbol);
                order_books_[key] = orderbook;
                
                if (is_debug) {
                    std::cout << "Created new order book for " << symbol.toString() 
                             << " on exchange " << exchange_id.toString() << std::endl;
                }
            } else {
                orderbook = it->second;
            }
        }
        
        if (!orderbook) {
            std::cerr << "Error: Failed to create/get OrderBook for " << symbol.toString() 
                     << " on exchange " << exchange_id.toString() << std::endl;
            return;
        }
        
        if (is_debug) {
            std::cout << "\n=== DEBUG: Processing depthUpdate ===" << std::endl;
            std::cout << "Symbol: " << symbol.toString() << std::endl;
            std::cout << "Exchange: " << exchange_id.toString() << std::endl;
            std::cout << "Full message: " << data.dump().substr(0, 500) << std::endl;
        }
        
        // Collect ALL bid and ask updates in vectors
        std::vector<trading::data_ingestion::PriceLevel> bid_updates;
        std::vector<trading::data_ingestion::PriceLevel> ask_updates;
        
        // Process ALL bids into vector
        if (data.contains("b") && data["b"].is_array()) {
            if (is_debug) {
                std::cout << "Found bids array with " << data["b"].size() << " entries" << std::endl;
            }
            
            for (const auto& bid : data["b"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    try {
                        std::string priceStr = bid[0].get<std::string>();
                        std::string qtyStr = bid[1].get<std::string>();
                        
                        double price = std::stod(priceStr);
                        double quantity = std::stod(qtyStr);
                        
                        if (is_debug) {
                            std::cout << "Processing bid: " << priceStr << " @ " << qtyStr 
                                    << " (parsed: " << price << " @ " << quantity << ")" << std::endl;
                        }
                        
                        if (price > 0) {
                            bid_updates.emplace_back(price, quantity);
                            
                            if (is_debug) {
                                if (quantity == 0.0) {
                                    std::cout << "  Will remove bid at " << price << std::endl;
                                } else {
                                    std::cout << "  Will update bid: " << price << " @ " << quantity << std::endl;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        if (is_debug) std::cout << "  Error parsing bid: " << e.what() << std::endl;
                    }
                }
            }
        }
        
        // Process ALL asks into vector
        if (data.contains("a") && data["a"].is_array()) {
            if (is_debug) {
                std::cout << "Found asks array with " << data["a"].size() << " entries" << std::endl;
            }
            
            for (const auto& ask : data["a"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    try {
                        std::string priceStr = ask[0].get<std::string>();
                        std::string qtyStr = ask[1].get<std::string>();
                        
                        double price = std::stod(priceStr);
                        double quantity = std::stod(qtyStr);
                        
                        if (is_debug) {
                            std::cout << "Processing ask: " << priceStr << " @ " << qtyStr 
                                    << " (parsed: " << price << " @ " << quantity << ")" << std::endl;
                        }
                        
                        if (price > 0) {
                            ask_updates.emplace_back(price, quantity);
                            
                            if (is_debug) {
                                if (quantity == 0.0) {
                                    std::cout << "  Will remove ask at " << price << std::endl;
                                } else {
                                    std::cout << "  Will update ask: " << price << " @ " << quantity << std::endl;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        if (is_debug) std::cout << "  Error parsing ask: " << e.what() << std::endl;
                    }
                }
            }
        }
        
        // Apply ALL updates at once using proper OrderBook method
        if (!bid_updates.empty() || !ask_updates.empty()) {
            // Get sequence number from message if available
            uint64_t sequence_num = 0;
            if (data.contains("u")) {
                sequence_num = data["u"].get<uint64_t>();
            }
            
            // Apply the update
            bool success = orderbook->applyUpdate(
                bid_updates, 
                ask_updates, 
                sequence_num, 
                std::chrono::system_clock::now()
            );
            
            if (is_debug) {
                std::cout << "Applied update with " << bid_updates.size() 
                        << " bid changes and " << ask_updates.size() << " ask changes" << std::endl;
                std::cout << "Update success: " << (success ? "YES" : "NO") << std::endl;
            }
            
            if (success) {
                // Verify order book state after update
                auto bids = orderbook->getBids(5);
                auto asks = orderbook->getAsks(5);
                
                if (is_debug) {
                    std::cout << "OrderBook state after update:" << std::endl;
                    std::cout << "  Bids count: " << bids.size() << std::endl;
                    std::cout << "  Asks count: " << asks.size() << std::endl;
                    
                    if (!bids.empty()) {
                        std::cout << "  Best bid: " << bids[0].price << " @ " << bids[0].quantity << std::endl;
                    }
                    if (!asks.empty()) {
                        std::cout << "  Best ask: " << asks[0].price << " @ " << asks[0].quantity << std::endl;
                    }
                }
                
                // Publish the updated order book
                publishOrderBook(orderbook);
                
            } else {
                if (is_debug) std::cout << "OrderBook update failed - possibly due to sequence gap" << std::endl;
                
                // On failure, request a snapshot to resync
                requestSnapshot(exchange_id, symbol);
            }
        } else {
            if (is_debug) std::cout << "No price level updates to apply" << std::endl;
        }
        
        if (is_debug) {
            std::cout << "==================================" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Error processing order book update for " << symbol.toString() 
                 << ": " << e.what() << std::endl;
    }
}
 bool OrderBookReconstructor::processSnapshot(
     ExchangeId exchange, 
     const Symbol& symbol,
     const std::vector<PriceLevel>& bids,
     const std::vector<PriceLevel>& asks,
     uint64_t sequenceNumber,
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
     if (!order_book->applySnapshot(bids, asks, sequenceNumber, timestamp)) {
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
    uint64_t sequenceNumber,
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
    if (!order_book->applyUpdate(bids, asks, sequenceNumber, timestamp)) {
        std::cerr << "Error: Failed to apply update to order book" << std::endl;
        
        // If there's a sequence gap, request a snapshot
        if (order_book->getSequenceNumber() + 1 < sequenceNumber) {
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
         if (key.exchangeId == exchange) {
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
        std::to_string(update.sequenceNumber) + ":" +
        std::to_string(timestampToMillis(update.exchangeTimestamp)) + ":" +
        std::to_string(timestampToMillis(update.receivedTimestamp));
    std::vector<unsigned char> binary_payload(payload.begin(), payload.end());
    // Serialize the order book data
    // This is a placeholder for actual serialization logic

    // Publish message
    publisher_->publish("orderbook", binary_payload);

 }
 
 } // namespace data_ingestion
} // namespace trading