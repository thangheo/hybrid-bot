/**
 * @file zeromq_publisher.hpp
 * @brief ZeroMQ-based market data publisher
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * Provides a high-performance, thread-safe ZeroMQ publisher with support for:
 * - Message queuing and asynchronous publishing
 * - Rate limiting and back pressure mechanisms
 * - Optional data compression
 * - Heartbeat generation
 * - Comprehensive statistics tracking
 * 
 * Created on: April 30, 2025
 */
#pragma once
    
 #include <zmq.hpp>
 #include <string>
 #include <memory>
 #include <queue>
 #include <vector>
 #include <thread>
 #include <mutex>
 #include <atomic>
 #include <condition_variable>
 #include <functional>
 #include <data_ingestion/orderbook/order_book_types.hpp>
 #include "config.hpp"
namespace trading {
namespace data_ingestion {
    
 // Compression algorithm options
//  enum class CompressionAlgorithm {
//      NONE,
//      ZSTD,  // Zstandard compression
//      // Add other compression algorithms as needed
//  };
 
 // Publisher configuration
//  struct PublisherConfig {
//      std::string zmq_endpoint = "tcp://*:5555";  // ZeroMQ binding endpoint
//      int zmq_hwm = 1000;             // High water mark for outgoing messages
//      int zmq_linger_ms = 1000;       // Linger period when closing socket
//      size_t max_queue_size = 10000;  // Maximum message queue size
//      int max_publish_rate = 0;       // Maximum messages per second (0 = unlimited)
//      CompressionAlgorithm compression = CompressionAlgorithm::NONE;  // Compression algorithm
//      int compression_level = 3;      // Compression level (algorithm-specific)
//      int heartbeat_interval_ms = 5000;  // Heartbeat interval in milliseconds (0 = disabled)
//  };
 
 // Publisher statistics
 struct PublisherStats {
     uint64_t messages_published = 0;     // Total messages published
     uint64_t messages_dropped = 0;       // Messages dropped due to back pressure
     uint64_t bytes_published = 0;        // Total bytes published
     uint64_t bytes_before_compression = 0;  // Total bytes before compression
     uint64_t bytes_after_compression = 0;   // Total bytes after compression
     size_t current_queue_size = 0;       // Current message queue size
     size_t max_queue_size_reached = 0;   // Maximum queue size reached
     std::chrono::system_clock::time_point start_time;  // Publisher start time
     std::chrono::system_clock::time_point last_publish_time;  // Last successful publish time
 };
 
 // ZeroMQ-based publisher
 class ZeroMQPublisher {
 public:
     ZeroMQPublisher(const PublisherConfig& config);
     ~ZeroMQPublisher();
     
     // Start the publisher
     bool start();
     
     // Stop the publisher
     void stop();
     
     // Publish a message
     bool publish(const std::string& topic, const std::vector<uint8_t>& data);
     
     // Get publisher statistics
     PublisherStats getStats() const;
     
     // Register a callback for back pressure notification
     void registerBackPressureCallback(std::function<void(const std::string&, const std::vector<uint8_t>&)> callback);
     
     // Update publisher configuration
     bool updateConfig(const PublisherConfig& config);
     
 private:
     PublisherConfig config_;
     std::unique_ptr<zmq::context_t> context_;
     std::unique_ptr<zmq::socket_t> publisher_;
     
     std::thread worker_thread_;
     std::atomic<bool> running_{false};
     
     std::mutex queue_mutex_;
     std::condition_variable queue_condition_;
     std::queue<std::pair<std::string, std::vector<uint8_t>>> message_queue_;
     
     std::mutex callback_mutex_;
     std::function<void(const std::string&, const std::vector<uint8_t>&)> back_pressure_callback_;
     
     PublisherStats stats_;
     std::atomic<int> messages_in_current_interval_{0};
     std::chrono::system_clock::time_point last_rate_check_time_;
     
     // Worker thread function
     void workerFunction();
     
     // Helper functions
     void initializeSocket();
     bool shouldThrottlePublishing();
     void sendHeartbeat();
     std::vector<uint8_t> compressData(const std::vector<uint8_t>& data);
 };
 
 // Market data publisher using ZeroMQ
 class MarketDataPublisher {
 public:
     MarketDataPublisher(const PublisherConfig& config);
     ~MarketDataPublisher();
     
     // Start the publisher
     bool start();
     
     // Stop the publisher
     void stop();
     
     // Publish market data
     bool publishMarketData(const std::string& source, const std::string& type, 
                           const std::vector<uint8_t>& data);
     
     // Publish raw data with custom topic
     bool publishRawData(const std::string& topic, const std::vector<uint8_t>& data);
     
     // Get publisher statistics
     PublisherStats getStats() const;
     
     // Update publisher configuration
     bool updateConfig(const PublisherConfig& config);
     
 private:
     PublisherConfig config_;
     std::unique_ptr<ZeroMQPublisher> zmq_publisher_;
 };
 
 } // namespace data_ingestion
 } // namespace trading
  