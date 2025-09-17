/**
 * @file reconstructor.cpp
 * @brief Implementation of order book reconstruction logic
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * Refactored to use OrderBookManager for proper separation of concerns.
 * 
 * Created on: April 30, 2025
 * Refactored on: September 11, 2025
 */

#include "data_ingestion/orderbook/reconstructor.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace trading {
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

bool OrderBookReconstructor::initialize(
    std::shared_ptr<trading::communication::TradingCommunicationServer> publisher,
    std::shared_ptr<OrderBookManager> manager
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true; // Already initialized
    }
    
    if (!publisher) {
        std::cerr << "Error: Null publisher provided to OrderBookReconstructor" << std::endl;
        return false;
    }
    
    if (!manager) {
        std::cerr << "Error: Null order book manager provided to OrderBookReconstructor" << std::endl;
        return false;
    }
    
    publisher_ = publisher;
    orderBookManager_ = manager;
    initialized_ = true;
    
    std::cout << "OrderBookReconstructor initialized successfully" << std::endl;
    return true;
}

void OrderBookReconstructor::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    // Release resources
    publisher_.reset();
    orderBookManager_.reset();
    
    initialized_ = false;
    std::cout << "OrderBookReconstructor shutdown completed" << std::endl;
}

void OrderBookReconstructor::updateOrderBookFromJson(
    ExchangeId exchangeId, 
    const nlohmann::json& data, 
    const core::Symbol& symbol
) {
    try {
        static std::atomic<int> debugUpdates{0};
        // bool isDebug = debugUpdates.fetch_add(1) < 3;
        bool isDebug = false;
        
        if (!initialized_ || !orderBookManager_) {
            std::cerr << "Error: OrderBookReconstructor not initialized" << std::endl;
            return;
        }
        
        if (isDebug) {
            std::cout << "\n=== DEBUG: Processing depthUpdate ===" << std::endl;
            std::cout << "Symbol: " << symbol.toString() << std::endl;
            std::cout << "Exchange: " << exchangeId.toString() << std::endl;
            std::cout << "Full message: " << data.dump().substr(0, 500) << std::endl;
        }
        
        // Collect ALL bid and ask updates in vectors
        std::vector<PriceLevel> bidUpdates;
        std::vector<PriceLevel> askUpdates;
        
        // Process ALL bids into vector
        if (data.contains("b") && data["b"].is_array()) {
            if (isDebug) {
                std::cout << "Found bids array with " << data["b"].size() << " entries" << std::endl;
            }
            
            for (const auto& bid : data["b"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    try {
                        std::string priceStr = bid[0].get<std::string>();
                        std::string qtyStr = bid[1].get<std::string>();
                        
                        double price = std::stod(priceStr);
                        double quantity = std::stod(qtyStr);
                        
                        if (isDebug) {
                            std::cout << "Processing bid: " << priceStr << " @ " << qtyStr 
                                    << " (parsed: " << price << " @ " << quantity << ")" << std::endl;
                        }
                        
                        if (price > 0) {
                            bidUpdates.emplace_back(price, quantity);
                            
                            if (isDebug) {
                                if (quantity == 0.0) {
                                    std::cout << "  Will remove bid at " << price << std::endl;
                                } else {
                                    std::cout << "  Will update bid: " << price << " @ " << quantity << std::endl;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        if (isDebug) std::cout << "  Error parsing bid: " << e.what() << std::endl;
                    }
                }
            }
        }
        
        // Process ALL asks into vector
        if (data.contains("a") && data["a"].is_array()) {
            if (isDebug) {
                std::cout << "Found asks array with " << data["a"].size() << " entries" << std::endl;
            }
            
            for (const auto& ask : data["a"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    try {
                        std::string priceStr = ask[0].get<std::string>();
                        std::string qtyStr = ask[1].get<std::string>();
                        
                        double price = std::stod(priceStr);
                        double quantity = std::stod(qtyStr);
                        
                        if (isDebug) {
                            std::cout << "Processing ask: " << priceStr << " @ " << qtyStr 
                                    << " (parsed: " << price << " @ " << quantity << ")" << std::endl;
                        }
                        
                        if (price > 0) {
                            askUpdates.emplace_back(price, quantity);
                            
                            if (isDebug) {
                                if (quantity == 0.0) {
                                    std::cout << "  Will remove ask at " << price << std::endl;
                                } else {
                                    std::cout << "  Will update ask: " << price << " @ " << quantity << std::endl;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        if (isDebug) std::cout << "  Error parsing ask: " << e.what() << std::endl;
                    }
                }
            }
        }
        
        // Create OrderBookUpdate and delegate to manager
        if (!bidUpdates.empty() || !askUpdates.empty()) {
            // Get sequence number from message if available
            uint64_t sequenceNum = 0;
            if (data.contains("u")) {
                sequenceNum = data["u"].get<uint64_t>();
            }
            
            // Create OrderBookUpdate structure
            OrderBookUpdate update;
            update.exchangeId = exchangeId;
            update.tradingPair = symbol;  // Set tradingPair field
            update.bids = bidUpdates;
            update.asks = askUpdates;
            update.sequenceNumber = sequenceNum;
            update.exchangeTimestamp = std::chrono::system_clock::now();
            update.receivedTimestamp = std::chrono::system_clock::now();
            update.isSnapshot = false; // This is an update, not a snapshot
            
            // Delegate to order book manager
            bool success = orderBookManager_->processUpdate(update);
            
            if (isDebug) {
                std::cout << "Applied update with " << bidUpdates.size() 
                        << " bid changes and " << askUpdates.size() << " ask changes" << std::endl;
                // std::cout << "Update success: " << (success ? "YES" : "NO") << std::endl;
            }
            
            if (success) {
                // Get the updated order book from manager and publish
                auto orderBook = orderBookManager_->getOrderBook(exchangeId, symbol);
                if (orderBook) {
                    // Verify order book state after update
                    auto bids = orderBook->getBids(5);
                    auto asks = orderBook->getAsks(5);
                    
                    if (isDebug) {
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
                    publishOrderBook(orderBook);
                }
            } else {
                if (isDebug) std::cout << "OrderBook update failed - possibly due to sequence gap" << std::endl;
                
                // On failure, request a snapshot to resync
                requestSnapshot(exchangeId, symbol);
            }
        } else {
            if (isDebug) std::cout << "No price level updates to apply" << std::endl;
        }
        
        if (isDebug) {
            std::cout << "==================================" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Error processing order book update for " << symbol.toString() 
                 << ": " << e.what() << std::endl;
    }
}

bool OrderBookReconstructor::processSnapshot(
    ExchangeId exchange, 
    const core::Symbol& symbol,
    const std::vector<PriceLevel>& bids,
    const std::vector<PriceLevel>& asks,
    uint64_t sequenceNumber,
    core::Timestamp timestamp
) {
    if (!initialized_ || !orderBookManager_) {
        std::cerr << "Error: OrderBookReconstructor not initialized" << std::endl;
        return false;
    }
    
    // Create OrderBookUpdate structure
    OrderBookUpdate update;
    update.exchangeId = exchange;
    update.tradingPair = symbol;  // Set tradingPair field
    update.bids = bids;
    update.asks = asks;
    update.sequenceNumber = sequenceNumber;
    update.exchangeTimestamp = timestamp;
    update.receivedTimestamp = std::chrono::system_clock::now();
    update.isSnapshot = true;
    
    // Delegate to order book manager (will auto-create if needed)
    bool success = orderBookManager_->processUpdate(update);
    
    if (success) {
        // Get the order book from manager and publish
        auto orderBook = orderBookManager_->getOrderBook(exchange, symbol);
        if (orderBook) {
            publishOrderBook(orderBook);
        }
    }
    
    return success;
}

bool OrderBookReconstructor::processUpdate(
    ExchangeId exchange,
    const core::Symbol& symbol,
    const std::vector<PriceLevel>& bids,
    const std::vector<PriceLevel>& asks,
    uint64_t sequenceNumber,
    core::Timestamp timestamp
) {
    if (!initialized_ || !orderBookManager_) {
        std::cerr << "Error: OrderBookReconstructor not initialized" << std::endl;
        return false;
    }
    
    // Create OrderBookUpdate structure
    OrderBookUpdate update;
    update.exchangeId = exchange;
    update.tradingPair = symbol;  // Set tradingPair field
    update.bids = bids;
    update.asks = asks;
    update.sequenceNumber = sequenceNumber;
    update.exchangeTimestamp = timestamp;
    update.receivedTimestamp = std::chrono::system_clock::now();
    update.isSnapshot = false;
    // std::cout << "Processing incremental update for " << symbol.toString() 
    //          << " with " << bids.size() << " bid changes and " << asks.size() << " ask changes" << std::endl;
    // Delegate to order book manager
    bool success = orderBookManager_->processUpdate(update);
    if (success) {
        // Get the updated order book from manager and publish
        auto orderBook = orderBookManager_->getOrderBook(exchange, symbol);
        if (orderBook) {
            publishOrderBook(orderBook);
        }
    } else {
        // If there's a sequence gap, request a snapshot
        auto orderBook = orderBookManager_->getOrderBook(exchange, symbol);
        if (orderBook && orderBook->getSequenceNumber() + 1 < sequenceNumber) {
            requestSnapshot(exchange, symbol);
        }
    }
    
    return success;
}

void OrderBookReconstructor::registerLiquidityFlowMonitor(
    std::shared_ptr<strategy::LiquidityFlowMonitor> monitor
) {
    // This would need to be implemented in the OrderBookManager
    // For now, we'll just log that the request was received
    std::cout << "Liquidity flow monitor registration requested (not yet implemented)" << std::endl;
}

std::shared_ptr<OrderBook> OrderBookReconstructor::getOrderBook(
    ExchangeId exchange, 
    const core::Symbol& symbol
) const {
    if (!orderBookManager_) {
        return nullptr;
    }
    
    return orderBookManager_->getOrderBook(exchange, symbol);
}

std::vector<core::Symbol> OrderBookReconstructor::getOrderBooksForExchange(ExchangeId exchange) const {
    if (!orderBookManager_) {
        return {};
    }
    
    return orderBookManager_->getOrderBooksForExchange(exchange);
}

bool OrderBookReconstructor::hasOrderBook(ExchangeId exchange, const core::Symbol& symbol) const {
    if (!orderBookManager_) {
        return false;
    }
    
    return orderBookManager_->hasOrderBook(exchange, symbol);
}

void OrderBookReconstructor::removeOrderBook(ExchangeId exchange, const core::Symbol& symbol) {
    if (!orderBookManager_) {
        return;
    }
    
    orderBookManager_->removeOrderBook(exchange, symbol);
}

void OrderBookReconstructor::clearOrderBooks() {
    if (!orderBookManager_) {
        return;
    }
    
    orderBookManager_->cleanup();
}

bool OrderBookReconstructor::validateOrderBook(ExchangeId exchange, const core::Symbol& symbol) {
    auto orderBook = getOrderBook(exchange, symbol);
    if (!orderBook) {
        return false;
    }
    
    return orderBook->isValid();
}

bool OrderBookReconstructor::requestSnapshot(ExchangeId exchange, const core::Symbol& symbol) {
    // In a real implementation, this would send a request to the exchange API
    // For now, we'll just log the request
    std::cout << "Requesting snapshot for "
              << exchange.toString() << ":"
              << symbol.base << "/" << symbol.quote << std::endl;
    
    // Return true to indicate request was sent
    return true;
}

OrderBookManagerStats OrderBookReconstructor::getStatistics() const {
    if (!orderBookManager_) {
        return OrderBookManagerStats{};
    }
    
    return orderBookManager_->getStatistics();
}

size_t OrderBookReconstructor::getOrderBookCount() const {
    if (!orderBookManager_) {
        return 0;
    }
    
    return orderBookManager_->getOrderBookCount();
}

void OrderBookReconstructor::publishOrderBook(const std::shared_ptr<OrderBook>& orderBook) {
    if (!publisher_ || !orderBook) {
        return;
    }
    
    try {
        // Get the full order book snapshot
        OrderBookUpdate update = orderBook->getSnapshot();
        
        // Create a proper message with order book data
        nlohmann::json message;
        
        // Header information
        message["exchangeId"] = update.exchangeId.toString();
        message["tradingPair"] = update.tradingPair.toString();
        message["sequenceNumber"] = update.sequenceNumber;
        message["exchangeTimestamp"] = timestampToMillis(update.exchangeTimestamp);
        message["receivedTimestamp"] = timestampToMillis(update.receivedTimestamp);
        message["isSnapshot"] = update.isSnapshot;
        
        // Bids data
        message["bids"] = nlohmann::json::array();
        for (const auto& bid : update.bids) {
            message["bids"].push_back({
                {"price", bid.price},
                {"quantity", bid.quantity}
            });
        }
        
        // Asks data  
        message["asks"] = nlohmann::json::array();
        for (const auto& ask : update.asks) {
            message["asks"].push_back({
                {"price", ask.price},
                {"quantity", ask.quantity}
            });
        }
        
        // Convert to binary payload
        std::string jsonStr = message.dump();
        std::vector<unsigned char> binaryPayload(jsonStr.begin(), jsonStr.end());
        
        // Publish with proper topic
        std::string topic = "orderbook." + update.exchangeId.toString() + "." + update.tradingPair.toString();
        // publisher_->PublishOrderBookData(topic, binaryPayload);
        publisher_->PublishRawMessage(topic, jsonStr);
        
    } catch (const std::exception& e) {
        std::cerr << "Error publishing order book: " << e.what() << std::endl;
    }
}

core::Timestamp OrderBookReconstructor::timestampFromMillis(uint64_t millis) const {
    return core::Timestamp{std::chrono::milliseconds{millis}};
}

uint64_t OrderBookReconstructor::timestampToMillis(const core::Timestamp& timestamp) const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()
    ).count();
}

} // namespace data_ingestion
} // namespace trading