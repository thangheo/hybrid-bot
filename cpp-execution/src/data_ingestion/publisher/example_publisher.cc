// example/order_book_publisher_example.cpp
// #include "data_ingestion/order_book_publisher.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <csignal>

using namespace trading::data_ingestion;

// Global variable for clean shutdown
volatile bool running = true;

// Signal handler
void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    running = false;
}

// Generate a random order book update for testing
OrderBookUpdate generateRandomOrderBookUpdate(const core::ExchangeId& exchangeId, 
                                             const core::Symbol& tradingPair) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> price_dist(10000.0, 10100.0);
    static std::uniform_real_distribution<> qty_dist(0.01, 10.0);
    static std::uniform_int_distribution<> depth_dist(1, 10);
    
    OrderBookUpdate update;
    update.exchangeId = exchangeId;
    update.tradingPair = tradingPair;
    update.exchangeTimestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    update.receivedTimestamp = update.exchangeTimestamp + 5000000;  // 5ms later
    static int64_t seq_num = 1;
    update.sequenceNumber = seq_num++;
    update.isSnapshot = (seq_num % 100 == 1);  // Every 100th update is a snapshot
    
    // Generate random bids
    int bid_depth = depth_dist(gen);
    double bid_start = price_dist(gen);
    for (int i = 0; i < bid_depth; ++i) {
        PriceLevel level;
        double price = bid_start - i * 0.1;  // Decrease price for each level
        double qty = qty_dist(gen);
        
        std::stringstream price_ss, qty_ss;
        price_ss << std::fixed << std::setprecision(2) << price;
        qty_ss << std::fixed << std::setprecision(8) << qty;
        
        level.price = price_ss.str();
        level.quantity = qty_ss.str();
        
        // Add some individual orders at this level
        int num_orders = std::uniform_int_distribution<>(1, 5)(gen);
        for (int j = 0; j < num_orders; ++j) {
            double order_qty = qty / num_orders;
            std::stringstream order_id_ss, order_qty_ss;
            order_id_ss << "order_" << seq_num << "_" << i << "_" << j;
            order_qty_ss << std::fixed << std::setprecision(8) << order_qty;
            
            level.orders.push_back(std::make_pair(order_id_ss.str(), order_qty_ss.str()));
        }
        
        update.bids.push_back(level);
    }
    
    // Generate random asks
    int ask_depth = depth_dist(gen);
    double ask_start = bid_start + 0.1;  // Start asks slightly above highest bid
    for (int i = 0; i < ask_depth; ++i) {
        PriceLevel level;
        double price = ask_start + i * 0.1;  // Increase price for each level
        double qty = qty_dist(gen);
        
        std::stringstream price_ss, qty_ss;
        price_ss << std::fixed << std::setprecision(2) << price;
        qty_ss << std::fixed << std::setprecision(8) << qty;
        
        level.price = price_ss.str();
        level.quantity = qty_ss.str();
        
        // Add some individual orders at this level
        int num_orders = std::uniform_int_distribution<>(1, 5)(gen);
        for (int j = 0; j < num_orders; ++j) {
            double order_qty = qty / num_orders;
            std::stringstream order_id_ss, order_qty_ss;
            order_id_ss << "order_" << seq_num << "_" << i << "_" << j;
            order_qty_ss << std::fixed << std::setprecision(8) << order_qty;
            
            level.orders.push_back(std::make_pair(order_id_ss.str(), order_qty_ss.str()));
        }
        
        update.asks.push_back(level);
    }
    
    // Generate a simple checksum
    std::stringstream checksum_ss;
    checksum_ss << "checksum_" << seq_num;
    update.checksum = checksum_ss.str();
    
    return update;
}

