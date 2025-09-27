/**
 * @file zmq_server.cpp
 * @brief Implementation of Generic ZeroMQ Server for trading system communications
 * 
 * @author Trading System Team  
 * @date September 17, 2025
 */

#include "communication/zmq_server.hpp"
#include "communication/zmq_manager.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <random>
#include <nlohmann/json.hpp>
#include <regex>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace trading {
namespace communication {
    

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

std::string GenericMessage::generateCorrelationId() {
    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint64_t> dis;
    
    std::ostringstream oss;
    oss << std::hex << dis(gen);
    return oss.str();
}

// Replace characters not allowed in topic names with underscore
static std::string sanitizeTopic(const std::string& topic) {
    try {
        static const std::regex invalid(R"([^a-zA-Z0-9._-])");
        return std::regex_replace(topic, invalid, "_");
    } catch (const std::regex_error&) {
        // Fallback: simple character replace for common problematic chars
        std::string out = topic;
        for (auto &c : out) {
            if (!( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
                c = '_';
            }
        }
        return out;
    }
}


// =============================================================================
// CONSTRUCTOR AND DESTRUCTOR
// =============================================================================

GenericZmqServer::GenericZmqServer(const ZmqServerConfig& config) 
    : config_(config), priority_queue_(createPriorityComparator()) {

    if (!config_.validate()) {
        throw std::invalid_argument("Invalid ZeroMQ server configuration");
    }
    
    logInfo("Generic ZMQ Server created with config validation passed");
}

GenericZmqServer::~GenericZmqServer() {
    if (state_.load() != ServerState::STOPPED) {
        stop();
    }
}

// =============================================================================
// SERVER LIFECYCLE
// =============================================================================

bool GenericZmqServer::start() {
    if (state_.load() != ServerState::STOPPED) {
        logError("Cannot start server - not in STOPPED state");
        return false;
    }
    
    state_.store(ServerState::STARTING);
    logInfo("Starting Generic ZMQ Server...");
    
    try {
        // Initialize ZeroMQ context and sockets
        if (!initializeSockets()) {
            state_.store(ServerState::ERROR);
            logError("Failed to initialize ZeroMQ sockets");
            return false;
        }
        state_.store(ServerState::RUNNING);
        stats_.start_time = std::chrono::steady_clock::now();
        // Start publisher threads
        intel_threads_.reserve(config_.intel_publisher_threads);
        for (int i = 0; i < config_.intel_publisher_threads; ++i) {
            intel_threads_.emplace_back(&GenericZmqServer::intelPublisherWorker, this);
        }
        internal_threads_.reserve(config_.internal_publisher_threads);
        for (int i = 0; i < config_.internal_publisher_threads; ++i) {
            internal_threads_.emplace_back(&GenericZmqServer::internalPublisherWorker, this);
        }

        // Start request handler threads
        request_threads_.reserve(config_.request_handler_threads);
        for (int i = 0; i < config_.request_handler_threads; ++i) {
            request_threads_.emplace_back(&GenericZmqServer::requestWorker, this);
        }
        
        // Start monitoring thread if enabled
        if (config_.enable_monitoring) {
            monitoring_thread_ = std::thread(&GenericZmqServer::monitoringWorker, this);
        }
        
        // Start heartbeat thread if enabled
        if (config_.enable_heartbeat) {
            heartbeat_thread_ = std::thread(&GenericZmqServer::heartbeatWorker, this);
        }
        
        state_.store(ServerState::RUNNING);
        stats_.start_time = std::chrono::steady_clock::now();
        
        logInfo("Generic ZMQ Server started successfully");
        logInfo("Intel publisher endpoint: " + config_.intel_endpoint);
        logInfo("Internal publisher endpoint: " + config_.internal_endpoint);
        logInfo("Request endpoint: " + config_.request_endpoint);
        
        return true;
        
    } catch (const std::exception& e) {
        state_.store(ServerState::ERROR);
        logError("Failed to start server: " + std::string(e.what()));
        cleanup();
        return false;
    }
}

void GenericZmqServer::stop() {
    if (state_.load() == ServerState::STOPPED) {
        return;
    }
    
    logInfo("Stopping Generic ZMQ Server...");
    state_.store(ServerState::STOPPING);
    
    // Note: contexts are managed by ZmqManager (shared). Do NOT shutdown or
    // close the shared context here – closing it would affect other components
    // that may be using the same context. Only sockets are closed below.
    
    
    // Notify all waiting threads
    intel_queue_cv_.notify_all();
    internal_queue_cv_.notify_all();

    // Give threads time to see the state change and exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Join publisher threads first
    logInfo("Stopping publisher threads...");
    for (auto& thread : intel_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    intel_threads_.clear();
    
    for (auto& thread : internal_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    internal_threads_.clear();

    // Join request handler threads
    logInfo("Stopping request threads...");
    for (auto& thread : request_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    request_threads_.clear();
    
    // Join monitoring thread
    logInfo("Stopping monitoring thread...");
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    // Join heartbeat thread
    logInfo("Stopping heartbeat thread...");
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    // Only cleanup after ALL worker threads have stopped
    logInfo("Cleaning up resources...");
    cleanup();

    state_.store(ServerState::STOPPED);
    logInfo("Generic ZMQ Server stopped");
}

// =============================================================================
// PUBLISHING INTERFACE
// =============================================================================
// ...existing code...
bool GenericZmqServer::pushToIntelQueue(GenericMessage&& message, const std::string& correlation_id) {
    std::unique_lock<std::mutex> lock(intel_queue_mutex_);
    if (intel_priority_queue_.size() >= config_.max_intel_queue_size) {
        stats_.queue_overflows++;
        logInfo("Intel queue overflow detected");
        message.correlation_id = correlation_id;
        if (shouldDropMessage(message)) {
            stats_.messages_dropped++;
            logError("Intel message dropped due to queue overflow: " + message.topic);
            return false;
        }
        if (!intel_queue_cv_.wait_for(lock, config_.queue_timeout,
                [this]() { return intel_priority_queue_.size() < config_.max_intel_queue_size || state_.load() != ServerState::RUNNING; })) {
            stats_.messages_dropped++;
            logError("Intel message timeout while waiting for queue space: " + message.topic);
            return false;
        }
    }
    if (state_.load() != ServerState::RUNNING) {
        logError("Server stopped while waiting for intel queue space");
        return false;
    }
    intel_priority_queue_.push(std::move(message));
    intel_queue_cv_.notify_one();
    stats_.messages_queued++;
    return true;
}

bool GenericZmqServer::pushToInternalQueue(GenericMessage&& message, const std::string& correlation_id) {
    std::unique_lock<std::mutex> lock(internal_queue_mutex_);
    if (internal_priority_queue_.size() >= config_.max_internal_queue_size) {
        stats_.queue_overflows++;
        logInfo("Internal queue overflow detected");
        message.correlation_id = correlation_id;
        if (shouldDropMessage(message)) {
            stats_.messages_dropped++;
            logError("Message dropped due to queue overflow: " + message.topic);
            return false;
        }
        if (!internal_queue_cv_.wait_for(lock, config_.queue_timeout,
                [this]() { return internal_priority_queue_.size() < config_.max_internal_queue_size || state_.load() != ServerState::RUNNING; })) {
            stats_.messages_dropped++;
            logError("Message timeout while waiting for internal queue space: " + message.topic);
            return false;
        }
    }
    if (state_.load() != ServerState::RUNNING) {
        logError("Server stopped while waiting for internal queue space");
        return false;
    }
    internal_priority_queue_.push(std::move(message));
    internal_queue_cv_.notify_one();
    stats_.messages_queued++;
    return true;
}

bool GenericZmqServer::publishAsync(const std::string& topic, std::string data,
                                   MessagePriority priority, const std::string& correlation_id) {
    if (state_.load() != ServerState::RUNNING) {
        logError("Cannot publish - server not running");
        return false;
    }

    if (!isValidTopic(topic)) {
        logError("Invalid topic format: " + topic);
        return false;
    }

    std::string safe_topic = topic;
    // data is taken by value so we can move it into the message without copy
    GenericMessage message(std::move(safe_topic), std::move(data), priority);
    message.correlation_id = correlation_id;

    // logInfo("Async Publishing message: " + topic + " with priority " + std::to_string(static_cast<int>(priority)));

    if (topic == "MARKET_INTEL") {
        return pushToIntelQueue(std::move(message), correlation_id);
    } else {
        return pushToInternalQueue(std::move(message), correlation_id);
    }
}

bool GenericZmqServer::processIntelMessage(const GenericMessage& message) {
    if (!intel_publisher_socket_) {
        logError("Intel publisher socket not available");
        return false;
    }

    try {
        // Validate inputs first
        if (message.topic.empty() || message.data.empty()) {
            logError("Empty topic or data in processIntelMessage");
            return false;
        }

        // Sanitize topic but keep original validation
        std::string clean_topic = sanitizeTopic(message.topic);
        if (clean_topic.empty()) {
            logError("Topic sanitization resulted in empty topic: " + message.topic);
            return false;
        }

        // For protobuf data, validate it's actually binary (not accidentally text)
        if (clean_topic.find("orderbook.") == 0) {
            // Quick check: protobuf should contain non-printable bytes
            bool looks_like_protobuf = false;
            for (size_t i = 0; i < std::min<size_t>(20, message.data.size()); ++i) {
                unsigned char c = static_cast<unsigned char>(message.data[i]);
                if (c < 0x20 || c > 0x7E) {
                    looks_like_protobuf = true;
                    break;
                }
            }
            
            if (!looks_like_protobuf && message.data.size() > 50) {
                logError("Data for orderbook topic looks like text, not protobuf: " + clean_topic + 
                        " (preview: " + message.data.substr(0, 50) + ")");
                return false;
            }
        }

        // CRITICAL FIX: Defensive allocation/copy to avoid OOM or heap corruption
        constexpr size_t kMaxMessageBytes = 512 * 1024; // 512KB safety cap
        if (message.data.size() > kMaxMessageBytes) {
            stats_.messages_dropped++;
            logError("Dropping intel message due to oversized payload: " + clean_topic + " size=" + std::to_string(message.data.size()));
            return false;
        }

        zmq::message_t topic_msg;
        zmq::message_t data_msg;
        try {
            // Build topic message
            topic_msg.rebuild(clean_topic.size());
            std::memcpy(topic_msg.data(), clean_topic.data(), clean_topic.size());

            // Build data message
            data_msg.rebuild(message.data.size());
            std::memcpy(data_msg.data(), message.data.data(), message.data.size());
        } catch (const std::bad_alloc& e) {
            stats_.send_failures++;
            logError(std::string("OOM while allocating zmq frames for intel message: ") + e.what());
            return false;
        } catch (const zmq::error_t& e) {
            stats_.send_failures++;
            logError(std::string("ZMQ error while preparing frames for intel message: ") + e.what());
            return false;
        } catch (const std::exception& e) {
            stats_.send_failures++;
            logError(std::string("Exception while preparing frames for intel message: ") + e.what());
            return false;
        }

        // CRITICAL FIX: Use mutex to ensure atomic multipart send
        std::lock_guard<std::mutex> send_guard(intel_send_mutex_);

        auto send_start = std::chrono::steady_clock::now();
        
        try {
            // Send topic frame with SNDMORE flag
            auto topic_result = intel_publisher_socket_->send(topic_msg, 
                zmq::send_flags::sndmore | zmq::send_flags::dontwait);
            
            if (!topic_result.has_value()) {
                stats_.send_eagain_count++;
                logError("EAGAIN on intel topic send for: " + clean_topic);
                return false;
            }
            
            // Send data frame (final frame, no SNDMORE)
            auto data_result = intel_publisher_socket_->send(data_msg, 
                zmq::send_flags::dontwait);
            
            if (!data_result.has_value()) {
                stats_.send_eagain_count++;
                logError("EAGAIN on intel data send for: " + clean_topic);
                
                // CRITICAL: If data send fails after topic succeeds, we have a partial message
                // Try to flush or reset the socket state
                try {
                    zmq::message_t dummy;
                    intel_publisher_socket_->send(dummy, zmq::send_flags::dontwait);
                } catch (...) {
                    // Ignore errors in recovery attempt
                }
                
                return false;
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() == EAGAIN) {
                stats_.send_eagain_count++;
                logError("ZMQ EAGAIN during intel multipart send for: " + clean_topic);
            } else {
                stats_.send_failures++;
                stats_.connection_errors++;
                logError(std::string("ZMQ error processing intel message: ") + e.what() + 
                        " (errno: " + std::to_string(e.num()) + ") for topic: " + clean_topic);
            }
            return false;
        }

        auto send_end = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(send_end - send_start).count();
        
        // Log performance issues
        const int kWarnMs = 5;
        if (duration_ms > kWarnMs) {
            stats_.send_blocking_events++;
            logInfo(std::string("Slow intel send (ms): ") + std::to_string(duration_ms) + 
                   " topic=" + clean_topic + " data_size=" + std::to_string(message.data.size()));
        }

        // Success logging (reduce frequency)
        static thread_local int success_count = 0;
        if (++success_count % 1000 == 0) {
            logInfo("Intel messages sent successfully: " + std::to_string(success_count) + 
                   " (latest: " + clean_topic + ")");
        }

        return true;
        
    } catch (const std::exception& e) {
        stats_.send_failures++;
        logError("Exception in processIntelMessage: " + std::string(e.what()) + 
                " for topic: " + message.topic);
        return false;
    }
}

// FIXED: processInternalMessage with same improvements
bool GenericZmqServer::processInternalMessage(const GenericMessage& message) {
    if (!internal_publisher_socket_) {
        logError("Internal publisher socket not available");
        return false;
    }

    try {
        // Validate inputs
        if (message.topic.empty() || message.data.empty()) {
            logError("Empty topic or data in processInternalMessage");
            return false;
        }

        std::string clean_topic = sanitizeTopic(message.topic);
        if (clean_topic.empty()) {
            logError("Topic sanitization failed: " + message.topic);
            return false;
        }

        // Defensive allocation/copy
        constexpr size_t kMaxInternalMessageBytes = 512 * 1024; // 512KB cap
        if (message.data.size() > kMaxInternalMessageBytes) {
            stats_.messages_dropped++;
            logError("Dropping internal message due to oversized payload: " + clean_topic + " size=" + std::to_string(message.data.size()));
            return false;
        }

        zmq::message_t topic_msg;
        zmq::message_t data_msg;
        try {
            topic_msg.rebuild(clean_topic.size());
            std::memcpy(topic_msg.data(), clean_topic.data(), clean_topic.size());

            data_msg.rebuild(message.data.size());
            std::memcpy(data_msg.data(), message.data.data(), message.data.size());
        } catch (const std::bad_alloc& e) {
            stats_.send_failures++;
            logError(std::string("OOM while allocating zmq frames for internal message: ") + e.what());
            return false;
        } catch (const zmq::error_t& e) {
            stats_.send_failures++;
            logError(std::string("ZMQ error while preparing frames for internal message: ") + e.what());
            return false;
        } catch (const std::exception& e) {
            stats_.send_failures++;
            logError(std::string("Exception while preparing frames for internal message: ") + e.what());
            return false;
        }

        auto send_start = std::chrono::steady_clock::now();
        
        // Retry logic for internal messages (more tolerance for backpressure)
        const int kMaxRetries = 3;
        bool success = false;
        
        for (int attempt = 0; attempt < kMaxRetries && !success; ++attempt) {
            try {
                // Note: No mutex needed for internal socket if it's only used by one thread
                auto topic_result = internal_publisher_socket_->send(topic_msg, 
                    zmq::send_flags::sndmore | zmq::send_flags::dontwait);
                
                if (!topic_result.has_value()) {
                    stats_.send_eagain_count++;
                    if (attempt + 1 < kMaxRetries) {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                        continue;
                    }
                    logError("EAGAIN on internal topic send after retries: " + clean_topic);
                    return false;
                }
                
                auto data_result = internal_publisher_socket_->send(data_msg, 
                    zmq::send_flags::dontwait);
                
                if (!data_result.has_value()) {
                    stats_.send_eagain_count++;
                    if (attempt + 1 < kMaxRetries) {
                        // Try to send empty frame to complete the multipart message
                        try {
                            zmq::message_t dummy;
                            internal_publisher_socket_->send(dummy, zmq::send_flags::dontwait);
                        } catch (...) {}
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                        continue;
                    }
                    logError("EAGAIN on internal data send after retries: " + clean_topic);
                    return false;
                }
                
                success = true;
                
            } catch (const zmq::error_t& e) {
                if (e.num() == EAGAIN) {
                    stats_.send_eagain_count++;
                    if (attempt + 1 < kMaxRetries) {
                        stats_.send_blocking_events++;
                        std::this_thread::sleep_for(std::chrono::microseconds(200));
                        continue;
                    }
                } else {
                    stats_.send_failures++;
                    logError(std::string("ZMQ error in internal send: ") + e.what() + 
                            " for topic: " + clean_topic);
                    return false;
                }
            }
        }

        if (!success) {
            logError("Failed to send internal message after all retries: " + clean_topic);
            return false;
        }

        auto send_end = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(send_end - send_start).count();

        if (duration_ms > 50) {
            stats_.send_blocking_events++;
            logError(std::string("Very slow internal send (ms): ") + std::to_string(duration_ms) + 
                    " topic=" + clean_topic);
        }

        return true;
        
    } catch (const std::exception& e) {
        stats_.send_failures++;
        logError("Exception in processInternalMessage: " + std::string(e.what()) + 
                " for topic: " + message.topic);
        return false;
    }
}

// FIXED: publishSync method with better error handling
bool GenericZmqServer::publishSync(const std::string& topic, const std::string& data,
                                  MessagePriority priority, const std::string& correlation_id) {
    if (state_.load() != ServerState::RUNNING || !intel_publisher_socket_ || !internal_publisher_socket_) {
        logError("Cannot publish sync - server not running or socket not available");
        return false;
    }

    // Pre-validate
    if (!isValidTopic(topic) || !isTopicAllowed(topic)) {
        logError("Invalid or filtered topic: " + topic);
        return false;
    }

    if (data.empty()) {
        logError("Empty data for topic: " + topic);
        return false;
    }

    try {
        // Create metadata
        static thread_local std::string serialized_buffer;
        static thread_local std::unordered_map<std::string, std::string> metadata_buffer;
        
        metadata_buffer.clear();
        metadata_buffer["correlation_id"] = correlation_id.empty() ? 
            GenericMessage::generateCorrelationId() : correlation_id;
        metadata_buffer["priority"] = std::to_string(static_cast<int>(priority));
        
        // Cache timestamp
        static thread_local auto last_ts_update = std::chrono::steady_clock::now();
        static thread_local std::string cached_timestamp;
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ts_update).count() > 100) {
            cached_timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
            last_ts_update = now;
        }
        metadata_buffer["timestamp"] = cached_timestamp;

        // For sync publishing, we'll use raw data without additional serialization
        // to avoid the serializeWithMetadata overhead
        std::string clean_topic = sanitizeTopic(topic);
        
        // Create message frames
        zmq::message_t topic_msg(clean_topic.size());
        std::memcpy(topic_msg.data(), clean_topic.data(), clean_topic.size());
        
        zmq::message_t data_msg(data.size());
        std::memcpy(data_msg.data(), data.data(), data.size());

        bool send_success = false;
        
        if (topic == "MARKET_INTEL" && intel_publisher_socket_) {
            std::lock_guard<std::mutex> lock(intel_send_mutex_);
            
            auto topic_result = intel_publisher_socket_->send(topic_msg, 
                zmq::send_flags::sndmore | zmq::send_flags::dontwait);
            auto data_result = intel_publisher_socket_->send(data_msg, 
                zmq::send_flags::dontwait);
            
            send_success = topic_result.has_value() && data_result.has_value();
            
        } else if (internal_publisher_socket_) {
            auto topic_result = internal_publisher_socket_->send(topic_msg, 
                zmq::send_flags::sndmore | zmq::send_flags::dontwait);
            auto data_result = internal_publisher_socket_->send(data_msg, 
                zmq::send_flags::dontwait);
            
            send_success = topic_result.has_value() && data_result.has_value();
        }

        if (send_success) {
            stats_.messages_published++;
            stats_.bytes_sent += clean_topic.size() + data.size();
            return true;
        }
        
        return false;

    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {
            logError("ZMQ error in publishSync: " + std::string(e.what()) + " for topic: " + topic);
        }
        return false;
    } catch (const std::exception& e) {
        logError("Exception in publishSync: " + std::string(e.what()) + " for topic: " + topic);
        return false;
    }
}


// Template publishProtobuf implementations moved to header (for proper instantiation)
bool GenericZmqServer::publishWithMetadata(const std::string& topic, const std::string& data,
                                         const std::unordered_map<std::string, std::string>& metadata,
                                         MessagePriority priority) {
    // Extract correlation ID from metadata if present
    std::string correlation_id;
    auto it = metadata.find("correlation_id");
    if (it != metadata.end()) {
        correlation_id = it->second;
    }

    // Sanitize topic and use sync publishing for metadata-rich messages
    std::string safe_topic = topic; //sanitizeTopic(topic); no need
    return publishSync(safe_topic, serializeWithMetadata(data, metadata), priority, correlation_id);
}

// =============================================================================
// REQUEST-REPLY INTERFACE  
// =============================================================================

void GenericZmqServer::registerRequestHandler(const std::string& request_type, RequestHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    request_handlers_[request_type] = std::move(handler);
    logInfo("Registered request handler for type: " + request_type);
}

void GenericZmqServer::unregisterRequestHandler(const std::string& request_type) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    auto it = request_handlers_.find(request_type);
    if (it != request_handlers_.end()) {
        request_handlers_.erase(it);
        logInfo("Unregistered request handler for type: " + request_type);
    }
}

// =============================================================================
// TOPIC MANAGEMENT
// =============================================================================

void GenericZmqServer::addTopicFilter(const std::string& topic_pattern) {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    topic_filters_.push_back(topic_pattern);
    use_topic_filters_ = true;
    logInfo("Added topic filter: " + topic_pattern);
}

void GenericZmqServer::removeTopicFilter(const std::string& topic_pattern) {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    auto it = std::find(topic_filters_.begin(), topic_filters_.end(), topic_pattern);
    if (it != topic_filters_.end()) {
        topic_filters_.erase(it);
        use_topic_filters_ = !topic_filters_.empty();
        logInfo("Removed topic filter: " + topic_pattern);
    }
}

void GenericZmqServer::clearTopicFilters() {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    topic_filters_.clear();
    use_topic_filters_ = false;
    logInfo("Cleared all topic filters");
}

bool GenericZmqServer::isTopicAllowed(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    
    if (!use_topic_filters_) {
        return true;
    }
    
    // Check if topic matches any filter pattern
    for (const auto& pattern : topic_filters_) {
        try {
            std::regex regex_pattern(pattern);
            if (std::regex_match(topic, regex_pattern)) {
                return true;
            }
        } catch (const std::regex_error&) {
            // If regex is invalid, do simple string comparison
            if (topic.find(pattern) != std::string::npos) {
                return true;
            }
        }
    }
    
    return false;
}

// =============================================================================
// HEALTH AND DIAGNOSTICS
// =============================================================================

bool GenericZmqServer::healthCheck() const {
    if (state_.load() != ServerState::RUNNING) {
        return false;
    }
    
    // Check if sockets are available
    if (!intel_publisher_socket_ || !request_socket_ || ! internal_publisher_socket_) {
        return false;
    }
    
    // Check queue utilization
    if (getQueueUtilization("intel") > 0.9 || getQueueUtilization("internal") > 0.9) {
        return false;
    }
    
    // Check error rate (simple heuristic)
    uint64_t total_operations = stats_.messages_published + stats_.requests_processed;
    uint64_t total_errors = stats_.messages_dropped + stats_.requests_failed + stats_.connection_errors;
    
    if (total_operations > 100 && (static_cast<double>(total_errors) / total_operations) > 0.1) {  // 10% error rate
        return false;
    }
    
    return true;
}

void GenericZmqServer::sendHeartbeat() {
    if (state_.load() != ServerState::RUNNING) {
        return;
    }
    
    nlohmann::json heartbeat;
    heartbeat["type"] = "heartbeat";
    heartbeat["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    heartbeat["state"] = static_cast<int>(state_.load());
    heartbeat["stats"] = {
        {"messages_published", stats_.messages_published.load()},
        {"requests_processed", stats_.requests_processed.load()},
        {"queue_size", getQueueSize("intel")},
        {"queue_utilization", getQueueUtilization("intel")},
        {"connection_errors", stats_.connection_errors.load()}
    };
    
    publishSync("system.heartbeat", heartbeat.dump(), MessagePriority::HIGH, "heartbeat");
}
// dropMessages
void GenericZmqServer::dropMessages(const std::string& topic, size_t num_messages) {
    if (state_.load() != ServerState::RUNNING) {
        logError("Cannot drop messages - server not running");
        return;
    }

    if (topic == "MARKET_INTEL") {
        std::lock_guard<std::mutex> lock(intel_queue_mutex_);
        size_t dropped = 0;
        // Rebuild the queue preserving order but dropping up to num_messages matching the topic
        std::queue<GenericMessage> temp_queue;
        while (!intel_priority_queue_.empty()) {
            GenericMessage msg = std::move(intel_priority_queue_.front());
            intel_priority_queue_.pop();
            if (dropped < num_messages && msg.topic == topic) {
                dropped++;
                stats_.messages_dropped++;
                continue; // Drop this message
            }
            temp_queue.push(std::move(msg));
        }
        // Swap back the rebuilt queue
        intel_priority_queue_.swap(temp_queue);
        logInfo("Dropped " + std::to_string(dropped) + " messages from topic: " + topic);
        intel_queue_cv_.notify_all();
    } else {
        std::lock_guard<std::mutex> lock(internal_queue_mutex_);
        size_t dropped = 0;
        std::queue<GenericMessage> temp_queue;
        while (!internal_priority_queue_.empty()) {
            GenericMessage msg = std::move(internal_priority_queue_.front());
            internal_priority_queue_.pop();
            if (dropped < num_messages && msg.topic == topic) {
                dropped++;
                stats_.messages_dropped++;
                continue; // Drop this message
            }
            temp_queue.push(std::move(msg));
        }
        internal_priority_queue_.swap(temp_queue);
        logInfo("Dropped " + std::to_string(dropped) + " messages from topic: " + topic);
        internal_queue_cv_.notify_all();
    }
}

// =============================================================================
// PRIVATE METHODS - INITIALIZATION
// =============================================================================
bool GenericZmqServer::initializeSockets() {
    try {
        logInfo("Starting socket initialization...");
        
        // Validate config first
        if (!config_.validate()) {
            logError("Invalid configuration");
            return false;
        }
        
        logInfo("Config validation passed");
        logInfo("Intel endpoint: " + config_.intel_endpoint);
        logInfo("Internal endpoint: " + config_.internal_endpoint);
        logInfo("Request endpoint: " + config_.request_endpoint);
        
    // Use a single shared context from ZmqManager to avoid multiple contexts
    logInfo("Acquiring shared ZMQ context with " + std::to_string(config_.io_threads) + " IO threads");
    auto shared_ctx = trading::communication::ZmqManager::instance().getContext(config_.io_threads);
    // Store shared_ptr references so lifetime is shared with ZmqManager
    intel_context_ = shared_ctx;
    internal_context_ = shared_ctx;

    // Best-effort set
    try { shared_ctx->set(zmq::ctxopt::blocky, false); } catch(...) {}

        // Create and configure intel publisher socket
        logInfo("Creating intel publisher socket...");
    intel_publisher_socket_ = std::make_unique<zmq::socket_t>(*intel_context_, ZMQ_PUB);
        
        logInfo("Setting intel socket options...");
    // Increase HWM for high-frequency orderbook publishing to reduce backpressure
    // but still prefer non-blocking sends and dropping when necessary.
    intel_publisher_socket_->set(zmq::sockopt::sndhwm, std::max<int>(config_.intel_hwm, 10000));
        intel_publisher_socket_->set(zmq::sockopt::linger, 0);  // Immediate close
        intel_publisher_socket_->set(zmq::sockopt::sndtimeo, 1000);  // Send timeout
        intel_publisher_socket_->set(zmq::sockopt::sndbuf, 0);  // OS buffer size 0 to drop messages when HWM is reached
        
        if (config_.intel_immediate) {
            intel_publisher_socket_->set(zmq::sockopt::immediate, 1);
        }
        
        // Enable conflate for intel socket by default to keep only the latest
        // update if downstream can't keep up (saves bandwidth and CPU).
        try {
            // Respect the configuration flag for conflate (keep only latest when enabled)
            intel_publisher_socket_->set(zmq::sockopt::conflate, config_.intel_conflate ? 1 : 0);
        } catch (...) {}
        
        // Before binding, check manager to avoid duplicate binds
        if (!trading::communication::ZmqManager::instance().isEndpointBound(config_.intel_endpoint)) {
                intel_publisher_socket_->bind(config_.intel_endpoint);
            trading::communication::ZmqManager::instance().registerEndpoint(config_.intel_endpoint);
            logInfo("Intel publisher socket bound successfully to: " + config_.intel_endpoint);
        } else {
            logError("Intel endpoint already bound elsewhere: " + config_.intel_endpoint);
        }
       
        // Create and configure internal publisher socket
        logInfo("Creating internal publisher socket...");
    internal_publisher_socket_ = std::make_unique<zmq::socket_t>(*internal_context_, ZMQ_PUB);
        
        logInfo("Setting internal socket options...");
    internal_publisher_socket_->set(zmq::sockopt::sndhwm, config_.internal_hwm);
        internal_publisher_socket_->set(zmq::sockopt::linger, 0);  // Immediate close
        internal_publisher_socket_->set(zmq::sockopt::sndtimeo, 1000);  // Send timeout
        internal_publisher_socket_->set(zmq::sockopt::sndbuf, 0);  // OS buffer size 0 to drop messages when HWM is reached
        
        if (config_.internal_immediate) {
            internal_publisher_socket_->set(zmq::sockopt::immediate, 1);
        }
        
        if (config_.internal_conflate) {
            internal_publisher_socket_->set(zmq::sockopt::conflate, 1);
        }
        
        if (!trading::communication::ZmqManager::instance().isEndpointBound(config_.internal_endpoint)) {
            internal_publisher_socket_->bind(config_.internal_endpoint);
            trading::communication::ZmqManager::instance().registerEndpoint(config_.internal_endpoint);
            logInfo("Internal publisher socket bound successfully to: " + config_.internal_endpoint);
        } else {
            logError("Internal endpoint already bound elsewhere: " + config_.internal_endpoint);
        }
        
        // Create and configure request socket
        logInfo("Creating request socket...");
    request_socket_ = std::make_unique<zmq::socket_t>(*internal_context_, ZMQ_ROUTER); // trading intent is from python
        
        logInfo("Setting request socket options...");
        request_socket_->set(zmq::sockopt::rcvhwm, config_.request_hwm);
        request_socket_->set(zmq::sockopt::sndhwm, config_.request_hwm);
        request_socket_->set(zmq::sockopt::linger, 0);  // Immediate close
        request_socket_->set(zmq::sockopt::rcvtimeo, 100);  // Receive timeout
        request_socket_->set(zmq::sockopt::sndtimeo, 1000);  // Send timeout
        request_socket_->set(zmq::sockopt::rcvbuf, 0);  // Receive buffer
        
        if (!trading::communication::ZmqManager::instance().isEndpointBound(config_.request_endpoint)) {
            request_socket_->bind(config_.request_endpoint);
            trading::communication::ZmqManager::instance().registerEndpoint(config_.request_endpoint);
            logInfo("Request socket bound successfully to: " + config_.request_endpoint);
        } else {
            logError("Request endpoint already bound elsewhere: " + config_.request_endpoint);
        }
        
        logInfo("All ZeroMQ sockets initialized successfully");
        return true;
        
    } catch (const zmq::error_t& e) {
        logError("GenericZmqServer server error initializing sockets: " + std::string(e.what()) + 
                " (errno: " + std::to_string(e.num()) + ")");
        return false;
    } catch (const std::exception& e) {
        logError("GenericZmqServer server error initializing sockets: " + std::string(e.what()));
        return false;
    } catch (...) {
        logError("GenericZmqServer server unknown error initializing sockets");
        return false;
    }
}

void GenericZmqServer::cleanup() {
    logInfo("Starting cleanup...");
    
    // Close sockets safely with error handling
    if (intel_publisher_socket_) {
        try {
            intel_publisher_socket_->set(zmq::sockopt::linger, 0);
            intel_publisher_socket_->close();
        } catch (const zmq::error_t& e) {
            logError("Error closing intel publisher socket: " + std::string(e.what()));
        }
        intel_publisher_socket_.reset();
        trading::communication::ZmqManager::instance().unregisterEndpoint(config_.intel_endpoint);
    }
    
    if (internal_publisher_socket_) {
        try {
            internal_publisher_socket_->set(zmq::sockopt::linger, 0);
            internal_publisher_socket_->close();
        } catch (const zmq::error_t& e) {
            logError("Error closing internal publisher socket: " + std::string(e.what()));
        }
        internal_publisher_socket_.reset();
        trading::communication::ZmqManager::instance().unregisterEndpoint(config_.internal_endpoint);
    }
    
    if (request_socket_) {
        try {
            request_socket_->set(zmq::sockopt::linger, 0);
            request_socket_->close();
        } catch (const zmq::error_t& e) {
            logError("Error closing request socket: " + std::string(e.what()));
        }
        request_socket_.reset();
        trading::communication::ZmqManager::instance().unregisterEndpoint(config_.request_endpoint);
    }
    
    // Do NOT close the shared contexts - they are owned by ZmqManager.
    // Just release our references so the manager retains ownership and
    // can decide lifecycle globally.
    internal_context_.reset();
    intel_context_.reset();
    // Clear queues
    {
        std::lock_guard<std::mutex> lock1(intel_queue_mutex_);
        std::lock_guard<std::mutex> lock2(internal_queue_mutex_);
        
        // Clear internal queue
        std::queue<GenericMessage> empty_queue;
        internal_priority_queue_.swap(empty_queue);
        
        // Clear intel priority queue
        std::queue<GenericMessage> empty_intel_queue;
        intel_priority_queue_.swap(empty_intel_queue);
        
        // Clear general priority queue
        std::priority_queue<GenericMessage, std::vector<GenericMessage>,
                           std::function<bool(const GenericMessage&, const GenericMessage&)>> empty_priority;
        priority_queue_.swap(empty_priority);
    }
    
    logInfo("ZMQ Server resources cleaned up");
}

// =============================================================================
// PRIVATE METHODS - WORKER THREADS
// =============================================================================

void GenericZmqServer::intelPublisherWorker() {
    // Unconditional print so we see the thread even if logging flags change
    std::cout << "[THREAD] Intel publisher worker thread started id=" << std::this_thread::get_id() << std::endl;
    logInfo("Intel publisher worker thread started");

    while (state_.load() == ServerState::RUNNING) {
        try {
            std::unique_lock<std::mutex> lock(intel_queue_mutex_);

            // Wait for messages or shutdown signal (timed wait to avoid missed notifications)
            // logInfo("Intel worker: About to wait for messages");
            intel_queue_cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                return !intel_priority_queue_.empty() || state_.load() != ServerState::RUNNING;
            });
            // Diagnostic:  
                      
            while (!intel_priority_queue_.empty()) {
                // logInfo("Publishing Intel message: " + intel_priority_queue_.front().topic);
                GenericMessage message = std::move(intel_priority_queue_.front());
                intel_priority_queue_.pop();                
                lock.unlock();

                bool ok = false;
                try {
                    // logInfo("Processing Intel message: " + message.topic);
                    ok = processIntelMessage(message);
                } catch (const std::exception& e) {
                    logError("Error processing message: " + std::string(e.what()));
                }

                if (ok) {
                    stats_.messages_published++;
                    stats_.bytes_sent += message.topic.size() + message.data.size();
                } else {
                    stats_.messages_dropped++;
                }

                lock.lock();
            }

        } catch (const std::exception& e) {
            logError("Error in publisher worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    logInfo("Intel publisher worker thread stopped");
}
// internalPublisherWorker
void GenericZmqServer::internalPublisherWorker() {
    // Unconditional print so we see the thread even if logging flags change
    std::cout << "[THREAD] Internal publisher worker thread started id=" << std::this_thread::get_id() << std::endl;
    logInfo("Internal publisher worker thread started");

    while (state_.load() == ServerState::RUNNING) {
        try {
            std::unique_lock<std::mutex> lock(internal_queue_mutex_);
            // logInfo("Internal worker in main loop, state=" + std::to_string(static_cast<int>(state_.load())));
            // Wait for messages or shutdown signal (timed wait to avoid missed notifications)
            internal_queue_cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                return !internal_priority_queue_.empty() || state_.load() != ServerState::RUNNING;
            });

                    
            while (!internal_priority_queue_.empty()) {
                GenericMessage message = std::move(internal_priority_queue_.front());
                internal_priority_queue_.pop();
                
                lock.unlock();

                bool ok = false;
                try {
                    ok = processInternalMessage(message);
                } catch (const std::exception& e) {
                    logError("Error processing message: " + std::string(e.what()));
                }

                if (ok) {
                    stats_.messages_published++;
                    stats_.bytes_sent += message.topic.size() + message.data.size();
                } else {
                    stats_.messages_dropped++;
                }

                lock.lock();
            }

        } catch (const std::exception& e) {
            logError("Error in publisher worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    logInfo("Internal publisher worker thread stopped");
}

void GenericZmqServer::requestWorker() {
    logInfo("Request worker thread started");
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            // Check socket validity without accessing it
            if (state_.load() != ServerState::RUNNING) {
                break;
            }
            
            // Use a simple sleep-based approach instead of polling
            // This is less efficient but much safer for shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            // Only proceed if we're still running and have a valid socket
            if (state_.load() != ServerState::RUNNING || !request_socket_) {
                continue;
            }
            
            zmq::message_t identity;
            zmq::message_t request;
            
            try {
                // Use non-blocking recv to check for messages
                auto id_result = request_socket_->recv(identity, zmq::recv_flags::dontwait);
                if (!id_result.has_value()) {
                    continue; // No message available
                }
                
                auto req_result = request_socket_->recv(request, zmq::recv_flags::dontwait);
                if (!req_result.has_value()) {
                    logError("Received identity but no request data");
                    continue;
                }
                
                // Process the request
                handleRequest(identity, request);
                stats_.requests_processed++;
                
            } catch (const zmq::error_t& e) {
                if (e.num() == ETERM || e.num() == ENOTSOCK) {
                    logInfo("Socket terminated during recv, exiting worker");
                    break;
                } else if (e.num() == EAGAIN) {
                    // No message available, continue
                    continue;
                } else {
                    logError("Recv error: " + std::string(e.what()) + " (errno: " + std::to_string(e.num()) + ")");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            
        } catch (const std::exception& e) {
            logError("Error in request worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    logInfo("Request worker thread stopped");
}

void GenericZmqServer::monitoringWorker() {
    logInfo("Monitoring worker thread started");
    
    auto last_stats_time = std::chrono::steady_clock::now();
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= config_.stats_interval) {
                // Log statistics
                auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.start_time).count();
                
                logInfo("=== Server Statistics ===");
                logInfo("Uptime: " + std::to_string(uptime) + "s");
                logInfo("State: " + std::to_string(static_cast<int>(state_.load())));
                logInfo("Messages Published: " + std::to_string(stats_.messages_published.load()));
                logInfo("Messages Queued: " + std::to_string(stats_.messages_queued.load()));
                logInfo("Messages Dropped: " + std::to_string(stats_.messages_dropped.load()));
                logInfo("Requests Processed: " + std::to_string(stats_.requests_processed.load()));
                logInfo("Requests Failed: " + std::to_string(stats_.requests_failed.load()));
        logInfo("Send failures: " + std::to_string(stats_.send_failures.load()));
        logInfo("Send EAGAIN count: " + std::to_string(stats_.send_eagain_count.load()));
        logInfo("Send blocking events: " + std::to_string(stats_.send_blocking_events.load()));
                logInfo("Queue Size: " + std::to_string(getQueueSize("intel")) + " (intel), " +
                        std::to_string(getQueueSize("internal")) + " (internal)");
                logInfo("Queue Utilization: " + std::to_string(getQueueUtilization("intel")) + "% (intel), " +
                        std::to_string(getQueueUtilization("internal")) + "% (internal)");
                logInfo("Bytes Sent: " + std::to_string(stats_.bytes_sent.load()));
                logInfo("Bytes Received: " + std::to_string(stats_.bytes_received.load()));
                logInfo("========================");
                
                last_stats_time = now;
            }
            
        } catch (const std::exception& e) {
            logError("Error in monitoring worker: " + std::string(e.what()));
        }
    }
    
    logInfo("Monitoring worker thread stopped");
}

void GenericZmqServer::heartbeatWorker() {
    logInfo("Heartbeat worker thread started");
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            sendHeartbeat();
            std::this_thread::sleep_for(config_.heartbeat_interval);
        } catch (const std::exception& e) {
            logError("Error in heartbeat worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    logInfo("Heartbeat worker thread stopped");
}

// bool GenericZmqServer::processIntelMessage(const GenericMessage& message) {
//     if (!intel_publisher_socket_) {
//         logError("Intel publisher socket not available");
//         return false;
//     }

//     try {
//         // DO NOT lock the intel_queue_mutex_ here — the worker already popped and unlocked.
//         // Use a dedicated send mutex to serialize multipart sends so topic+data don't interleave.
//         if (message.data.empty()) {
//             logError("Empty data in processIntelMessage");
//             return false;
//         }

//         std::string topic = sanitizeTopic(message.topic);
//         if (topic.empty() || message.data.empty()) {
//             logError("Empty topic or data in processIntelMessage");
//             return false;
//         }

//         zmq::message_t topic_msg(topic.data(), topic.size());
//         zmq::message_t data_msg(message.data.data(), message.data.size());

//         std::lock_guard<std::mutex> send_guard(intel_send_mutex_);

//         // Non-blocking send with instrumentation (same approach as internal sends)
//         auto send_start = std::chrono::steady_clock::now();
//         try {
//             auto topic_result = intel_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore | zmq::send_flags::dontwait);
//             if (!topic_result.has_value()) {
//                 stats_.send_eagain_count++;
//                 if (logging_enabled_) logError("EAGAIN on intel topic send for: " + topic);
//                 return false;
//             }
//             auto data_result = intel_publisher_socket_->send(data_msg, zmq::send_flags::dontwait);
//             if (!data_result.has_value()) {
//                 stats_.send_eagain_count++;
//                 if (logging_enabled_) logError("EAGAIN on intel data send for: " + topic);
//                 return false;
//             }
//         } catch (const zmq::error_t& e) {
//             if (e.num() == EAGAIN) {
//                 stats_.send_eagain_count++;
//             } else {
//                 stats_.send_failures++;
//                 stats_.connection_errors++;
//                 logError(std::string("ZMQ error processing intel message: ") + e.what());
//             }
//             return false;
//         }

//         auto send_end = std::chrono::steady_clock::now();
//         auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(send_end - send_start).count();
//         const int kWarnMs = 5; // smaller threshold (ms) to catch slow sends
//         if (duration_ms > kWarnMs) {
//             stats_.send_blocking_events++;
//             if (logging_enabled_) logInfo(std::string("Slow intel send (ms): ") + std::to_string(duration_ms) + " topic=" + topic);
//         } else if (logging_enabled_) {
//             logInfo("Sent MARKET_INTEL message: " + topic + " (send_ms=" + std::to_string(duration_ms) + ")");
//         }

//         return true;
//     } catch (const zmq::error_t& e) {
//         logError("ZMQ error processing message: " + std::string(e.what()));
//         stats_.connection_errors++;
//         return false;
//     } catch (const std::exception& e) {
//         logError("Error processing message: " + std::string(e.what()));
//         return false;
//     }
// }

// bool GenericZmqServer::processInternalMessage(const GenericMessage& message) {
//     if (!internal_publisher_socket_) {
//         logError("Internal publisher socket not available");
//         return false;
//     }

//     try {
//         // Create topic message
//         //sanitize topic
//         std::string topic = sanitizeTopic(message.topic);   
//         zmq::message_t topic_msg(topic.size());
//         memcpy(topic_msg.data(), topic.c_str(), topic.size());

//         // Create metadata
//         std::unordered_map<std::string, std::string> metadata;
//         metadata["correlation_id"] = message.correlation_id;
//         metadata["priority"] = std::to_string(static_cast<int>(message.priority));
//         metadata["timestamp"] = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
//         message.timestamp.time_since_epoch()).count());

//         zmq::message_t data_msg(message.data.size());
//         memcpy(data_msg.data(), message.data.c_str(), message.data.size());

//         // Send messages
//         if (message.topic.empty() || message.data.empty()) {
//             logError("Empty topic or data in processMessage");
//             return false;
//         }        
//         // logInfo("Processing message for topic: " + message.topic);
//         // Diagnostic: measure send time to detect blocking/backpressure
//         auto send_start = std::chrono::steady_clock::now();
//             // Try non-blocking send with a small retry loop for transient backpressure.
//             const int kMaxRetries = 3;
//             int attempts = 0;
//             bool topic_ok = false;
//             bool data_ok = false;
//             for (; attempts < kMaxRetries; ++attempts) {
//                 try {
//                     auto topic_result = internal_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore | zmq::send_flags::dontwait);
//                     if (topic_result.has_value()) topic_ok = true;
//                     else stats_.send_eagain_count++;

//                     auto data_result = internal_publisher_socket_->send(data_msg, zmq::send_flags::dontwait);
//                     if (data_result.has_value()) data_ok = true;
//                     else stats_.send_eagain_count++;

//                     if (topic_ok && data_ok) {
//                         break; // success
//                     }
//                 } catch (const zmq::error_t& e) {
//                     if (e.num() == EAGAIN) {
//                         stats_.send_eagain_count++;
//                     } else {
//                         // non-recoverable error for this send
//                         stats_.send_failures++;
//                         logError(std::string("ZMQ error during internal send attempt: ") + e.what());
//                         return false;
//                     }
//                 }

//                 // If we will retry, record a blocking event and sleep a tiny amount to yield
//                 if (attempts + 1 < kMaxRetries) {
//                     stats_.send_blocking_events++;
//                     std::this_thread::sleep_for(std::chrono::microseconds(200));
//                 }
//             }

//             auto send_end = std::chrono::steady_clock::now();
//             auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(send_end - send_start).count();

//             // Warn if sending took unusually long
//             if (duration_ms > 50) {
//                 stats_.send_blocking_events++;
//                 logError(std::string("Slow internal send detected (ms): ") + std::to_string(duration_ms) + " topic=" + message.topic);
//             } else if (logging_enabled_) {
//                 // logInfo("Sent internal message (send_ms=" + std::to_string(duration_ms) + ")");
//             }

//             if (topic_ok && data_ok) {
//                 return true;
//             }

//             // All retries exhausted - treat as backpressure drop
//             if (logging_enabled_) logInfo("EAGAIN on internal send for topic: " + message.topic + " after attempts=" + std::to_string(attempts));
//             return false;
//         // Fallback - unexpected failure
//         stats_.send_failures++;
//         logError("Failed to publish internal message (unexpected): " + message.topic);
//         return false;
//     } catch (const zmq::error_t& e) {
//         logError("ZMQ error processing message: " + std::string(e.what()));
//         stats_.connection_errors++;
//         return false;
//     } catch (const std::exception& e) {
//         logError("Error processing message: " + std::string(e.what()));
//         return false;
//     }
// }
 
void GenericZmqServer::handleRequest(zmq::message_t& identity, zmq::message_t& request) {
    try {
        // Parse request data
        std::string request_data(static_cast<char*>(request.data()), request.size());
        auto [data, metadata] = deserializeWithMetadata(request_data);
        
        // Extract request type from metadata or data
        std::string request_type = "default";
        auto type_it = metadata.find("type");
        if (type_it != metadata.end()) {
            request_type = type_it->second;
        }
        
        // Find handler
        std::string response_data;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            auto handler_it = request_handlers_.find(request_type);
            
            if (handler_it != request_handlers_.end()) {
                // Call handler
                try {
                    response_data = handler_it->second(data, metadata);
                } catch (const std::exception& e) {
                    sendErrorResponse(identity, "Handler error: " + std::string(e.what()),
                                    metadata.count("request_id") ? metadata["request_id"] : "");
                    return;
                }
            } else {
                sendErrorResponse(identity, "Unknown request type: " + request_type,
                                metadata.count("request_id") ? metadata["request_id"] : "");
                return;
            }
        }
        
        // Send response
        zmq::message_t identity_copy;
        identity_copy.rebuild(identity.size());
        memcpy(identity_copy.data(), identity.data(), identity.size());
        
        zmq::message_t response(response_data.size());
        memcpy(response.data(), response_data.c_str(), response_data.size());
        
        request_socket_->send(identity_copy, zmq::send_flags::sndmore);
        request_socket_->send(response, zmq::send_flags::none);
        
        stats_.bytes_received += request_data.size();
        stats_.bytes_sent += response_data.size();
        
    } catch (const std::exception& e) {
        logError("Error handling request: " + std::string(e.what()));
        sendErrorResponse(identity, "Internal server error");
        stats_.requests_failed++;
    }
}

void GenericZmqServer::sendErrorResponse(zmq::message_t& identity, const std::string& error_msg,
                                       const std::string& request_id) {
    try {
        nlohmann::json error_response;
        error_response["error"] = true;
        error_response["message"] = error_msg;
        if (!request_id.empty()) {
            error_response["request_id"] = request_id;
        }
        error_response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        std::string response_data = error_response.dump();
        
        zmq::message_t identity_copy;
        identity_copy.rebuild(identity.size());
        memcpy(identity_copy.data(), identity.data(), identity.size());
        
        zmq::message_t response(response_data.size());
        memcpy(response.data(), response_data.c_str(), response_data.size());
        
        request_socket_->send(identity_copy, zmq::send_flags::sndmore);
        request_socket_->send(response, zmq::send_flags::none);
        
    } catch (const std::exception& e) {
        logError("Failed to send error response: " + std::string(e.what()));
    }
}

// =============================================================================
// PRIVATE METHODS - UTILITY
// =============================================================================

std::string GenericZmqServer::serializeWithMetadata(const std::string& data,
                                                   const std::unordered_map<std::string, std::string>& metadata) const {
    // Return raw data without JSON wrapping
    return data;
}

// 1. FIX: JSON parsing with proper exception safety
std::pair<std::string, std::unordered_map<std::string, std::string>>
GenericZmqServer::deserializeWithMetadata(const std::string& serialized_data) const {
    // Return raw data without JSON parsing
    return {serialized_data, {}};
}

// 2. FIX: Memory-safe base64 decode
std::string GenericZmqServer::base64_decode_safe(const std::string& input) const {
    if (input.empty()) return {};
    
    // Pre-calculate output size to avoid reallocations
    size_t output_size = (input.size() * 3) / 4;
    std::string result;
    result.reserve(output_size);
    
    static const std::array<int, 256> decode_table = []() {
        std::array<int, 256> table;
        table.fill(-1);
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < chars.size(); ++i) {
            table[static_cast<unsigned char>(chars[i])] = static_cast<int>(i);
        }
        return table;
    }();
    
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (decode_table[c] == -1) break;
        val = (val << 6) + decode_table[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return result;
}


void GenericZmqServer::logError(const std::string& error_msg) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error_msg;
    
    if (logging_enabled_) {
        std::cerr << "[ERROR] GenericZmqServer: " << error_msg << std::endl;
    }
}

void GenericZmqServer::logInfo(const std::string& info_msg) {
    if (logging_enabled_) {
        std::cout << "[INFO] GenericZmqServer: " << info_msg << std::endl;
    }
}

void GenericZmqServer::updateStats(const std::string& operation, size_t bytes) {
    // This method can be extended to track operation-specific statistics
    stats_.bytes_sent += bytes;
}

bool GenericZmqServer::shouldDropMessage(const GenericMessage& message) const {
    // Drop low priority messages when queue is under pressure
    if (message.priority == MessagePriority::LOW || message.priority == MessagePriority::NORMAL) {
        return getQueueUtilization("internal") > 0.9;  // Drop if over 90% utilization
    }
    
    // Never drop high priority or critical messages
    return false;
}

bool GenericZmqServer::reconnectSocket(zmq::socket_t& socket, const std::string& endpoint) {
    try {
        socket.close();
        
        // Recreate socket
        if (&socket == intel_publisher_socket_.get()) {
            intel_publisher_socket_ = std::make_unique<zmq::socket_t>(*intel_context_, ZMQ_PUB);
            intel_publisher_socket_->set(zmq::sockopt::sndhwm, config_.intel_hwm);
            intel_publisher_socket_->set(zmq::sockopt::linger, config_.intel_linger);
        } else if (&socket == internal_publisher_socket_.get()) {
            internal_publisher_socket_ = std::make_unique<zmq::socket_t>(*internal_context_, ZMQ_PUB);
            internal_publisher_socket_->set(zmq::sockopt::sndhwm, config_.internal_hwm);
            internal_publisher_socket_->set(zmq::sockopt::linger, config_.internal_linger);
        } else if (&socket == request_socket_.get()) {
            request_socket_ = std::make_unique<zmq::socket_t>(*intel_context_, ZMQ_ROUTER);
            request_socket_->set(zmq::sockopt::rcvhwm, config_.request_hwm);
            request_socket_->set(zmq::sockopt::linger, 1000);
        }
        
        // Attempt to bind via manager to avoid duplicate bind errors
        if (trading::communication::ZmqManager::instance().registerEndpoint(endpoint)) {
            socket.bind(endpoint);
            logInfo("Socket reconnected to: " + endpoint);
            return true;
        } else {
            logError("Socket reconnect failed - endpoint already bound: " + endpoint);
            return false;
        }
        
    } catch (const zmq::error_t& e) {
        logError("Failed to reconnect socket: " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqServer::isValidTopic(const std::string& topic) {
    if (topic.empty() || topic.length() > 255) {
        return false;
    }
    if (topic.front() == '.' || topic.back() == '.') {
        return false;
    }
    
    // Manual validation - faster than regex
    // for (char c : topic) {
    //     if (!std::isalnum(c) && c != '.' && c != '_' && c != '-') {
    //         return false;
    //     }
    // }
    return true;
}

std::function<bool(const GenericMessage&, const GenericMessage&)>
GenericZmqServer::createPriorityComparator() {
    return [](const GenericMessage& a, const GenericMessage& b) {
        // Higher priority messages should be processed first
        if (a.priority != b.priority) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        }
        // If same priority, older messages first
        return a.timestamp > b.timestamp;
    };
}

/**
 * @brief Calculates the threshold for inline message processing to prevent queue overload.
 * Processes one message inline if the queue exceeds this threshold.
 */
size_t GenericZmqServer::calculateInlineThreshold() const {
    // Minimum 1, up to 1/4 of max_internal_queue_size, capped at 1024
    return std::min<size_t>(std::max<size_t>(1, config_.max_internal_queue_size / 4), 1024);
}

} // namespace communication
} // namespace trading