// tests/data_ingestion/test_order_book.cpp
#include "data_ingestion/orderbook/order_book.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/orderbook/price_level.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <chrono>

using namespace trading::data_ingestion;

// Helper function to create price levels
// std::vector<PriceLevel> createPriceLevels(const std::vector<std::pair<std::string, std::string>>& prices_quantities) {
//     std::vector<PriceLevel> levels;
//     for (const auto& [price, quantity] : prices_quantities) {
//         PriceLevel level;
//         level.setPrice(std::stod(price));
//         level.setQuantity(std::stod(quantity));
//         levels.push_back(level);
//     }
//     return levels;
// }

// Helper function to create price levels
std::vector<PriceLevel> createPriceLevels(const std::vector<std::pair<double, double>>& prices_quantities) {
    std::vector<PriceLevel> levels;
    for (const auto& [price, quantity] : prices_quantities) {
        PriceLevel level;
        level.setPrice(price);
        level.setQuantity(quantity);
        levels.push_back(level);
    }
    return levels;
}

// Helper function to get current timestamp
Timestamp getCurrentTimestamp() {
    return std::chrono::system_clock::now();
}

// Test basic order book functionality
void testOrderBookBasic() {
    // Create OrderBook with string constructor
    OrderBook order_book(ExchangeId::binance(), Symbol::fromString("btcusdt"));

    // Create initial snapshot
    auto bids = createPriceLevels({
        {10100.00, 1.5},
        {10099.00, 2.3},
        {10098.00, 3.1}
    });
    
    auto asks = createPriceLevels({
        {10101.00, 1.2},
        {10102.00, 2.4},
        {10103.00, 3.0}
    });
    
    // Apply snapshot
    Timestamp timestamp = getCurrentTimestamp();
    bool result = order_book.applySnapshot(bids, asks, 1, timestamp);
    assert(result == true);
    assert(order_book.isValid() == true);
    
    // Get order book snapshot
    OrderBookUpdate snapshot = order_book.getSnapshot();
    
    // Check values
    std::vector<PriceLevel> bids_result = snapshot.bids;
    std::vector<PriceLevel> asks_result = snapshot.asks;
    
    assert(bids_result.size() == 3);
    assert(asks_result.size() == 3);
    
    assert(bids_result[0].price == 10100.00);
    assert(bids_result[0].quantity == 1.5);
    assert(asks_result[0].price == 10101.00);
    assert(asks_result[0].quantity == 1.2);
    
    std::cout << "Basic order book test passed" << std::endl;
}

// Test incremental updates
void testOrderBookUpdates() {
    OrderBook order_book(ExchangeId::binance(), Symbol::fromString("btcusdt"));
    
    // Create initial snapshot
    auto bids = createPriceLevels({
        {10100.00, 1.5},
        {10099.00, 2.3},
        {10098.00, 3.1}
    });
    
    auto asks = createPriceLevels({
        {10101.00, 1.2},
        {10102.00, 2.4},
        {10103.00, 3.0}
    });
    
    // Apply snapshot
    Timestamp timestamp = getCurrentTimestamp();
    bool result = order_book.applySnapshot(bids, asks, 1, timestamp);
    assert(result == true);
    
    // Process an update - add new level, update existing, and remove by setting quantity to 0
    auto bids_update = createPriceLevels({
        {10100.00, 2.0},  // Update existing level
        {10101.00, 0.5},  // Add new level at top
        {10098.00, 0.0}   // Remove level by setting quantity to 0
    });
    
    auto asks_update = createPriceLevels({
        {10101.00, 0.5},  // Update existing level
        {10100.50, 1.0},  // Add new level at top
        {10103.00, 0.0}   // Remove level by setting quantity to 0
    });
    
    // Apply update
    timestamp = getCurrentTimestamp();
    result = order_book.applyUpdate(bids_update, asks_update, 2, timestamp);
    assert(result == true);
    
    // Get updated order book
    OrderBookUpdate snapshot = order_book.getSnapshot();
    std::vector<PriceLevel> bids_result = snapshot.bids;
    std::vector<PriceLevel> asks_result = snapshot.asks;
    
    // Should have appropriate number of levels for each side (after removing one level)
    assert(bids_result.size() == 3); // 2 from before + 1 new - 1 removed = 2
    assert(asks_result.size() == 3); // 2 from before + 1 new - 1 removed = 2
    
    // Check best bid/ask
    PriceLevel bestBid = order_book.getBestBid();
    PriceLevel bestAsk = order_book.getBestAsk();
    
    assert(bestBid.price == 10101.00);
    assert(bestBid.quantity == 0.5);
    assert(bestAsk.price == 10100.50);
    assert(bestAsk.quantity == 1.0);
    
    std::cout << "Order book updates test passed" << std::endl;
}



