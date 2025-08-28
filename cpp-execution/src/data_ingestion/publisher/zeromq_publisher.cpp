/**
 * @file zeromq_publisher.cpp
 * @brief Implementation of ZeroMQ market data publisher
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

 #include "data_ingestion/publisher/zeromq_publisher.hpp"
 #include <iostream>
 #include <iomanip>
 #include <sstream>
 #include <chrono>
 
 // Include compression libraries based on what's available
 #ifdef HAS_ZSTD
 #include <zstd.h>
 #endif
 
 namespace trading {
 namespace data_ingestion {
 
 ZeroMQPublisher::ZeroMQPublisher(const PublisherConfig& config)
     : config_(config),
       context_(std::make_unique<zmq::context_t>(1)),
       last_rate_check_time_(std::chrono::system_clock::now()) {
     
     stats_.start_time = std::chrono::system_clock::now();
     stats_.last_publish_time = stats_.start_time;
     
     // Initialize ZeroMQ socket
     initializeSocket();
 }
 
 ZeroMQPublisher::~ZeroMQPublisher() {
     stop();
 }
 
 bool ZeroMQPublisher::start() {
     if (running_) {
         return false;  // Already running
     }
     
     running_ = true;
     worker_thread_ = std::thread(&ZeroMQPublisher::workerFunction, this);
     
     return true;
 }
 
 void ZeroMQPublisher::stop() {
     if (!running_) {
         return;  // Already stopped
     }
     
     running_ = false;
     
     // Notify worker thread to check running_ status
     queue_condition_.notify_all();
     
     // Join worker thread if joinable
     if (worker_thread_.joinable()) {
         worker_thread_.join();
     }
     
     // Clear the queue
     {
         std::lock_guard<std::mutex> lock(queue_mutex_);
         std::queue<std::pair<std::string, std::vector<uint8_t>>> empty;
         std::swap(message_queue_, empty);
     }
 }
 
 bool ZeroMQPublisher::publish(const std::string& topic, const std::vector<uint8_t>& data) {
     if (!running_) {
         return false;
     }
     
     // Apply rate limiting if configured
     if (shouldThrottlePublishing()) {
         // Invoke back pressure callback if registered
         std::lock_guard<std::mutex> lock(callback_mutex_);
         if (back_pressure_callback_) {
             back_pressure_callback_(topic, data);
         }
         
         stats_.messages_dropped++;
         return false;
     }
     
     // Compress data if configured
     std::vector<uint8_t> compressed_data = data;
     if (config_.compression != CompressionAlgorithm::NONE) {
         stats_.bytes_before_compression += data.size();
         compressed_data = compressData(data);
         stats_.bytes_after_compression += compressed_data.size();
     }
     
     // Add to publish queue with back-pressure checks
     {
         std::lock_guard<std::mutex> lock(queue_mutex_);
         
         // Check queue size for back-pressure
         if (message_queue_.size() >= config_.max_queue_size) {
             // Apply back pressure - don't queue the message
             std::lock_guard<std::mutex> cb_lock(callback_mutex_);
             if (back_pressure_callback_) {
                 back_pressure_callback_(topic, data);
             }
             
             stats_.messages_dropped++;
             return false;
         }
         
         // Queue the message
         message_queue_.push(std::make_pair(topic, std::move(compressed_data)));
         stats_.current_queue_size = message_queue_.size();
         
         if (stats_.current_queue_size > stats_.max_queue_size_reached) {
             stats_.max_queue_size_reached = stats_.current_queue_size;
         }
     }
     
     // Signal worker thread that a new message is available
     queue_condition_.notify_one();
     
     return true;
 }
 
 PublisherStats ZeroMQPublisher::getStats() const {
     return stats_;
 }
 
 void ZeroMQPublisher::registerBackPressureCallback(
     std::function<void(const std::string&, const std::vector<uint8_t>&)> callback) {
     std::lock_guard<std::mutex> lock(callback_mutex_);
     back_pressure_callback_ = std::move(callback);
 }
 
 bool ZeroMQPublisher::updateConfig(const PublisherConfig& config) {
     // We can't change the ZMQ endpoint while running
     if (running_ && config_.zmq_endpoint != config.zmq_endpoint) {
         return false;
     }
     
     config_ = config;
     
     // If we're not running, update the socket with new config
     if (!running_) {
         initializeSocket();
     }
     
     return true;
 }
 
 void ZeroMQPublisher::workerFunction() {
     auto last_heartbeat_time = std::chrono::system_clock::now();
     
     while (running_) {
         std::pair<std::string, std::vector<uint8_t>> message;
         bool has_message = false;
         
         // Get a message from the queue
         {
             std::unique_lock<std::mutex> lock(queue_mutex_);
             
             // Wait for a message or check running_ status every 100ms
             auto wait_result = queue_condition_.wait_for(lock, std::chrono::milliseconds(100), 
                 [this] { return !message_queue_.empty() || !running_; });
             
             if (!running_) {
                 break;
             }
             
             // Send heartbeat if needed
             auto now = std::chrono::system_clock::now();
             if (config_.heartbeat_interval_ms > 0 && 
                 std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time).count() 
                     >= config_.heartbeat_interval_ms) {
                 sendHeartbeat();
                 last_heartbeat_time = now;
             }
             
             // If there's no message, continue the loop
             if (message_queue_.empty()) {
                 continue;
             }
             
             // Get the message
             message = std::move(message_queue_.front());
             message_queue_.pop();
             has_message = true;
             
             // Update queue size stats
             stats_.current_queue_size = message_queue_.size();
         }
         
         // If we have a message, publish it
         if (has_message) {
             try {
                 // First send the topic
                 zmq::message_t topic_msg(message.first.data(), message.first.size());
                 publisher_->send(topic_msg, zmq::send_flags::sndmore);
                 
                 // Then send the data
                 zmq::message_t data_msg(message.second.data(), message.second.size());
                 publisher_->send(data_msg, zmq::send_flags::none);
                 
                 // Update statistics
                 stats_.messages_published++;
                 stats_.bytes_published += message.first.size() + message.second.size();
                 stats_.last_publish_time = std::chrono::system_clock::now();
             }
             catch (const zmq::error_t& e) {
                 std::cerr << "ZeroMQ error when publishing: " << e.what() << std::endl;
             }
         }
     }
 }
 
 void ZeroMQPublisher::initializeSocket() {
     try {
         // Create new socket
         publisher_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::pub);
         
         // Set socket options
         publisher_->set(zmq::sockopt::linger, config_.zmq_linger_ms);
         if (config_.zmq_hwm > 0) {
             publisher_->set(zmq::sockopt::sndhwm, config_.zmq_hwm);
         }
         
         // Bind to endpoint
         publisher_->bind(config_.zmq_endpoint);
     }
     catch (const zmq::error_t& e) {
         std::cerr << "ZeroMQ error when initializing socket: " << e.what() << std::endl;
         throw;  // Re-throw the exception
     }
 }
 
 bool ZeroMQPublisher::shouldThrottlePublishing() {
     // If no rate limit, never throttle
     if (config_.max_publish_rate <= 0) {
         return false;
     }
     
     auto now = std::chrono::system_clock::now();
     auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
         now - last_rate_check_time_).count();
     
     // Reset counter every second
     if (elapsed_ms >= 1000) {
         messages_in_current_interval_ = 0;
         last_rate_check_time_ = now;
         return false;
     }
     
     // Check if we've exceeded the rate
     if (messages_in_current_interval_ >= config_.max_publish_rate) {
         return true;  // Throttle
     }
     
     // Increment counter and don't throttle
     messages_in_current_interval_++;
     return false;
 }
 
 void ZeroMQPublisher::sendHeartbeat() {
     try {
         // Topic for heartbeat
         std::string topic = "system.heartbeat.publisher";
         zmq::message_t topic_msg(topic.data(), topic.size());
         publisher_->send(topic_msg, zmq::send_flags::sndmore);
         
         // Create heartbeat message with timestamp
         auto now = std::chrono::system_clock::now();
         auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
             now.time_since_epoch()).count();
         
         std::string msg = "timestamp=" + std::to_string(now_ms);
         zmq::message_t data_msg(msg.data(), msg.size());
         publisher_->send(data_msg, zmq::send_flags::none);
     }
     catch (const zmq::error_t& e) {
         std::cerr << "ZeroMQ error when sending heartbeat: " << e.what() << std::endl;
     }
 }
 
 std::vector<uint8_t> ZeroMQPublisher::compressData(const std::vector<uint8_t>& data) {
     // If no compression requested, return original data
     if (config_.compression == CompressionAlgorithm::NONE) {
         return data;
     }
     
     // Handle ZSTD compression if available
     #ifdef HAS_ZSTD
     if (config_.compression == CompressionAlgorithm::ZSTD) {
         // Allocate buffer for compressed data (worst case size)
         size_t compressed_size = ZSTD_compressBound(data.size());
         std::vector<uint8_t> compressed(compressed_size);
         
         // Compress
         compressed_size = ZSTD_compress(
             compressed.data(), compressed.size(),
             data.data(), data.size(),
             config_.compression_level);
         
         if (ZSTD_isError(compressed_size)) {
             std::cerr << "ZSTD compression error: " 
                       << ZSTD_getErrorName(compressed_size) << std::endl;
             return data;  // Return uncompressed on error
         }
         
         // Resize to actual compressed size
         compressed.resize(compressed_size);
         return compressed;
     }
     #endif
     
     // If requested compression not available, return original data
     std::cerr << "Requested compression algorithm not available" << std::endl;
     return data;
 }
 
 // Generic Market Data Publisher implementation
 MarketDataPublisher::MarketDataPublisher(const PublisherConfig& config)
     : config_(config) {
     
     // Create ZeroMQ publisher
     zmq_publisher_ = std::make_unique<ZeroMQPublisher>(config);
 }
 
 MarketDataPublisher::~MarketDataPublisher() {
     stop();
 }
 
 bool MarketDataPublisher::start() {
     return zmq_publisher_->start();
 }
 
 void MarketDataPublisher::stop() {
     zmq_publisher_->stop();
 }
 
 bool MarketDataPublisher::publishMarketData(const std::string& source, const std::string& type, 
                                            const std::vector<uint8_t>& data) {
     // Create topic string: market.<source>.<type>
     std::string topic = "market." + source + "." + type;
     
     // Publish via ZeroMQ
     return zmq_publisher_->publish(topic, data);
 }
 
 bool MarketDataPublisher::publishRawData(const std::string& topic, const std::vector<uint8_t>& data) {
     return zmq_publisher_->publish(topic, data);
 }
 
 PublisherStats MarketDataPublisher::getStats() const {
     return zmq_publisher_->getStats();
 }
 
 bool MarketDataPublisher::updateConfig(const PublisherConfig& config) {
     config_ = config;
     return zmq_publisher_->updateConfig(config);
 }
 
 } // namespace data_ingestion
 } // namespace trading