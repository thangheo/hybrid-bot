#include "risk/interfaces/market_data_subscriber.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <random>
#include <future>

namespace trading {
namespace risk {

constexpr const char* ORDERBOOK_TOPIC_PREFIX = "orderbook.";

MarketDataSubscriber::MarketDataSubscriber(const SubscriberConfig& config)
    : config_(config), currentBatchSize_(config.processingBatchSize) {
    
    // Create processor with default config
    ProcessorConfig procConfig;
    procConfig.maxMessageSize = 256 * 1024;  // 256KB
    procConfig.defaultExchange = "binance";
    processor_ = std::make_unique<MarketDataProcessor>(procConfig);
    
    LOG_INFO("MarketDataSubscriber initialized with {} processing threads, batch size: {}",
             config_.processingThreadCount, config_.processingBatchSize);
}

MarketDataSubscriber::~MarketDataSubscriber() {
    if (running_.load()) {
        shutdown();
    }
}
//set zmq context


bool MarketDataSubscriber::initialize() {
    if (running_.load()) {
        LOG_WARN("MarketDataSubscriber already running");
        return true;
    }
    
    try {
        // Create ZMQ client if not injected
        if (!zmqClient_) {
            trading::communication::ZmqClientConfig zmqConfig;
            // Configure endpoints correctly:
            // - pub_endpoints: endpoints of the server publisher(s) the SUB socket should connect to
            // - req_endpoint: endpoint of the server request/reply socket for RPCs
            zmqConfig.pub_endpoints = { config_.publisherEndpoint };
            zmqConfig.req_endpoint = config_.subscriberEndpoint;
            zmqConfig.auto_reconnect = config_.autoReconnect;
            zmqConfig.reconnect_delay_ms = static_cast<int>(config_.reconnectInterval.count());
            zmqConfig.req_timeout = static_cast<int>(config_.requestTimeout.count());
            zmqConfig.max_retries = config_.maxReconnectAttempts;
            zmqConfig.rcvhwm = 10000;  // High water mark for receive
            zmqConfig.sndhwm = 10000;  // High water mark for send

            LOG_INFO("MarketDataSubscriber ZMQ config: pub_endpoints=[{}], req_endpoint={}",
                     config_.publisherEndpoint, config_.subscriberEndpoint);
            // Use provided ZMQ context or create a new one
            if (!zmqContext_) {
                // zmqContext_ = std::make_shared<zmq::context_t>(1);
                zmqContext_ = std::make_shared<zmq::context_t>(1);
            }
            zmqClient_ = std::make_unique<trading::communication::GenericZmqClient>(zmqConfig, zmqContext_);
        }
        //check if client is already connected
        if (zmqClient_->isConnected()) {
            LOG_WARN("ZMQ client already connected");
        }
        else {
            LOG_INFO("ZMQ client not connected, proceeding to connect");
            // Connect the client
            if (!zmqClient_->connect()) {
                LOG_ERROR("Failed to connect ZMQ client: {}", zmqClient_->getLastError());
                return false;
            }
            LOG_INFO("ZMQ client connected successfully");    
        }
        
        running_.store(true);
        lastConnectionCheck_ = std::chrono::steady_clock::now();
        
        // Start processing threads
        startProcessingThreads();
        
        // Start connection monitor thread
        startConnectionMonitor();
        
        LOG_INFO("MarketDataSubscriber initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize MarketDataSubscriber: {}", e.what());
        return false;
    }
}

void MarketDataSubscriber::shutdown() {
    LOG_INFO("Shutting down MarketDataSubscriber");
    
    running_.store(false);
    
    // Stop processing
    stopProcessingThreads();
    
    // Stop connection monitor
    stopConnectionMonitor();
    
    // Disconnect and cleanup ZMQ client
    if (zmqClient_) {
        try {
            zmqClient_->disconnect();
            LOG_INFO("ZMQ client disconnected");
        } catch (const std::exception& e) {
            LOG_WARN("Exception during ZMQ disconnect: {}", e.what());
        }
    }
    
    // Clear subscriptions
    {
        std::lock_guard<std::mutex> lock(subscriptionMutex_);
        subscribedTopics_.clear();
    }
    
    LOG_INFO("MarketDataSubscriber shutdown complete");
}

bool MarketDataSubscriber::subscribe(const core::Symbol& tradingPair, 
                                    const std::optional<std::string>& exchangeId) {
    
    if (!running_.load() || !zmqClient_) {
        LOG_ERROR("Cannot subscribe - subscriber not running");
        return false;
    }
    
    std::string exchange = exchangeId.value_or("binance");
    std::string topic = createOrderbookTopic(tradingPair, exchange);
    LOG_INFO("Subscribing to market data: {}", topic);
    try {
        bool success = zmqClient_->subscribe(topic, 
            [this](const std::string& topic, const std::string& message,
                   const std::unordered_map<std::string, std::string>& metadata) {
                this->onMessageReceived(topic, message, metadata);
            });
        
        if (success) {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            subscribedTopics_.insert(topic);
            LOG_INFO("Subscribed to market data: {}", topic);
            return true;
        } else {
            LOG_ERROR("Failed to subscribe to topic: {}", topic);
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during subscription to {}: {}", topic, e.what());
        return false;
    }
}

bool MarketDataSubscriber::unsubscribe(const core::Symbol& tradingPair, 
                                      const std::optional<std::string>& exchangeId) {
    
    if (!zmqClient_) {
        return false;
    }
    
    std::string exchange = exchangeId.value_or("binance");
    std::string topic = createOrderbookTopic(tradingPair, exchange);
    
    try {
        bool success = zmqClient_->unsubscribe(topic);
        
        if (success) {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            subscribedTopics_.erase(topic);
            LOG_INFO("Unsubscribed from market data: {}", topic);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during unsubscribe from {}: {}", topic, e.what());
        return false;
    }
}

bool MarketDataSubscriber::subscribePattern(const std::string& topicPattern) {
    if (!running_.load() || !zmqClient_) {
        LOG_ERROR("Cannot subscribe pattern - subscriber not running");
        return false;
    }

    LOG_INFO("Subscribing to pattern: {}", topicPattern);
    try {
        bool success = zmqClient_->subscribe(topicPattern,
            [this](const std::string& topic, const std::string& message,
                   const std::unordered_map<std::string, std::string>& metadata) {
                this->onMessageReceived(topic, message, metadata);
            });

        if (success) {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            subscribedTopics_.insert(topicPattern);
            LOG_INFO("Subscribed to pattern: {}", topicPattern);
            return true;
        } else {
            LOG_ERROR("Failed to subscribe to pattern: {}", topicPattern);
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during pattern subscription {}: {}", topicPattern, e.what());
        return false;
    }
}

bool MarketDataSubscriber::unsubscribePattern(const std::string& topicPattern) {
    if (!zmqClient_) return false;
    try {
        bool success = zmqClient_->unsubscribe(topicPattern);
        if (success) {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            subscribedTopics_.erase(topicPattern);
            LOG_INFO("Unsubscribed pattern: {}", topicPattern);
        }
        return success;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during unsubscribe pattern {}: {}", topicPattern, e.what());
        return false;
    }
}

void MarketDataSubscriber::registerCallback(const MarketDataCallback& callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbacks_.push_back(callback);
    LOG_DEBUG("Registered market data callback (total: {})", callbacks_.size());
}

bool MarketDataSubscriber::isConnected() const {
    return running_.load() && zmqClient_ && zmqClient_->isConnected();
}

void MarketDataSubscriber::setZmqClient(
    std::unique_ptr<trading::communication::GenericZmqClient> client) {
    
    if (running_.load()) {
        LOG_ERROR("Cannot set ZMQ client while subscriber is running");
        return;
    }
    
    zmqClient_ = std::move(client);
    LOG_INFO("Custom ZMQ client injected");
}

// void MarketDataSubscriber::onMessageReceived(
//     const std::string& topic, 
//     const std::string& message,
//     const std::unordered_map<std::string, std::string>& metadata) {
    
//     totalReceived_.fetch_add(1, std::memory_order_relaxed);
    
//     RawMessage rawMsg{topic, message, std::chrono::steady_clock::now()};
    
//     {
//         std::unique_lock<std::mutex> lock(queueMutex_);
//         // LOG_DEBUG("Received message on topic: {}, queue size: {}", topic, inboundQueue_.size());
//         if (config_.enableConflation) {
//             // Use conflation to keep only latest message per topic
//             auto it = conflationMap_.find(topic);
//             if (it != conflationMap_.end()) {
//                 // Replace existing message
//                 it->second = std::move(rawMsg);
//                 totalDropped_.fetch_add(1, std::memory_order_relaxed);
//             } else {
//                 // New topic
//                 conflationMap_.emplace(topic, std::move(rawMsg));
//                 conflationKeyQueue_.push_back(topic);
//                 totalEnqueued_.fetch_add(1, std::memory_order_relaxed);
//                 LOG_DEBUG("Enqueued (conflation) topic='{}', conflation_map_size={}, conflation_key_queue_size={}",
//                           topic, conflationMap_.size(), conflationKeyQueue_.size());
//             }
            
//             // Log warning if conflation map gets too large
//             if (conflationMap_.size() > 1000) {
//                 LOG_WARN("Conflation map size high: {}", conflationMap_.size());
//             }
//         } else {
//             // Use simple queue
//             if (inboundQueue_.size() >= config_.inboundQueueMaxSize) {
//                 totalDropped_.fetch_add(1, std::memory_order_relaxed);
//                 LOG_WARN("Dropping message due to queue overflow (size: {})", 
//                         inboundQueue_.size());
//             } else {
//                 inboundQueue_.push_back(std::move(rawMsg));
//                 totalEnqueued_.fetch_add(1, std::memory_order_relaxed);
//                     LOG_DEBUG("Enqueued (inbound) topic='{}', inbound_queue_size={}", topic, inboundQueue_.size());
//             }
//         }
        
//         queueCv_.notify_one();
//     }
// }
void MarketDataSubscriber::onMessageReceived(
    const std::string& topic, 
    const std::string& message,
    const std::unordered_map<std::string, std::string>& metadata) {
    
    totalReceived_.fetch_add(1, std::memory_order_relaxed);
    // LOG_INFO("MESSAGE RECEIVED: topic='{}', size={}", topic, message.size());
    RawMessage rawMsg{topic, message, std::chrono::steady_clock::now()};
    
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        
        if (config_.enableConflation) {
            auto it = conflationMap_.find(topic);
            if (it != conflationMap_.end()) {
                // Update existing message - don't add to key queue again
                it->second = std::move(rawMsg);
                totalDropped_.fetch_add(1, std::memory_order_relaxed);
                LOG_DEBUG("Updated conflation entry for topic='{}', map_size={}, queue_size={}",
                          topic, conflationMap_.size(), conflationKeyQueue_.size());
            } else {
                // New topic - add to both map and key queue
                conflationMap_.emplace(topic, std::move(rawMsg));
                conflationKeyQueue_.push_back(topic);
                totalEnqueued_.fetch_add(1, std::memory_order_relaxed);
                LOG_DEBUG("Added new conflation entry for topic='{}', map_size={}, queue_size={}",
                          topic, conflationMap_.size(), conflationKeyQueue_.size());
            }
        } else {
            if (inboundQueue_.size() >= config_.inboundQueueMaxSize) {
                totalDropped_.fetch_add(1, std::memory_order_relaxed);
                LOG_WARN("Dropping message due to queue overflow (size: {})", 
                        inboundQueue_.size());
            } else {
                inboundQueue_.push_back(std::move(rawMsg));
                totalEnqueued_.fetch_add(1, std::memory_order_relaxed);
                LOG_DEBUG("Enqueued message for topic='{}', queue_size={}", 
                          topic, inboundQueue_.size());
            }
        }
        
        // Always notify to wake up processing threads
        queueCv_.notify_one();
    }
}

// void MarketDataSubscriber::processQueueLoop() {
//     LOG_DEBUG("Processing thread started");
    
//     while (processingRunning_.load()) {
//         std::vector<RawMessage> batch;
        
//         // Get batch of messages
//         {
//             std::unique_lock<std::mutex> lock(queueMutex_);
//             // Debug: log sizes while holding the lock to get a consistent snapshot
//             LOG_DEBUG("Locked for extraction: conflation_map_size={}, conflation_key_queue_size={}, inbound_queue_size={}",
//                       conflationMap_.size(), conflationKeyQueue_.size(), inboundQueue_.size());
//             // LOG_DEBUG("processQueueLoop checking queue, inbound size: {}, conflation size: {}",
//             //           inboundQueue_.size(), conflationMap_.size());
//             // Wait for messages or timeout
//             if (conflationMap_.empty() && inboundQueue_.empty()) {
//                 queueCv_.wait_for(lock, std::chrono::milliseconds(100));
//                 if (!processingRunning_.load()) break;
//                 if (conflationMap_.empty() && inboundQueue_.empty()) continue;
//             }
            
//             // Extract batch
//             if (config_.enableConflation && !conflationKeyQueue_.empty()) {
//                 size_t batchSize = std::min(currentBatchSize_, conflationKeyQueue_.size());

//                 for (size_t i = 0; i < batchSize; ++i) {
//                     const std::string key = conflationKeyQueue_.front();
//                     LOG_DEBUG("Popping conflation key: {}", key);
//                     conflationKeyQueue_.pop_front();
                    
//                     auto it = conflationMap_.find(key);
//                     if (it != conflationMap_.end()) {
//                         LOG_DEBUG("Draining conflation entry for key: {}", key);
//                         batch.push_back(std::move(it->second));
//                         conflationMap_.erase(it);
//                     } else {
//                         LOG_DEBUG("No conflation entry found for key: {}", key);
//                     }
//                 }
//             } else if (config_.enableConflation && conflationKeyQueue_.empty() && !conflationMap_.empty()) {
//                 // Edge case: conflation map contains entries but the key queue is empty
//                 // (possible if keys were consumed elsewhere). Drain up to currentBatchSize_
//                 // entries directly from the map to ensure progress.
//                 size_t batchSize = std::min(currentBatchSize_, conflationMap_.size());
//                 auto it = conflationMap_.begin();
//                 for (size_t i = 0; i < batchSize && it != conflationMap_.end(); ++i) {
//                     const std::string key = it->first;
//                     LOG_DEBUG("Draining map entry for key (direct drain): {}", key);
//                     batch.push_back(std::move(it->second));
//                     auto eraseIt = it++;
//                     conflationMap_.erase(eraseIt);
//                 }
//             } else if (!inboundQueue_.empty()) {
//                 size_t batchSize = std::min(currentBatchSize_, inboundQueue_.size());
                
//                 for (size_t i = 0; i < batchSize; ++i) {
//                     batch.push_back(std::move(inboundQueue_.front()));
//                     inboundQueue_.pop_front();
//                 }
//             }
//         }
        
//         // Process batch
//         // If conflation is enabled but the key queue is empty while the map contains entries,
//         // drain the conflation map directly into the batch to avoid wasting messages.
//         {
//             std::unique_lock<std::mutex> lock(queueMutex_, std::defer_lock);
//             // We're already outside the lock here; extraction was done above while holding the lock.
//         }

//         LOG_INFO("Processing batch of {} messages (conflation_map_size={}, inbound_size={})",
//                  batch.size(), conflationMap_.size(), inboundQueue_.size());

//         for (auto& rawMsg : batch) {
//             try {
//                 auto start = std::chrono::steady_clock::now();
                
//                 // Process message using processor with timeout to prevent hangs
//                 LOG_INFO("Invoking processor for topic='{}', message_size={}'", rawMsg.topic, rawMsg.message.size());

//                 // Make copies for the async worker to avoid use-after-move
//                 std::string topic_copy = rawMsg.topic;
//                 std::string msg_copy = rawMsg.message;

//                 auto fut = std::async(std::launch::async, [this, topic_copy = std::move(topic_copy), msg_copy = std::move(msg_copy)]() {
//                     return this->processor_->processMessage(topic_copy, msg_copy);
//                 });

//                 // Wait up to 200ms for processing (tunable)
//                 constexpr auto kProcessingTimeout = std::chrono::milliseconds(200);
//                 std::optional<core::MarketData> marketData;
//                 if (fut.wait_for(kProcessingTimeout) == std::future_status::ready) {
//                     marketData = fut.get();
//                     // Debug: log every processed message (helpful during investigation)
//                     if (marketData.has_value()) {
//                         LOG_INFO("Processed market data: {} from topic {}", 
//                                  marketData->tradingPair.toString(), rawMsg.topic);
//                     } else {
//                         LOG_INFO("Processor returned no market data for topic {}", rawMsg.topic);
//                     }
//                 } else {
//                     LOG_ERROR("Processing timeout for topic '{}' after {} ms", rawMsg.topic, kProcessingTimeout.count());
//                     recordError();
//                     // Attempt to detach/ignore the future — it will complete on its own thread
//                     // and its result will be discarded. Continue to next message.
//                     continue;
//                 }
//                 if (marketData.has_value()) {
//                     invokeCallbacks(marketData.value());
//                     totalProcessed_.fetch_add(1, std::memory_order_relaxed);
//                 }
                
//                 // Log slow processing
//                 auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
//                     std::chrono::steady_clock::now() - start).count();
                
//                 if (duration > 50) {  // Log if processing takes > 50ms
//                     LOG_WARN("Slow message processing: {}ms for topic {}", 
//                             duration, rawMsg.topic);
//                 }
                
//             } catch (const std::exception& e) {
//                 LOG_ERROR("Error processing message from topic {}: {}", 
//                          rawMsg.topic, e.what());
//                 recordError();
//             }
//         }
//     }
    
//     LOG_DEBUG("Processing thread stopped");
// }
void MarketDataSubscriber::processQueueLoop() {
    LOG_DEBUG("Processing thread started");
    
    while (processingRunning_.load()) {
        std::vector<RawMessage> batch;
        
        // Get batch of messages
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            
            // Wait for messages or timeout
            if (conflationMap_.empty() && inboundQueue_.empty()) {
                queueCv_.wait_for(lock, std::chrono::milliseconds(100));
                if (!processingRunning_.load()) break;
                if (conflationMap_.empty() && inboundQueue_.empty()) continue;
            }
            
            // Extract batch - YOUR ORIGINAL LOGIC IS FINE
            if (config_.enableConflation && !conflationKeyQueue_.empty()) {
                size_t batchSize = std::min(currentBatchSize_, conflationKeyQueue_.size());

                for (size_t i = 0; i < batchSize; ++i) {
                    const std::string key = conflationKeyQueue_.front();
                    LOG_DEBUG("Popping conflation key: {}", key);
                    conflationKeyQueue_.pop_front();
                    
                    auto it = conflationMap_.find(key);
                    if (it != conflationMap_.end()) {
                        LOG_DEBUG("Draining conflation entry for key: {}", key);
                        batch.push_back(std::move(it->second));
                        conflationMap_.erase(it);
                    }
                }
            } else if (!inboundQueue_.empty()) {
                size_t batchSize = std::min(currentBatchSize_, inboundQueue_.size());
                
                for (size_t i = 0; i < batchSize; ++i) {
                    batch.push_back(std::move(inboundQueue_.front()));
                    inboundQueue_.pop_front();
                }
            }
        }
        
        // Process batch WITHOUT async - this is the key fix
        LOG_INFO("Processing batch of {} messages", batch.size());
        
        for (auto& rawMsg : batch) {
            try {
                auto start = std::chrono::steady_clock::now();
                
                // Direct call without async
                LOG_DEBUG("Invoking processor for topic='{}', message_size={}", 
                         rawMsg.topic, rawMsg.message.size());
                
                auto marketData = processor_->processMessage(rawMsg.topic, rawMsg.message);
                
                if (marketData.has_value()) {
                    LOG_DEBUG("Processed market data: {} from topic {}", 
                             marketData->tradingPair.toString(), rawMsg.topic);
                    invokeCallbacks(marketData.value());
                    totalProcessed_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    LOG_DEBUG("Processor returned no market data for topic {}", rawMsg.topic);
                }
                
                // Log slow processing
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                
                if (duration > 50) {
                    LOG_WARN("Slow message processing: {}ms for topic {}", 
                            duration, rawMsg.topic);
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("Error processing message from topic {}: {}", 
                         rawMsg.topic, e.what());
                recordError();
            }
        }
    }
    
    LOG_DEBUG("Processing thread stopped");
}

void MarketDataSubscriber::connectionMonitorLoop() {
    LOG_DEBUG("Connection monitor thread started");
    
    int reconnectAttempt = 0;
    auto lastRateCheck = std::chrono::steady_clock::now();
    
    while (running_.load()) {
        // Sleep with timeout to allow quick shutdown
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (!running_.load()) break;
        
        // Check connection health
        if (!checkConnection()) {
            handleDisconnection();
            
            if (needsReconnection_.load() && config_.autoReconnect) {
                LOG_INFO("Attempting reconnection (attempt {}/{})", 
                        reconnectAttempt + 1, config_.maxReconnectAttempts);
                
                if (reconnect()) {
                    LOG_INFO("Reconnection successful");
                    needsReconnection_.store(false);
                    reconnectAttempt = 0;
                } else {
                    reconnectAttempt++;
                    if (reconnectAttempt >= config_.maxReconnectAttempts) {
                        LOG_ERROR("Max reconnection attempts reached, giving up");
                        reconnectAttempt = 0;
                        std::this_thread::sleep_for(std::chrono::seconds(60));
                    } else {
                        auto delay = std::chrono::milliseconds(
                            config_.reconnectInterval.count() * (1 << std::min(reconnectAttempt, 5)));
                        std::this_thread::sleep_for(delay);
                    }
                }
            }
        }
        
        // Adaptive batch sizing (every 30 seconds)
        auto now = std::chrono::steady_clock::now();
        if (now - lastRateCheck >= std::chrono::seconds(30)) {
            measureAndAdjustBatchSize();
            lastRateCheck = now;
        }
    }
    
    LOG_DEBUG("Connection monitor thread stopped");
}

bool MarketDataSubscriber::checkConnection() {
    try {
        if (!zmqClient_) return false;
        
        bool connected = zmqClient_->isConnected();
        lastConnectionCheck_ = std::chrono::steady_clock::now();
        return connected;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception checking connection: {}", e.what());
        return false;
    }
}

void MarketDataSubscriber::handleDisconnection() {
    LOG_WARN("Connection lost, scheduling reconnection");
    needsReconnection_.store(true);
    recordError();
}

bool MarketDataSubscriber::reconnect() {
    try {
        if (!zmqClient_) return false;
        
        // Disconnect first
        zmqClient_->disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Reconnect
        if (!zmqClient_->connect()) {
            LOG_ERROR("Reconnection failed: {}", zmqClient_->getLastError());
            return false;
        }
        
        // Resubscribe to all topics
        std::vector<std::string> topicsToResubscribe;
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            topicsToResubscribe.assign(subscribedTopics_.begin(), subscribedTopics_.end());
        }
        
        for (const auto& topic : topicsToResubscribe) {
            if (!zmqClient_->subscribe(topic, 
                [this](const std::string& t, const std::string& m,
                       const std::unordered_map<std::string, std::string>& meta) {
                    this->onMessageReceived(t, m, meta);
                })) {
                LOG_ERROR("Failed to resubscribe to topic: {}", topic);
                return false;
            }
        }
        
        LOG_INFO("Resubscribed to {} topics after reconnection", topicsToResubscribe.size());
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during reconnection: {}", e.what());
        return false;
    }
}

void MarketDataSubscriber::invokeCallbacks(const core::MarketData& data) {
    std::vector<MarketDataCallback> callbacksCopy;
    
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbacksCopy = callbacks_;
    }
    
    for (const auto& callback : callbacksCopy) {
        try {
            callback(data);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in market data callback: {}", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in market data callback");
        }
    }
}

void MarketDataSubscriber::measureAndAdjustBatchSize() {
    try {
        auto now = std::chrono::steady_clock::now();
        uint64_t currentReceived = totalReceived_.load(std::memory_order_relaxed);
        uint64_t lastSnapshot = lastReceivedSnapshot_.load(std::memory_order_relaxed);
        
        double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
            now - lastRateCheck_).count();
        
        if (elapsed <= 0.0) return;
        
        uint64_t delta = (currentReceived >= lastSnapshot) ? (currentReceived - lastSnapshot) : 0;
        size_t messageRate = static_cast<size_t>(delta / elapsed);
        
        size_t optimalBatchSize = calculateOptimalBatchSize(messageRate);
        
        // Apply smoothing
        double currentBatch = static_cast<double>(currentBatchSize_);
        double smoothed = batchSizeSmoothingAlpha_ * currentBatch + 
                         (1.0 - batchSizeSmoothingAlpha_) * static_cast<double>(optimalBatchSize);
        
        size_t newBatchSize = static_cast<size_t>(std::round(smoothed));
        newBatchSize = std::max(config_.minProcessingBatchSize, 
                               std::min(config_.maxProcessingBatchSize, newBatchSize));
        
        if (newBatchSize != currentBatchSize_) {
            LOG_INFO("Adjusting batch size from {} to {} (rate: {} msg/s)", 
                    currentBatchSize_, newBatchSize, messageRate);
            currentBatchSize_ = newBatchSize;
        }
        
        lastReceivedSnapshot_.store(currentReceived, std::memory_order_relaxed);
        lastRateCheck_ = now;
        
    } catch (const std::exception& e) {
        LOG_WARN("Error in batch size adjustment: {}", e.what());
    }
}