// Function to display publisher stats
void displayStats(const PublisherStats& stats) {
    auto now = std::chrono::system_clock::now();
    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
        now - stats.start_time).count();
    
    if (elapsed_sec == 0) elapsed_sec = 1;  // Avoid division by zero
    
    std::cout << "===== Publisher Statistics =====" << std::endl;
    std::cout << "Messages published: " << stats.messages_published << std::endl;
    std::cout << "Messages dropped: " << stats.messages_dropped << std::endl;
    std::cout << "Current queue size: " << stats.current_queue_size << std::endl;
    std::cout << "Max queue size reached: " << stats.max_queue_size_reached << std::endl;
    std::cout << "Publish rate: " << (stats.messages_published / elapsed_sec) << " msg/sec" << std::endl;
    
    // Calculate compression ratio if compression is used
    if (stats.bytes_before_compression > 0) {
        double ratio = static_cast<double>(stats.bytes_after_compression) / 
                      static_cast<double>(stats.bytes_before_compression);
        std::cout << "Compression ratio: " << std::fixed << std::setprecision(2) << (ratio * 100) << "%" << std::endl;
    }
    
    std::cout << "Total bytes published: " << (stats.bytes_published / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "================================" << std::endl;
}

// Back pressure callback
void handleBackPressure(const OrderBookUpdate& update) {
    std::cout << "Back pressure applied for update from " 
              << update.exchangeId << ":" << update.tradingPair 
              << " sequence: " << update.sequenceNumber << std::endl;
}

int main() {
    // Register signal handler for clean shutdown
    signal(SIGINT, signalHandler);
    
    // Configure the publisher
    PublisherConfig config;
    config.zmq_endpoint = "tcp://*:5555";  // Change to appropriate endpoint
    config.max_queue_size = 1000;
    config.max_publish_rate = 1000;  // 1000 messages per second
    config.compression = CompressionAlgorithm::ZSTD;  // Use Zstandard compression
    config.compression_level = 3;  // Balanced compression/speed
    config.heartbeat_interval_ms = 5000;  // 5 second heartbeat
    
    std::cout << "Creating and starting order book publisher..." << std::endl;
    OrderBookPublisher publisher(config);
    
    // Register back pressure callback
    publisher.registerBackPressureCallback(handleBackPressure);
    
    // Start the publisher
    if (!publisher.start()) {
        std::cerr << "Failed to start the publisher" << std::endl;
        return 1;
    }
    
    std::cout << "Publisher started. Press Ctrl+C to stop." << std::endl;
    
    // Exchanges and trading pairs to simulate
    std::vector<std::pair<std::string, std::string>> exchange_pairs = {
        {"binance", "BTC/USDT"},
        {"binance", "ETH/USDT"},
        {"coinbase", "BTC/USD"},
        {"coinbase", "ETH/USD"},
        {"kraken", "XBT/USD"},
        {"kraken", "ETH/USD"}
    };
    
    // Generate and publish random order book updates
    int update_count = 0;
    auto stats_time = std::chrono::system_clock::now();
    
    while (running) {
        // Choose a random exchange-pair combination
        size_t idx = std::rand() % exchange_pairs.size();
        const auto& [exchange, pair] = exchange_pairs[idx];
        
        // Generate random order book update
        auto update = generateRandomOrderBookUpdate(exchange, pair);
        
        // Publish the update
        if (!publisher.publishOrderBookUpdate(update)) {
            std::cout << "Failed to publish update: back pressure applied" << std::endl;
        }
        
        update_count++;
        
        // Display stats every 1000 updates
        if (update_count % 1000 == 0) {
            auto now = std::chrono::system_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - stats_time).count();
            
            std::cout << "Published " << update_count << " updates" << std::endl;
            displayStats(publisher.getStats());
            
            // Adjust sleep time to maintain desired rate
            double rate = 1000.0 * 1000.0 / elapsed_ms;
            std::cout << "Current publish rate: " << rate << " updates/sec" << std::endl;
            
            stats_time = now;
        }
        
        // Sleep to control publication rate
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    std::cout << "Stopping publisher..." << std::endl;
    publisher.stop();
    std::cout << "Final statistics:" << std::endl;
    displayStats(publisher.getStats());
    
    return 0;
}