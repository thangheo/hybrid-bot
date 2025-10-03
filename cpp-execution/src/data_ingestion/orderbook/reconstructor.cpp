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
#include "../generated/cpp/internal_orderbook.pb.h"
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
    std::shared_ptr<trading::communication::GenericZmqServer> publisher,
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
    //cleanup old order books if too many
        
    static size_t updateCount = 0;
    if (++updateCount % 1000 == 0) { // Check mỗi 1000 updates
        orderBookManager_->limitOrderBooks(100); // Keep 100 newest
    }

    return success;
}

void OrderBookReconstructor::registerLiquidityFlowMonitor(
    std::shared_ptr<strategy::LiquidityFlowMonitor> monitor
) {
    // This would need to be implemented in the OrderBookManager
    // For now, we'll just log that the request was received
    // std::cout << "Liquidity flow monitor registration requested (not yet implemented)" << std::endl;
    //add liquidity flow monitor to order book manager
    if (orderBookManager_) {
        // create liquidity flow monitor map if not exists
        // orderBookManager_->registerLiquidityFlowMonitor(monitor);
        // createLiquidityFlowMonitor
        orderBookManager_->createLiquidityFlowMonitor(monitor->getExchangeId(), monitor->getTradingPair(), monitor);
    }
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
    // If publishing is disabled (e.g., main.cpp is the canonical publisher), skip to avoid duplicates
    if (!publishEnabled_) {
        return;
    }
    
    try {
        // Get the full order book snapshot
        OrderBookUpdate update = orderBook->getSnapshot();
    //we reduce the update frequency to 2 per second per symbol
    static std::unordered_map<std::string, core::Timestamp> lastPublishTime;
    auto now = std::chrono::system_clock::now();
    std::string key = orderBook->getExchangeId().toString() + ":" + orderBook->getSymbol().toString();
    if (lastPublishTime.find(key) != lastPublishTime.end()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPublishTime[key]).count();
        if (duration < 500) {
            return; // Skip publishing if less than 100 milliseconds since last publish
        }
    }
    lastPublishTime[key] = now;
    publishOrderBookUpdate(update);
    
    } catch (const std::exception& e) {
        std::cerr << "Error publishing order book: " << e.what() << std::endl;
    }
}
void OrderBookReconstructor::publishOrderBookUpdate(const OrderBookUpdate& update) {
    try {
        // Validate input
        if (update.bids.empty() && update.asks.empty()) {
            std::cerr << "Warning: Orderbook update is empty for "
                        << update.exchangeId.toString() << ":" << update.tradingPair.toString() << std::endl;
            return;
        }
        
        // Create protobuf message (this part is correct)
        orderbook::OrderbookMessage obMsg;
        auto* data = obMsg.mutable_data();
        data->set_exchange(update.exchangeId.toString());
        data->set_symbol(update.tradingPair.toString());
        data->set_sequence(update.sequenceNumber);
        data->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            update.exchangeTimestamp.time_since_epoch()).count());
        
        // Best bid/ask
        data->set_bid(!update.bids.empty() ? update.bids[0].price : 0.0);
        data->set_ask(!update.asks.empty() ? update.asks[0].price : 0.0);
        
        // Mid price
        data->set_price((!update.bids.empty() && !update.asks.empty())
            ? (update.bids[0].price + update.asks[0].price) / 2.0 : 0.0);
        
        // Volume at top 5 levels
        double bidVolume = 0.0, askVolume = 0.0;
        for (int i = 0; i < std::min(5, (int)update.bids.size()); ++i) bidVolume += update.bids[i].quantity;
        for (int i = 0; i < std::min(5, (int)update.asks.size()); ++i) askVolume += update.asks[i].quantity;
        data->set_volume(bidVolume + askVolume);
        
        // Add bids/asks
        for (const auto& bid : update.bids) {
            auto* level = data->add_bids();
            level->set_price(bid.price);
            level->set_quantity(bid.quantity);
        }
        for (const auto& ask : update.asks) {
            auto* level = data->add_asks();
            level->set_price(ask.price);
            level->set_quantity(ask.quantity);
        }
        
        // Placeholders for 24h stats and last trade
        data->set_lasttradeprice(data->price());
        data->set_lasttradequantity(0.0);
        data->set_open24h(data->price());
        data->set_high24h(data->price());
        data->set_low24h(data->price());
        data->set_volumequote24h(0.0);
        data->set_pricechange24h(0.0);
        data->set_pricechangepercent24h(0.0);
        
        // Populate metadata
        auto* metadata = obMsg.mutable_metadata();
        metadata->set_correlation_id(""); // Empty for now
        metadata->set_priority("HIGH");
        auto now = std::chrono::system_clock::now();
        metadata->set_timestamp(std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count()));
        
        // Serialize protobuf to string
        std::string serializedOb;
        if (!obMsg.SerializeToString(&serializedOb)) {
            std::cerr << "Failed to serialize OrderBook protobuf" << std::endl;
            return;
        }
        
        // CRITICAL FIX: Create proper topic WITHOUT embedding in protobuf
        std::string topic = "orderbook." + update.exchangeId.toString() + "." + update.tradingPair.toString();
        
        // CRITICAL FIX: Use publishAsync correctly - it should send topic + data as separate frames
        // The issue was likely that the topic was being embedded in the protobuf data itself
        
        // Validate before publishing
        if (topic.empty() || serializedOb.empty()) {
            std::cerr << "Empty topic or serialized data" << std::endl;
            return;
        }
        
        // Debug output for first few messages
        static std::atomic<int> publishCount{0};
        int count = publishCount.fetch_add(1);
        if (count < 10) {
            std::cout << "=== PUBLISH DEBUG #" << count << " ===" << std::endl;
            std::cout << "Topic: '" << topic << "'" << std::endl;
            std::cout << "Protobuf size: " << serializedOb.size() << " bytes" << std::endl;
            std::cout << "Protobuf starts with: ";
            for (size_t i = 0; i < std::min<size_t>(16, serializedOb.size()); ++i) {
                printf("%02x ", static_cast<unsigned char>(serializedOb[i]));
            }
            std::cout << std::endl;
            
            // Check if topic is accidentally embedded in protobuf
            if (serializedOb.find(topic) != std::string::npos) {
                std::cout << "ERROR: Topic found embedded in protobuf data!" << std::endl;
            } else {
                std::cout << "Good: Topic not embedded in protobuf" << std::endl;
            }
            std::cout << "===============================" << std::endl;
        }
        
        // IMPORTANT: Use internal queue (not intel) for orderbook data to internal consumers
        bool success = publisher_->publishAsync(topic, std::move(serializedOb), MessagePriority::HIGH);
        //use publishSync to ensure message is sent before we move on                
        
        
    } catch (const std::exception& e) {
        std::cerr << "Error publishing orderbook for risk: " << e.what() << std::endl;
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