SubscriberStats MarketDataSubscriber::getStats() const {
    return SubscriberStats(
        totalReceived_.load(std::memory_order_relaxed),
        totalEnqueued_.load(std::memory_order_relaxed),
        totalProcessed_.load(std::memory_order_relaxed),
        totalDropped_.load(std::memory_order_relaxed),
        connectionErrors_.load(std::memory_order_relaxed),
        lastError_
    );
}

ProcessorErrorStats MarketDataSubscriber::getProcessorStats() const {
    if (processor_) {
        return processor_->getStats();
    }
    return ProcessorErrorStats{};
}

size_t MarketDataSubscriber::calculateOptimalBatchSize(size_t messageRate) const {
    if (messageRate == 0) return config_.minProcessingBatchSize;
    
    // Target: process batches every 10ms
    const double targetBatchDuration = 0.01;  // 10ms
    const double avgProcessingTimePerMessage = 0.0002;  // 0.2ms per message
    
    double optimalBatch = targetBatchDuration / avgProcessingTimePerMessage;
    
    // Scale with message rate for high throughput
    if (messageRate > 1000) {
        optimalBatch *= 1.5;  // Increase batch size for high rates
    }
    
    return static_cast<size_t>(std::max(1.0, optimalBatch));
}

std::string MarketDataSubscriber::createOrderbookTopic(
    const core::Symbol& tradingPair, const std::string& exchange) const {
    
    std::string symbol = tradingPair.toString();
    //real topic is for e.g orderbook.BINANCE.UNI/USDT
    // Use the symbol format produced by tradingPair.toString() (e.g. BTC/USDT)
    // Publisher topics use the slash separator ("BTC/USDT"), so keep it
    // to ensure subscriber topic strings match published topics.
    
    return std::string(ORDERBOOK_TOPIC_PREFIX) + exchange + "." + symbol;
}

void MarketDataSubscriber::startProcessingThreads() {
    processingRunning_.store(true);
    processingThreads_.clear();
    
    for (size_t i = 0; i < config_.processingThreadCount; ++i) {
        processingThreads_.emplace_back([this]() { 
            this->processQueueLoop(); 
        });
    }
    
    LOG_INFO("Started {} processing threads", config_.processingThreadCount);
}

void MarketDataSubscriber::stopProcessingThreads() {
    processingRunning_.store(false);
    
    // Wake up all processing threads
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queueCv_.notify_all();
    }
    
    // Wait for threads to finish
    for (auto& thread : processingThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    processingThreads_.clear();
    LOG_INFO("Stopped all processing threads");
}

void MarketDataSubscriber::startConnectionMonitor() {
    connectionThread_ = std::thread([this]() { 
        this->connectionMonitorLoop(); 
    });
    LOG_DEBUG("Started connection monitor thread");
}

void MarketDataSubscriber::stopConnectionMonitor() {
    if (connectionThread_.joinable()) {
        connectionThread_.join();
        LOG_DEBUG("Stopped connection monitor thread");
    }
}

} // namespace risk
} // namespace trading