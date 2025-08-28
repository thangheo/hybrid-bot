// example/order_book_subscriber_example.cpp
#include "generated/order_book.pb.h"  // Generated Protocol Buffer headers
#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <map>
#include <csignal>
#include <cstring>

#ifdef HAS_ZSTD
#include <zstd.h>
#endif

using namespace trading::data_ingestion::proto;

// Global variable for clean shutdown
volatile bool running = true;

// Signal handler
void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    running = false;
}

// Statistics for the subscriber
struct SubscriberStats {
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> updates_processed{0};
    std::atomic<uint64_t> heartbeats_received{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> bytes_decompressed{0};
    std::chrono::system_clock::time_point start_time;
    std::map<std::string, uint64_t> updates_by_exchange;
    std::map<std::string, uint64_t> updates_by_pair;
};

// Attempt to decompress data using ZSTD
std::vector<uint8_t> tryDecompress(const std::vector<uint8_t>& data) {
#ifdef HAS_ZSTD
    // Check if it might be ZSTD compressed (by checking header magic)
    if (data.size() >= 4 && ZSTD_isFrame(data.data(), data.size())) {
        // Get original size
        unsigned long long decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
        
        if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || 
            decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
            std::cerr << "Error: Cannot determine original size for ZSTD data" << std::endl;
            return data;  // Return original data
        }
        
        // Allocate buffer for decompressed data
        std::vector<uint8_t> decompressed(decompressed_size);
        
        // Decompress
        size_t result = ZSTD_decompress(
            decompressed.data(), decompressed.size(),
            data.data(), data.size());
        
        if (ZSTD_isError(result)) {
            std::cerr << "ZSTD decompression error: " 
                      << ZSTD_getErrorName(result) << std::endl;
            return data;  // Return original data
        }
        
        return decompressed;
    }
#endif
    // Not ZSTD compressed or ZSTD not available
    return data;
}

// Process an order book update
void processOrderBookUpdate(const OrderBookUpdate& update, SubscriberStats& stats) {
    stats.updates_processed++;
    
    // Update exchange and pair statistics
    std::string exchange = update.exchangeId();
    std::string pair = update.tradingPair();
    
    stats.updates_by_exchange[exchange]++;
    stats.updates_by_pair[pair]++;
    
    // Print basic info about the update
    std::cout << "Received order book update: "
              << exchange << ":" << pair
              << " seq=" << update.sequence_number()
              << " snapshot=" << (update.is_snapshot() ? "yes" : "no")
              << " bids=" << update.bids_size()
              << " asks=" << update.asks_size()
              << std::endl;
    
    // Could add more processing here...
}

// Display subscriber statistics
void displayStats(const SubscriberStats& stats) {
    auto now = std::chrono::system_clock::now();
    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
        now - stats.start_time).count();
    
    if (elapsed_sec == 0) elapsed_sec = 1;  // Avoid division by zero
    
    std::cout << "===== Subscriber Statistics =====" << std::endl;
    std::cout << "Messages received: " << stats.messages_received << std::endl;
    std::cout << "Updates processed: " << stats.updates_processed << std::endl;
    std::cout << "Heartbeats received: " << stats.heartbeats_received << std::endl;
    std::cout << "Errors: " << stats.errors << std::endl;
    std::cout << "Reception rate: " << (stats.messages_received / elapsed_sec) << " msg/sec" << std::endl;
    
    // Show stats by exchange
    std::cout << "Updates by exchange:" << std::endl;
    for (const auto& [exchange, count] : stats.updates_by_exchange) {
        std::cout << "  " << exchange << ": " << count << std::endl;
    }
    
    // Show stats by trading pair
    std::cout << "Updates by trading pair:" << std::endl;
    for (const auto& [pair, count] : stats.updates_by_pair) {
        std::cout << "  " << pair << ": " << count << std::endl;
    }
    
    // Calculate decompression ratio if compression is used
    if (stats.bytes_decompressed > 0) {
        double ratio = static_cast<double>(stats.bytes_decompressed) / 
                      static_cast<double>(stats.bytes_received);
        std::cout << "Decompression ratio: " << std::fixed << std::setprecision(2) 
                  << (ratio) << "x" << std::endl;
    }
    
    std::cout << "Total bytes received: " << (stats.bytes_received / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "================================" << std::endl;
}