// Test order book metrics
void testOrderBookMetrics() {
    OrderBookCallback callback = [](const OrderBook& book) {
        // Verify the book is valid
        assert(book.isValid());
        // Check some property to make sure we got the right book
        assert(book.getSymbol() == Symbol::fromString("BTC/USDT"));
        assert(book.getSymbol().base == "btc");
        assert(book.getSymbol().toString() == "BTC/USDT" || book.getSymbol().toString() == "btcusdt");
        assert(book.getExchangeId() == ExchangeId::binance());
        assert(book.getSequenceNumber() == 1);
    };
    OrderBook order_book(ExchangeId::binance(), Symbol::fromString("btcusdt"), callback);
    // order_book.setCallback(callback);

    // Create initial snapshot
    auto bids = createPriceLevels({
        {10100.00, 1.5},
        {10099.00, 2.3},
        {10098.00, 3.1}
    });
    
    auto asks = createPriceLevels({
        {10101.00, 1.2},
        {10102.00, 2.4},
        {10103.00, 3.0}
    });
    
    // Apply snapshot
    Timestamp timestamp = getCurrentTimestamp();
    bool result = order_book.applySnapshot(bids, asks, 1, timestamp);
    assert(result == true);
    
    // Get metrics
    double bidVolume = order_book.getBidVolume();
    double askVolume = order_book.getAskVolume();
    double midPrice = order_book.getMidPrice();
    double spread = order_book.getSpread();
    double spreadPercent = order_book.getSpreadPercent();
    double volumeImbalance  = order_book.getVolumeImbalance();
    
    // Expected values
    double expected_bid_volume = 1.5 + 2.3 + 3.1;
    double expected_ask_volume = 1.2 + 2.4 + 3.0;
    double expected_mid_price = (10100.0 + 10101.0) / 2.0;
    double expected_spread = 10101.0 - 10100.0;
    double expected_spread_percent = expected_spread / expected_mid_price * 100.0;
    
    // Check with some tolerance for floating point comparison
    assert(std::abs(bidVolume - expected_bid_volume) < 0.001);
    assert(std::abs(askVolume - expected_ask_volume) < 0.001);
    assert(std::abs(midPrice - expected_mid_price) < 0.001);
    assert(std::abs(spread - expected_spread) < 0.001);
    assert(std::abs(spreadPercent - expected_spread_percent) < 0.001);
    
    std::cout << "Order book metrics test passed" << std::endl;
}

// Test callback functionality
void testOrderBookCallback() {
    bool callback_called = false;
    
    // Create a callback that sets the flag when called
    OrderBookCallback callback = [&callback_called](const OrderBook& book) {
        callback_called = true;
        // Verify the book is valid
        assert(book.isValid());
        // Check some property to make sure we got the right book
        // assert(book.getSymbol() == Symbol{"BTC/USDT"});
        // assert(book.getSymbol().base == "btc");
        // assert(book.getSymbol().toString() == "BTC/USDT" || book.getSymbol().toString() == "btcusdt");
        assert(book.getExchangeId().toString() == "binance");
        assert(book.getSequenceNumber() == 1);
    };
    
    // Create order book with callback
    // OrderBook(const ExchangeId& exchangeId, const Symbol& symbol, OrderBookCallback callback = nullptr);
    // static const ExchangeId instance("BINANCE");

    OrderBook order_book(ExchangeId::binance(), Symbol::fromString("BTC/USDT"), callback);

    // Create initial snapshot
    auto bids = createPriceLevels({
        {10100.00, 1.5},
        {10099.00, 2.3}
    });
    
    auto asks = createPriceLevels({
        {10101.00, 1.2},
        {10102.00, 2.4}
    });
    
    // Apply snapshot - should trigger callback
    Timestamp timestamp = getCurrentTimestamp();
    bool result = order_book.applySnapshot(bids, asks, 1, timestamp);
    assert(result == true);
    
    // Verify callback was called
    assert(callback_called);
    
    std::cout << "Order book callback test passed" << std::endl;
}
int main() {
     // Run all tests
     testOrderBookBasic();
     testOrderBookUpdates();
     testOrderBookMetrics();
     testOrderBookCallback();
     
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