int main(int argc, char* argv[]) {
    // Register signal handler for clean shutdown
    signal(SIGINT, signalHandler);
    
    // Default ZeroMQ endpoint
    std::string endpoint = "tcp://localhost:5555";
    
    // Use command line argument for endpoint if provided
    if (argc > 1) {
        endpoint = argv[1];
    }
    
    std::cout << "Connecting to publisher at " << endpoint << std::endl;
    
    // Initialize ZeroMQ context and subscriber socket
    zmq::context_t context(1);
    zmq::socket_t subscriber(context, zmq::socket_type::sub);
    
    // Set socket options
    subscriber.set(zmq::sockopt::rcvhwm, 1000);  // High water mark for messages
    subscriber.set(zmq::sockopt::linger, 1000);  // Linger period when closing
    
    // Connect to publisher
    try {
        subscriber.connect(endpoint);
        
        // Subscribe to all order book updates
        subscriber.set(zmq::sockopt::subscribe, "market.orderbook.normalized.");
        
        // Also subscribe to heartbeats
        subscriber.set(zmq::sockopt::subscribe, "system.heartbeat.");
    }
    catch (const zmq::error_t& e) {
        std::cerr << "Failed to connect to publisher: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Subscriber started. Press Ctrl+C to stop." << std::endl;
    
    // Initialize statistics
    SubscriberStats stats;
    stats.start_time = std::chrono::system_clock::now();
    
    // Process incoming messages
    int message_count = 0;
    auto stats_time = std::chrono::system_clock::now();
    
    while (running) {
        zmq::message_t topic;
        zmq::message_t message;
        
        try {
            // Receive with timeout to allow checking running flag
            auto topic_result = subscriber.recv(topic, zmq::recv_flags::dontwait);
            if (!topic_result.has_value()) {
                // No message available, sleep and try again
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            
            // Receive the message body
            auto msg_result = subscriber.recv(message, zmq::recv_flags::dontwait);
            if (!msg_result.has_value()) {
                std::cerr << "Error: Received topic but no message" << std::endl;
                stats.errors++;
                continue;
            }
            
            stats.messages_received++;
            stats.bytes_received += topic.size() + message.size();
            
            // Convert topic to string
            std::string topic_str(static_cast<const char*>(topic.data()), topic.size());
            
            // Handle heartbeat messages
            if (topic_str.find("system.heartbeat") == 0) {
                stats.heartbeats_received++;
                std::string heartbeat_data(static_cast<const char*>(message.data()), message.size());
                std::cout << "Received heartbeat: " << heartbeat_data << std::endl;
                continue;
            }
            
            // Handle order book updates
            if (topic_str.find("market.orderbook.normalized") == 0) {
                // Copy message data to vector for processing
                std::vector<uint8_t> message_data(
                    static_cast<const uint8_t*>(message.data()),
                    static_cast<const uint8_t*>(message.data()) + message.size());
                
                // Try to decompress the data
                std::vector<uint8_t> decompressed_data = tryDecompress(message_data);
                stats.bytes_decompressed += decompressed_data.size();
                
                // Parse the Protocol Buffer message
                OrderBookUpdate update;
                if (update.ParseFromArray(decompressed_data.data(), decompressed_data.size())) {
                    processOrderBookUpdate(update, stats);
                }
                else {
                    std::cerr << "Failed to parse order book update" << std::endl;
                    stats.errors++;
                }
            }
            
            message_count++;
            
            // Display stats every 1000 messages
            if (message_count % 1000 == 0) {
                displayStats(stats);
                stats_time = std::chrono::system_clock::now();
            }
        }
        catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {  // EAGAIN is expected with non-blocking recv
                std::cerr << "ZeroMQ error: " << e.what() << std::endl;
                stats.errors++;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            stats.errors++;
        }
    }
    
    std::cout << "Closing subscriber..." << std::endl;
    subscriber.close();
    context.close();
    
    std::cout << "Final statistics:" << std::endl;
    displayStats(stats);
    
    return 0;
}