#include <risk/interfaces/market_data_interface.hpp>
#include "common/logger.hpp"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <array>
#include "communication/zmq_server_client.hpp"
#include "communication/zmq_manager.hpp"
#include <iostream>
//protobuf
#include "internal_orderbook.pb.h"
#include <google/protobuf/message.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>


using namespace trading::communication;
namespace trading {
namespace risk {

// Helper function for base64 decoding
static std::string base64_decode(const std::string& input) {
    if (input.empty()) return {};
    
    static const std::array<int, 256> decode_table = []() {
        std::array<int, 256> table;
        table.fill(-1);
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < chars.size(); ++i) {
            table[static_cast<unsigned char>(chars[i])] = static_cast<int>(i);
        }
        return table;
    }();
    
    std::string result;
    result.reserve((input.size() * 3) / 4);
    
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

// PriceHistory implementation
void PriceHistory::addPrice(Timestamp timestamp, double price) {
    prices.push_back({timestamp, price});
    while (prices.size() > maxSize) {
        prices.pop_front();
    }
}

std::vector<std::pair<Timestamp, double>> PriceHistory::getRecentPrices(int count) const {
    std::vector<std::pair<Timestamp, double>> result;
    
    int startIdx = std::max(0, static_cast<int>(prices.size()) - count);
    for (int i = startIdx; i < prices.size(); ++i) {
        result.push_back(prices[i]);
    }
    
    return result;
}

// Remove all prices older than the given cutoff timestamp

// MarketDataInterface implementation
MarketDataInterface::MarketDataInterface() : DataInterface() {
    // LOG_INFO("MarketDataInterface constructor");
}
MarketDataInterface::MarketDataInterface(const RiskConfig& config) : MarketDataInterface() {
    zmqSubscriberEndpoint_ = config.zmqSubscriberEndpoint;
    zmqPublisherEndpoint_ = config.zmqPublisherEndpoint;
    cacheValidityDuration_ = std::chrono::milliseconds(config.marketDataCacheDurationMs);
    auto DEFAULT_EXCHANGE = config.defaultExchange;
    // config binance api key secret here later
    //get api key from environment variable
    std::string apiKey = std::getenv("BINANCE_API_KEY") ? std::getenv("BINANCE_API_KEY") : "";
    std::string apiSecret = std::getenv("BINANCE_API_SECRET") ? std::getenv("BINANCE_API_SECRET") : "";
    if(apiKey.empty() || apiSecret.empty()) {
        LOG_ERROR("Binance API key or secret not set in environment variables");
    }
    m_binanceClient.setApiKey(apiKey, apiSecret);

    LOG_INFO("MarketDataInterface configured with subscriber: " + zmqSubscriberEndpoint_ + 
             ", publisher: " + zmqPublisherEndpoint_ + 
             ", cache duration: " + std::to_string(cacheValidityDuration_.count()) + "ms" +
             ", default exchange: " + DEFAULT_EXCHANGE);
}

bool MarketDataInterface::initialize() {
    if (initialized_.load()) {
        return true;
    }
    
    try {
        if(!zmqClient_){
            // Initialize ZeroMQ client
            ZmqClientConfig zmqConfig;
            // zmqConfig.server_pub_endpoint = zmqSubscriberEndpoint_;
            // zmqConfig.server_req_endpoint = zmqPublisherEndpoint_;
            // zmqConfig.auto_reconnect = true;
            // zmqConfig.reconnect_interval = std::chrono::milliseconds(5000);
            // zmqConfig.request_timeout = std::chrono::milliseconds(10000);
            // zmqConfig.io_threads = 2;
            // zmqConfig.subscriber_conflate = true;
            // zmqConfig.

            //only create if not already set via setter
            
            zmqClient_ = std::make_unique<GenericZmqClient>(zmqConfig);
        }
        // Start ZeroMQ client BEFORE subscribing so subscriptions succeed
        if (!zmqClient_->connect()) {
            LOG_ERROR("Failed to connect ZeroMQ client");
            return false;
        }
        // Record the time of successful connect
        lastConnectionCheck_ = std::chrono::steady_clock::now();

        RequestHandler requestHandler = 
            [this](const std::string& request_data, 
                   const std::unordered_map<std::string, std::string>& metadata) -> std::string {
            // Handle incoming requests if needed
            return "ACK"; // Simple acknowledgment for now
        };
        // Subscribe to orderbook topics
        zmqClient_->subscribe("orderbook.", 
            [this](const std::string& topic, const std::string& message, const std::unordered_map<std::string, std::string>& metadata) {
                // Enqueue-only: push raw message to inbound queue or conflation map and wake processing thread
                totalReceived_.fetch_add(1, std::memory_order_relaxed);
                MarketDataInterface::RawMessage raw{topic, message, std::chrono::steady_clock::now()};
                {
                    std::unique_lock<std::mutex> qlock(this->inboundQueueMutex_);
                    if (enableConflation_) {
                        // Keep only the latest message per topic to avoid queue blowup
                        auto it = this->inboundConflationMap_.find(topic);
                        if (it != this->inboundConflationMap_.end()) {
                            // Replace existing entry (in-place update)
                            it->second = std::move(raw);
                            // Count as received but not additional enqueue
                            this->totalDropped_.fetch_add(1, std::memory_order_relaxed); // logically dropped old
                        } else {
                            // New key
                            this->inboundConflationMap_.emplace(topic, std::move(raw));
                            this->inboundConflationKeyQueue_.push_back(topic);
                            this->totalEnqueued_.fetch_add(1, std::memory_order_relaxed);
                        }
                        // Diagnostic: log when map grows large
                        size_t msz = this->inboundConflationMap_.size();
                        if (msz % 1000 == 0 || msz > 2000) {
                            LOG_WARN("MarketDataInterface conflation map size high: {}", msz);
                        }
                        this->inboundQueueCv_.notify_one();
                    } else {
                        // Legacy behavior: simple deque
                        if (this->inboundQueue_.size() >= this->inboundQueueMaxSize_) {
                            // Drop the message to avoid unbounded growth
                            this->totalDropped_.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            this->inboundQueue_.push_back(std::move(raw));
                            this->totalEnqueued_.fetch_add(1, std::memory_order_relaxed);
                            // Diagnostic: log when queue is growing large
                            size_t qsz = this->inboundQueue_.size();
                            if (qsz % 1000 == 0 || qsz > 2000) {
                                LOG_WARN("MarketDataInterface inbound queue size high: {}", qsz);
                            }
                            this->inboundQueueCv_.notify_one();
                        }
                    }
                }
            });

        // Record the wildcard subscription so it can be reapplied on reconnect
        {
            std::lock_guard<std::mutex> sLock(subscriptionMutex_);
            subscribedTopics_.insert("orderbook.");
        }
        
        initialized_.store(true);
        running_.store(true);
        
        // Start background reconnection/monitor thread
        try {
            reconnectionThread_ = std::thread([this]() { this->connectionMonitorLoop(); });
            LOG_INFO("MarketDataInterface", "Started connection monitor thread");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start connection monitor thread: {}", e.what());
        }

        // Start multiple processing threads for inbound messages
        try {
            processingRunning_.store(true);
            processingThreads_.clear();
            for (size_t i = 0; i < processingThreadCount_; ++i) {
                processingThreads_.emplace_back([this]() { this->processQueueLoop(); });
            }
            LOG_INFO("MarketDataInterface", "Started {} market data processing threads", processingThreadCount_);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start processing threads: {}", e.what());
            processingRunning_.store(false);
        }

        LOG_INFO("MarketDataInterface initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize MarketDataInterface: " + std::string(e.what()));
        return false;
    }
}

// Process inbound queue: pop raw messages and call existing processing logic
void MarketDataInterface::processQueueLoop() {
    while (processingRunning_.load()) {
        // Batch processing: drain up to processingBatchSize_ unique keys/messages
        std::vector<RawMessage> batch;
        {
            std::unique_lock<std::mutex> lock(inboundQueueMutex_);
            // Wait until notified or check periodically to allow shutdown
            if (inboundConflationMap_.empty() && inboundQueue_.empty()) {
                inboundQueueCv_.wait_for(lock, std::chrono::milliseconds(500));
                if (!processingRunning_.load()) break;
                if (inboundConflationMap_.empty() && inboundQueue_.empty()) continue;
            }

            if (enableConflation_ && !inboundConflationKeyQueue_.empty()) {
                size_t to_take = std::min(processingBatchSize_, inboundConflationKeyQueue_.size());
                for (size_t i = 0; i < to_take; ++i) {
                    const std::string key = std::move(inboundConflationKeyQueue_.front());
                    inboundConflationKeyQueue_.pop_front();
                    auto it = inboundConflationMap_.find(key);
                    if (it != inboundConflationMap_.end()) {
                        batch.push_back(std::move(it->second));
                        inboundConflationMap_.erase(it);
                    }
                }
            } else if (!inboundQueue_.empty()) {
                // Legacy single-message deque fallback
                size_t to_take = std::min(processingBatchSize_, inboundQueue_.size());
                for (size_t i = 0; i < to_take; ++i) {
                    batch.push_back(std::move(inboundQueue_.front()));
                    inboundQueue_.pop_front();
                }
            }
        }

        for (auto& item : batch) {
            try {
                auto start = std::chrono::steady_clock::now();
                processSyntheticOrderBook_protobuf(item.topic, item.message);
                totalProcessed_.fetch_add(1, std::memory_order_relaxed);
                auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (dur > 20) {
                    LOG_WARN("Slow processing of market message: {} ms (totalProcessed={})", dur, totalProcessed_.load());
                }
            } catch (const std::exception& e) {
                errorStats_.dataErrors++;
                LOG_ERROR("Error processing queued market data: {}", e.what());
            }
        }
    }
}

void MarketDataInterface::shutdown() {
    LOG_INFO("Shutting down MarketDataInterface");

    running_.store(false);

    // Wake up reconnection/monitor thread so it can exit promptly
    reconnectionCv_.notify_all();

    // Stop processing threads
    processingRunning_.store(false);
    {
        std::lock_guard<std::mutex> lock(inboundQueueMutex_);
        inboundQueueCv_.notify_all();
    }
    for (auto& t : processingThreads_) {
        if (t.joinable()) t.join();
    }
    processingThreads_.clear();

    // Stop and destroy ZMQ client (this joins subscriber thread internally)
    if (zmqClient_) {
        try {
            zmqClient_->disconnect();
            zmqClient_.reset();
            LOG_INFO("ZMQ client disconnected and destroyed");
        } catch (const std::exception& e) {
            LOG_WARN("Exception during ZMQ client shutdown: {}", e.what());
        }
    }

    // Wait for reconnection/monitor thread to exit
    if (reconnectionThread_.joinable()) {
        reconnectionThread_.join();
        LOG_INFO("Reconnection/monitor thread stopped");
    }

    initialized_.store(false);
    LOG_INFO("MarketDataInterface shutdown complete");
}


void MarketDataInterface::processSyntheticOrderBook_json(const std::string& topic, const std::string& message) {
    // Early memory pressure check
    static std::atomic<size_t> activeProcessingCount{0};
    const size_t MAX_CONCURRENT_PROCESSING = 50; // Adjust based on your system
    
    if (activeProcessingCount.load() >= MAX_CONCURRENT_PROCESSING) {
        errorStats_.dataErrors++;
        LOG_WARN("Dropping message due to high memory pressure (active processing: {})", 
                activeProcessingCount.load());
        return;
    }
    
    // RAII counter for active processing
    struct ProcessingGuard {
        std::atomic<size_t>& counter;
        ProcessingGuard(std::atomic<size_t>& c) : counter(c) { counter++; }
        ~ProcessingGuard() { counter--; }
    };
    ProcessingGuard guard(activeProcessingCount);
    
    try {
        // More aggressive size check with headroom for parsing
        constexpr size_t PARSING_OVERHEAD_FACTOR = 3; // JSON parsing can use 2-3x memory
        if (message.size() > maxMessageSize_ / PARSING_OVERHEAD_FACTOR) {
            errorStats_.dataErrors++;
            LOG_WARN("Dropping oversized message considering parsing overhead (size={} bytes) for topic={}", 
                    message.size(), topic);
            return;
        }
        
        // Quick topic validation - avoid string operations if possible
        if (topic.size() < 10 || topic.compare(0, 10, "orderbook.") != 0) {
            return; // Not a synthetic orderbook message
        }

        if (message.empty()) {
            LOG_ERROR("Invalid JSON message: empty payload");
            return;
        }

        // Use stack-allocated buffer for small messages to reduce heap pressure
        constexpr size_t STACK_BUFFER_SIZE = 4096;
        bool useHeapParsing = message.size() > STACK_BUFFER_SIZE;
        
        nlohmann::json json_msg;
        try {
            if (useHeapParsing) {
                // For large messages, try streaming parser to reduce memory peaks
                std::istringstream stream(message);
                stream >> json_msg;
            } else {
                json_msg = nlohmann::json::parse(message);
            }
        } catch (const std::bad_alloc& e) {
            errorStats_.dataErrors++;
            LOG_ERROR("OOM while parsing outer JSON (size={}): {}", message.size(), e.what());
            return;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse outer JSON: {}", e.what());
            errorStats_.dataErrors++;
            return;
        }

        // Handle embedded JSON data with better memory management
        if (json_msg.contains("data") && json_msg["data"].is_string()) {
            try {
                const auto& inner_ref = json_msg["data"].get_ref<const std::string&>();
                if (!inner_ref.empty() && inner_ref.front() == '{') {
                    // Apply same size check with overhead consideration
                    if (inner_ref.size() <= maxMessageSize_ / PARSING_OVERHEAD_FACTOR) {
                        if (inner_ref.size() > STACK_BUFFER_SIZE) {
                            std::istringstream inner_stream(inner_ref);
                            inner_stream >> json_msg;
                        } else {
                            json_msg = nlohmann::json::parse(inner_ref);
                        }
                    } else {
                        LOG_WARN("Inner 'data' payload too large ({} bytes), skipping", inner_ref.size());
                    }
                }
            } catch (const std::bad_alloc& e) {
                errorStats_.dataErrors++;
                LOG_ERROR("OOM while parsing inner 'data' JSON: {}", e.what());
                return; // Don't continue if we can't parse the main data
            } catch (const std::exception& e) {
                LOG_WARN("Failed to parse inner 'data' JSON: {}", e.what());
                // Continue with outer json_msg
            }
        }

        // Extract exchange and symbol with minimal string operations
        std::string exchange;
        std::string symbol;
        
        // Try to get from JSON first (most efficient)
        if (json_msg.contains("exchange") && json_msg["exchange"].is_string()) {
            exchange = json_msg["exchange"].get<std::string>();
        }
        if (json_msg.contains("symbol") && json_msg["symbol"].is_string()) {
            symbol = json_msg["symbol"].get<std::string>();
        }

        // Fallback to topic parsing with pre-allocated string
        if (exchange.empty() || symbol.empty()) {
            // Use string_view for better performance where possible
            std::string_view topic_view(topic);
            auto first_dot = topic_view.find('.', 10); // Skip "orderbook."
            if (first_dot != std::string_view::npos) {
                auto second_dot = topic_view.find('.', first_dot + 1);
                if (second_dot != std::string_view::npos) {
                    if (exchange.empty()) {
                        exchange = std::string(topic_view.substr(first_dot + 1, second_dot - first_dot - 1));
                    }
                    if (symbol.empty()) {
                        symbol = std::string(topic_view.substr(second_dot + 1));
                        // Convert '_' to '/' in-place
                        std::replace(symbol.begin(), symbol.end(), '_', '/');
                    }
                }
            }
        }

        if (exchange.empty() || symbol.empty()) {
            LOG_ERROR("Missing exchange or symbol (topic={})", topic);
            return;
        }

        // Create MarketData on stack to avoid heap allocation
        MarketData data;
        data.exchangeId = core::ExchangeId::fromString(exchange);
        data.tradingPair = core::Symbol::fromString(symbol);
        data.timestamp = std::chrono::system_clock::now();

        // Optimized price extraction with minimal allocations
        auto extractPriceFromLevel = [](const nlohmann::json& level) -> std::optional<double> {
            try {
                if (level.is_array() && level.size() >= 1) {
                    const auto& price_elem = level[0];
                    if (price_elem.is_string()) {
                        const auto& price_str = price_elem.get_ref<const std::string&>();
                        return std::stod(price_str);
                    } else if (price_elem.is_number()) {
                        return price_elem.get<double>();
                    }
                }
            } catch (...) {
                // Silently handle conversion errors
            }
            return std::nullopt;
        };

        // Extract best bid/ask prices
        std::optional<double> bestBid;
        std::optional<double> bestAsk;

        // Check for bids
        if (json_msg.contains("bids") && json_msg["bids"].is_array() && !json_msg["bids"].empty()) {
            bestBid = extractPriceFromLevel(json_msg["bids"][0]);
        } else if (json_msg.contains("b") && json_msg["b"].is_array() && !json_msg["b"].empty()) {
            bestBid = extractPriceFromLevel(json_msg["b"][0]);
        }

        // Check for asks
        if (json_msg.contains("asks") && json_msg["asks"].is_array() && !json_msg["asks"].empty()) {
            bestAsk = extractPriceFromLevel(json_msg["asks"][0]);
        } else if (json_msg.contains("a") && json_msg["a"].is_array() && !json_msg["a"].empty()) {
            bestAsk = extractPriceFromLevel(json_msg["a"][0]);
        }

        // Set prices on data object
        if (bestBid.has_value()) data.bid = *bestBid;
        if (bestAsk.has_value()) data.ask = *bestAsk;
        if (bestBid.has_value() && bestAsk.has_value()) {
            data.price = (data.bid + data.ask) / 2.0;
        } else if (json_msg.contains("price") && json_msg["price"].is_number()) {
            data.price = json_msg["price"].get<double>();
        }

        // Create cache key once
        std::string cacheKey = createCacheKey(data.exchangeId, data.tradingPair);

        // If we don't have a valid price yet, try fallbacks and mark as invalid in cache
        bool validPrice = (data.price > 0.0);
        if (!validPrice) {
            // try last trade price if provided
            if (data.lastTradePrice > 0.0) {
                data.price = data.lastTradePrice;
                validPrice = true;
            }
        }

        // Update cache with minimal lock time; set isValid based on price availability
        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            marketDataCache_[cacheKey] = MarketDataCacheEntry{
                std::move(data), std::chrono::steady_clock::now(), validPrice
            };
        }

        // Update price history with error handling
        {
            std::unique_lock<std::shared_mutex> hlock(historyMutex_);
            if (data.price > 0.0) {
                try {
                    auto& history = priceHistories_[cacheKey];
                    history.addPrice(data.timestamp, data.price);

                    static size_t max_history_size = history.maxSize;
                    // Request one more than the max to detect overflow without a direct size() call
                    auto recent_overflow_check = history.getRecentPrices(static_cast<int>(max_history_size + 1));
                    if (recent_overflow_check.size() > max_history_size) {
                        // Keep 80% after trim
                        size_t target = static_cast<size_t>(max_history_size * 0.8);
                        auto trimmed = history.getRecentPrices(static_cast<int>(target));
                        PriceHistory newHist;
                        for (const auto& p : trimmed) {
                            newHist.addPrice(p.first, p.second);
                        }
                        // Replace the history with the trimmed version
                        priceHistories_[cacheKey] = std::move(newHist);
                    }
                } catch (const std::bad_alloc& e) {
                    errorStats_.dataErrors++;
                    LOG_ERROR("OOM while adding price to history: {}", e.what());
                } catch (const std::exception& e) {
                    errorStats_.dataErrors++;
                    LOG_ERROR("Exception while adding price to history: {}", e.what());
                }
            }
        }

        // Invoke callbacks with exception safety
        std::vector<std::function<void(const MarketData&)>> callbacksCopy;
        {
            std::lock_guard<std::mutex> cbLock(callbackMutex_);
            callbacksCopy = marketDataCallbacks_;
        }

        for (const auto& cb : callbacksCopy) {
            try {
                cb(data);
            } catch (const std::bad_alloc& e) {
                errorStats_.dataErrors++;
                LOG_ERROR("Callback OOM: {}", e.what());
                // Consider breaking the loop on OOM to prevent cascade failures
                break;
            } catch (const std::exception& e) {
                LOG_ERROR("Callback exception: {}", e.what());
            } catch (...) {
                LOG_ERROR("Callback unknown exception");
            }
        }

    } catch (const std::bad_alloc& e) {
        errorStats_.dataErrors++;
        LOG_ERROR("Critical OOM in processSyntheticOrderBook: {}", e.what());
        
        // Emergency cleanup - try to free some memory
        try {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_, std::defer_lock);
            if (lock.try_lock()) {
                // Remove oldest 10% of cache entries
                if (marketDataCache_.size() > 100) {
                    auto cutoff = std::chrono::steady_clock::now() - std::chrono::minutes(5);
                    for (auto it = marketDataCache_.begin(); it != marketDataCache_.end();) {
                        if (it->second.lastUpdate < cutoff) {
                            it = marketDataCache_.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
        } catch (...) {
            // If emergency cleanup fails, just log and continue
            LOG_ERROR("Emergency cleanup failed during OOM recovery");
        }
    } catch (const std::exception& e) {
        errorStats_.dataErrors++;
        LOG_ERROR("Exception in processSyntheticOrderBook: {}", e.what());
    } catch (...) {
        errorStats_.dataErrors++;
        LOG_ERROR("Unknown exception in processSyntheticOrderBook");
    }
}
void MarketDataInterface::processSyntheticOrderBook_protobuf(const std::string& topic, const std::string& message) {
    // Early memory pressure check
    static std::atomic<size_t> activeProcessingCount{0};
    const size_t MAX_CONCURRENT_PROCESSING = 50;
    
    if (activeProcessingCount.load() >= MAX_CONCURRENT_PROCESSING) {
        errorStats_.dataErrors++;
        LOG_WARN("Dropping message due to high memory pressure (active processing: {})", 
                activeProcessingCount.load());
        return;
    }
    
    // RAII counter for active processing
    struct ProcessingGuard {
        std::atomic<size_t>& counter;
        ProcessingGuard(std::atomic<size_t>& c) : counter(c) { counter++; }
        ~ProcessingGuard() { counter--; }
    };
    ProcessingGuard guard(activeProcessingCount);
    
    try {
        // Check message size
        if (message.size() > maxMessageSize_) {
            errorStats_.dataErrors++;
            LOG_WARN("Dropping oversized protobuf message (size={} bytes) for topic={}", 
                    message.size(), topic);
            return;
        }
        
        // Quick topic validation
        if (topic.size() < 10 || topic.compare(0, 10, "orderbook.") != 0) {
            return; // Not a synthetic orderbook message
        }

        if (message.empty()) {
            LOG_ERROR("Empty protobuf message for topic: {}", topic);
            errorStats_.dataErrors++;
            return;
        }

        // Parse protobuf directly - much simpler now!
        orderbook::OrderbookMessage orderbook_msg;
        
        try {
            // Use CodedInputStream for better memory control and performance
            google::protobuf::io::ArrayInputStream array_input(
                message.data(), 
                static_cast<int>(message.size())
            );
            google::protobuf::io::CodedInputStream coded_input(&array_input);
            
            // Set limits to prevent excessive memory usage
            coded_input.SetTotalBytesLimit(maxMessageSize_);
            
            if (!orderbook_msg.ParseFromCodedStream(&coded_input)) {
                LOG_ERROR("Failed to parse protobuf message for topic: {} (size: {} bytes)", 
                         topic, message.size());

                // If message is small (likely malformed), log a hex dump to help diagnosis
                if (message.size() <= 128) {
                    std::stringstream hex_dump;
                    hex_dump << std::hex << std::setfill('0');
                    for (size_t i = 0; i < std::min<size_t>(message.size(), 64); ++i) {
                        hex_dump << std::setw(2) << (static_cast<int>(static_cast<unsigned char>(message[i])) & 0xff) << " ";
                    }
                    LOG_ERROR("Protobuf small-payload hex dump ({} bytes): {}", message.size(), hex_dump.str());
                } else {
                    LOG_DEBUG("Protobuf parse failure for large payload (size={})", message.size());
                }

                errorStats_.dataErrors++;
                return;
            }
            
        } catch (const google::protobuf::FatalException& e) {
            LOG_ERROR("Protobuf fatal exception for topic {}: {}", topic, e.what());
            errorStats_.dataErrors++;
            return;
        } catch (const std::bad_alloc& e) {
            errorStats_.dataErrors++;
            LOG_ERROR("OOM while parsing protobuf (size={}) for topic {}: {}", 
                     message.size(), topic, e.what());
            return;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception parsing protobuf for topic {}: {}", topic, e.what());
            errorStats_.dataErrors++;
            return;
        }

        // Verify we have the main data
        if (!orderbook_msg.has_data()) {
            LOG_ERROR("Protobuf message missing data field for topic: {}", topic);
            errorStats_.dataErrors++;
            return;
        }

        const auto& data = orderbook_msg.data();

        // Extract exchange and symbol from protobuf
        std::string exchange = data.exchange();
        std::string symbol = data.symbol();
        
        // Fallback to topic parsing if protobuf fields are empty
        if (exchange.empty() || symbol.empty()) {
            std::string_view topic_view(topic);
            auto first_dot = topic_view.find('.', 10); // Skip "orderbook."
            if (first_dot != std::string_view::npos) {
                auto second_dot = topic_view.find('.', first_dot + 1);
                if (second_dot != std::string_view::npos) {
                    if (exchange.empty()) {
                        exchange = std::string(topic_view.substr(first_dot + 1, second_dot - first_dot - 1));
                    }
                    if (symbol.empty()) {
                        symbol = std::string(topic_view.substr(second_dot + 1));
                        // Convert '_' to '/' for symbol format
                        std::replace(symbol.begin(), symbol.end(), '_', '/');
                    }
                }
            }
        }

        if (exchange.empty() || symbol.empty()) {
            LOG_ERROR("Missing exchange or symbol (topic={}, exchange='{}', symbol='{}')", 
                     topic, exchange, symbol);
            errorStats_.dataErrors++;
            return;
        }

        // Create MarketData object
        MarketData marketData;
        marketData.exchangeId = core::ExchangeId::fromString(exchange);
        marketData.tradingPair = core::Symbol::fromString(symbol);
        marketData.timestamp = std::chrono::system_clock::now();

        // Extract pricing data from protobuf
        std::optional<double> bestBid;
        std::optional<double> bestAsk;

        // Check bids array first, then fallback to single bid field
        if (data.bids_size() > 0) {
            const auto& first_bid = data.bids(0);
            bestBid = first_bid.price();
        } else if (data.bid() != 0.0) {
            bestBid = data.bid();
        }

        // Check asks array first, then fallback to single ask field
        if (data.asks_size() > 0) {
            const auto& first_ask = data.asks(0);
            bestAsk = first_ask.price();
        } else if (data.ask() != 0.0) {
            bestAsk = data.ask();
        }

        // Set bid/ask on market data
        if (bestBid.has_value()) {
            marketData.bid = *bestBid;
        }
        if (bestAsk.has_value()) {
            marketData.ask = *bestAsk;
        }
        
        // Calculate mid price or use provided price
        if (bestBid.has_value() && bestAsk.has_value()) {
            marketData.price = (marketData.bid + marketData.ask) / 2.0;
        } else if (data.price() != 0.0) {
            marketData.price = data.price();
        } else {
            LOG_WARN("No price data available for {}.{}", exchange, symbol);
        }

        // Set additional market data fields
        marketData.volume = data.volume();
        marketData.high24h = data.high24h();
        marketData.low24h = data.low24h();
        
        // Optional: Store sequence number if your MarketData supports it
        // marketData.sequence = data.sequence();

        // Create cache key
        std::string cacheKey = createCacheKey(marketData.exchangeId, marketData.tradingPair);

        // Determine validity of this entry based on price
        bool validEntry = marketData.price > 0.0;
        if (!validEntry && marketData.lastTradePrice > 0.0) {
            marketData.price = marketData.lastTradePrice;
            validEntry = true;
        }

        // Update cache atomically and mark validity
        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            marketDataCache_[cacheKey] = MarketDataCacheEntry{
                std::move(marketData), std::chrono::steady_clock::now(), validEntry
            };
        }

        // Update price history with memory management
        if (marketData.price > 0.0) {
            std::unique_lock<std::shared_mutex> hlock(historyMutex_);
            try {
                auto& history = priceHistories_[cacheKey];
                history.addPrice(marketData.timestamp, marketData.price);

                // Trim history if it gets too large
                static size_t max_history_size = history.maxSize;
                auto recent_overflow_check = history.getRecentPrices(static_cast<int>(max_history_size + 1));
                if (recent_overflow_check.size() > max_history_size) {
                    size_t target = static_cast<size_t>(max_history_size * 0.8);
                    auto trimmed = history.getRecentPrices(static_cast<int>(target));
                    PriceHistory newHist;
                    for (const auto& p : trimmed) {
                        newHist.addPrice(p.first, p.second);
                    }
                    priceHistories_[cacheKey] = std::move(newHist);
                }
            } catch (const std::bad_alloc& e) {
                errorStats_.dataErrors++;
                LOG_ERROR("OOM while updating price history: {}", e.what());
            } catch (const std::exception& e) {
                errorStats_.dataErrors++;
                LOG_ERROR("Exception while updating price history: {}", e.what());
            }
        }

        // Trigger callbacks safely
        std::vector<std::function<void(const MarketData&)>> callbacksCopy;
        {
            std::lock_guard<std::mutex> cbLock(callbackMutex_);
            callbacksCopy = marketDataCallbacks_;
        }

        for (const auto& callback : callbacksCopy) {
            try {
                callback(marketData);
            } catch (const std::bad_alloc& e) {
                errorStats_.dataErrors++;
                LOG_ERROR("Callback OOM: {}", e.what());
                break; // Stop processing callbacks on OOM
            } catch (const std::exception& e) {
                LOG_ERROR("Callback exception: {}", e.what());
            } catch (...) {
                LOG_ERROR("Unknown callback exception");
            }
        }

        // Optional: Log successful processing at debug level
        LOG_DEBUG("Successfully processed orderbook for {}.{} (price={}, bid={}, ask={})", 
                 exchange, symbol, marketData.price, 
                 marketData.bid, marketData.ask);

    } catch (const std::bad_alloc& e) {
        errorStats_.dataErrors++;
        LOG_ERROR("Critical OOM in processSyntheticOrderBook_protobuf: {}", e.what());
        
        // Emergency cache cleanup
        try {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_, std::defer_lock);
            if (lock.try_lock()) {
                if (marketDataCache_.size() > 100) {
                    auto cutoff = std::chrono::steady_clock::now() - std::chrono::minutes(5);
                    for (auto it = marketDataCache_.begin(); it != marketDataCache_.end();) {
                        if (it->second.lastUpdate < cutoff) {
                            it = marketDataCache_.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
        } catch (...) {
            LOG_ERROR("Emergency cleanup failed during OOM recovery");
        }
    } catch (const std::exception& e) {
        errorStats_.dataErrors++;
        LOG_ERROR("Exception in processSyntheticOrderBook_protobuf: {}", e.what());
    } catch (...) {
        errorStats_.dataErrors++;
        LOG_ERROR("Unknown exception in processSyntheticOrderBook_protobuf");
    }
}

void MarketDataInterface::performPeriodicCleanup() {
    try {
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - std::chrono::hours(1); // Configurable retention period
        
        // Clean old cache entries
        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            for (auto it = marketDataCache_.begin(); it != marketDataCache_.end();) {
                if (it->second.lastUpdate < cutoff) {
                    it = marketDataCache_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Clean old price histories
        {
            std::unique_lock<std::shared_mutex> lock(historyMutex_);
            auto history_cutoff = std::chrono::system_clock::now() - std::chrono::hours(24);
            for (auto& [key, history] : priceHistories_) {
                history.removeOlderThan(history_cutoff);
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error during periodic cleanup: {}", e.what());
    }
}


void MarketDataInterface::pruneMarketDataCache() {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    auto now = std::chrono::steady_clock::now();
    for (auto it = marketDataCache_.begin(); it != marketDataCache_.end(); ) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.lastUpdate).count();
        if (age > cacheValidityDuration_.count()) {
            it = marketDataCache_.erase(it);
        } else {
            ++it;
        }
    }
}

//function to calculate adaptive batch size based on message rate
// Compute a recommended batch size for a given incoming message rate (messages/sec)
// Heuristic: try to keep per-thread work around target_work_ms (processing latency budget)
// If message rate is high, increase batch size up to maxProcessingBatchSize_.
size_t MarketDataInterface::adaptiveBatchSize(size_t messageRatePerSecond) const {
    if (messageRatePerSecond == 0) return minProcessingBatchSize_;
    // target per-batch processing time (ms) per thread; we want each batch to take a few ms
    const double targetBatchMs = 10.0; // aim for ~10ms of work per batch
    // assume average processing time per message ~0.2ms (empirical); clamp below
    const double avgPerMessageMs = 0.2;
    double desiredBatch = targetBatchMs / avgPerMessageMs;
    // scale with message rate: if rate is very high, it's better to process larger batches
    // but cap to avoid starvation
    size_t batch = static_cast<size_t>(std::max<double>(1.0, desiredBatch));
    if (batch > maxProcessingBatchSize_) batch = maxProcessingBatchSize_;
    return batch;
}

// Measure incoming rate using totalReceived_ snapshots and adjust processingBatchSize_
void MarketDataInterface::measureAndAdjustBatchSize() {
    try {
        auto now = std::chrono::steady_clock::now();
        uint64_t prev = lastTotalReceivedSnapshot_.load(std::memory_order_relaxed);
        uint64_t current = totalReceived_.load(std::memory_order_relaxed);
        double elapsed_s = std::chrono::duration_cast<std::chrono::duration<double>>(now - lastRateCheckTime_).count();
        if (elapsed_s <= 0.0) return;
        uint64_t delta = (current >= prev) ? (current - prev) : 0;
        size_t rate = static_cast<size_t>(std::round(delta / elapsed_s));

        // Compute target batch size using heuristic
        size_t recommended = adaptiveBatchSize(rate);

        // Smooth adjustments using exponential smoothing to avoid oscillation
        double currentBatch = static_cast<double>(processingBatchSize_);
        double alpha = batchSizeSmoothingAlpha_; // closer to 1 -> slower changes
        double smoothed = alpha * currentBatch + (1.0 - alpha) * static_cast<double>(recommended);
        size_t newBatch = static_cast<size_t>(std::max<double>(1.0, std::round(smoothed)));

        // enforce bounds
        if (newBatch > maxProcessingBatchSize_) newBatch = maxProcessingBatchSize_;
        if (newBatch < minProcessingBatchSize_) newBatch = minProcessingBatchSize_;

        if (newBatch != processingBatchSize_) {
            LOG_INFO("Adjusting processingBatchSize_ from {} to {} based on rate {} msg/s (delta={})",
                     processingBatchSize_, newBatch, rate, delta);
            processingBatchSize_ = newBatch;
        }

        lastTotalReceivedSnapshot_.store(current, std::memory_order_relaxed);
        // update timestamp for next measurement
        lastRateCheckTime_ = now;
    } catch (const std::exception& e) {
        LOG_WARN("measureAndAdjustBatchSize failed: {}", e.what());
    }
}

std::optional<MarketData> MarketDataInterface::getMarketData(
   const core::Symbol& tradingPair,
   const std::optional<std::string>& exchangeId) {
   
   core::ExchangeId exchange = core::ExchangeId::fromString(exchangeId.value_or(DEFAULT_EXCHANGE));
    std::string cacheKey = createCacheKey(exchange, tradingPair);

   // Check cache
   {
       std::shared_lock<std::shared_mutex> lock(cacheMutex_);
       auto it = marketDataCache_.find(cacheKey);
       if (it != marketDataCache_.end()) {
           const auto& entry = it->second;
           // Check if cache is still valid
           auto now = std::chrono::steady_clock::now();
           auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
               now - entry.lastUpdate).count();
           if (age <= cacheValidityDuration_.count() && entry.isValid) {
               // Hit
            //    LOG_INFO("getMarketData: cache HIT for {} (exchange={}), age={}ms, ttl={}ms", cacheKey, exchange.toString(), age, cacheValidityDuration_.count());
               return entry.data;  // Automatic conversion to std::optional
           } else {
               // Found but expired or invalid
            //    LOG_INFO("getMarketData: cache EXPIRED/INVALID for {} (exchange={}), age={}ms, ttl={}ms, isValid={}",
            //             cacheKey, exchange.toString(), age, cacheValidityDuration_.count(), entry.isValid);
           }
       } else {
           // Not in cache
           LOG_INFO("getMarketData: cache MISS for {} (exchange={}), ttl={}ms", cacheKey, exchange.toString(), cacheValidityDuration_.count());
       }
   }
   // Cache miss or expired
   LOG_WARN("getMarketData: no valid market data for {} on {} (returning nullopt)", tradingPair.toString(), exchange.toString());
   return std::nullopt;  // Indicate no data available

}

double MarketDataInterface::getCurrentPrice(
    const core::Symbol& tradingPair,
    const std::optional<std::string>& exchangeId) {
    
    auto marketDataOpt = getMarketData(tradingPair, exchangeId);
    
    // Check if we got valid market data
    if (!marketDataOpt.has_value()) {
        return 0.0; // or throw exception, depending on your error handling strategy
    }
    
    const auto& marketData = marketDataOpt.value();
    
    // Use mid-price if available
    if (marketData.bid > 0 && marketData.ask > 0) {
        return (marketData.bid + marketData.ask) / 2.0;
    }
    
    // Otherwise use last trade price
    if (marketData.lastTradePrice > 0) {
        return marketData.lastTradePrice;
    }
    //actually all are same
    // Fallback to price field
    return marketData.price;
}

std::vector<std::pair<Timestamp, double>> MarketDataInterface::getHistoricalPrices(
    const core::Symbol& tradingPair,
    const std::string& interval,
    int count,
    const std::optional<std::string>& exchangeId) {
    
    core::ExchangeId exchange = core::ExchangeId::fromString(exchangeId.value_or(DEFAULT_EXCHANGE));
    std::string cacheKey = createCacheKey(exchange, tradingPair);
    
    std::shared_lock<std::shared_mutex> lock(historyMutex_);
    auto it = priceHistories_.find(cacheKey);
    
    if (it != priceHistories_.end()) {
        return it->second.getRecentPrices(count);
    }
    
    // Return empty vector if no history available
    return {};
}
// Implementation in market_data_interface.cpp
double VolatilityCalculator::calculateEWMAVolatility(
    const std::deque<std::pair<Timestamp, double>>& prices, 
    const std::string& key) {
    
    if (prices.size() < 2) return 0.0;
    
    // Protect access to ewmaStates map
    std::lock_guard<std::mutex> g(ewmaMutex_);
    auto& state = ewmaStates[key];
    
    // Calculate returns
    std::vector<double> returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i-1].second > 0) {
            double logReturn = std::log(prices[i].second / prices[i-1].second);
            returns.push_back(logReturn);
        }
    }
    
    if (returns.empty()) return 0.0;
    
    // Initialize EWMA if needed
    if (!state.initialized && returns.size() >= 20) {
        // Use first 20 returns to initialize
        double sum = 0.0;
        for (size_t i = 0; i < 20; ++i) {
            sum += returns[i] * returns[i];
        }
        state.volatility = std::sqrt(sum / 20.0);
        state.initialized = true;
    }
    
    // Update EWMA volatility with latest return
    if (state.initialized && !returns.empty()) {
        double latestReturn = returns.back();
        state.volatility = std::sqrt(
            state.lambda * state.volatility * state.volatility + 
            (1 - state.lambda) * latestReturn * latestReturn
        );
    }
    
    // Annualize (assuming daily data)
    return state.volatility * std::sqrt(252.0);
}
// calculateSimpleVolatility
double VolatilityCalculator::calculateSimpleVolatility(
    const std::deque<std::pair<Timestamp, double>>& prices,
    const std::string& timeframe) {
    
    if (prices.size() < 2) return 0.0;
    
    // Simple volatility calculation using standard deviation of returns
    std::vector<double> returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i-1].second > 0) {
            double logReturn = std::log(prices[i].second / prices[i-1].second);
            returns.push_back(logReturn);
        }
    }
    
    if (returns.empty()) return 0.0;
    
    // Calculate mean
    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    
    // Calculate standard deviation
    double sumSquaredDiff = 0.0;
    for (double ret : returns) {
        sumSquaredDiff += std::pow(ret - mean, 2);
    }
    
    double stdDev = std::sqrt(sumSquaredDiff / returns.size());
    
    // Annualize based on timeframe
    double annualizationFactor = 252.0;  // Daily data
    if (timeframe == "1h") {
        annualizationFactor = 252.0 * 24.0;  // Hourly data
    } else if (timeframe == "1m") {
        annualizationFactor = 252.0 * 24.0 * 60.0;  // Minute data
    }
    
    return stdDev * std::sqrt(annualizationFactor);
}
// Calculate volatility using the specified method
double MarketDataInterface::getVolatility(
    const core::Symbol& tradingPair, 
    const std::string& timeframe, 
    const std::optional<std::string>& exchangeId) {
    
    core::ExchangeId exchange = core::ExchangeId::fromString(exchangeId.value_or(DEFAULT_EXCHANGE));
    std::string cacheKey = createCacheKey(exchange, tradingPair);
    
    std::shared_lock<std::shared_mutex> lock(historyMutex_);
    auto it = priceHistories_.find(cacheKey);
    
    if (it != priceHistories_.end()) {
        // Use EWMA volatility by default
        return volatilityCalculator_.calculateEWMAVolatility(it->second.prices, cacheKey);
    }
    
    return 0.0;
}

std::vector<OrderRequest> MarketDataInterface::getOrderHistory(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair, 
    int limit) {
    
    // This would typically fetch from a database or order management system
    // For now, return cached data or empty vector
    std::string cacheKey = createCacheKey(exchangeId, tradingPair);
    
    std::shared_lock<std::shared_mutex> lock(orderHistoryMutex_);
    auto it = orderHistoryCache_.find(cacheKey);
    
    if (it != orderHistoryCache_.end()) {
        const auto& orders = it->second;
        
        // Return up to 'limit' orders
        std::vector<OrderRequest> result;
        int count = std::min(limit, static_cast<int>(orders.size()));
        
        for (int i = 0; i < count; ++i) {
            result.push_back(orders[orders.size() - 1 - i]);  // Most recent first
        }
        
        return result;
    }
    
    return {};
}


void MarketDataInterface::registerMarketDataCallback(
    const std::function<void(const MarketData&)>& callback) {
    
    std::lock_guard<std::mutex> lock(callbackMutex_);
    marketDataCallbacks_.push_back(callback);
}
}

bool MarketDataInterface::subscribeMarketData(
    const core::Symbol& tradingPair, 
    const std::optional<std::string>& exchangeId) {
    
    if (!initialized_ || !zmqClient_) {
        LOG_ERROR("MarketDataInterface not initialized");
        return false;
    }
    
    core::ExchangeId exchange = core::ExchangeId::fromString(exchangeId.value_or(DEFAULT_EXCHANGE));
    
    // Subscribe to orderbook and trade topics for this pair
    std::string orderBookTopic = "orderbook." + exchange.toString() + "." + tradingPair.toString();
    // std::string tradeTopic = "trade." + exchange.toString() + "." + tradingPair.toString();

    bool success = true;
    MessageHandler handler = [this](const std::string& topic, const std::string& message,
                                  const std::unordered_map<std::string, std::string>& metadata) {
        if (topic.find("orderbook") != std::string::npos) {
            processSyntheticOrderBook_protobuf(topic, message);
        } else if (topic.find("trade") != std::string::npos) {
            processTradeUpdate(topic, message);
        }
    };
    success &= zmqClient_->subscribe(orderBookTopic, handler);
    if (success) {
        std::lock_guard<std::mutex> sLock(subscriptionMutex_);
        subscribedTopics_.insert(orderBookTopic);
    }
    // success &= zmqClient_->subscribe(tradeTopic, handler);
    
    if (success) {
        LOG_INFO("Subscribed to market data for " + exchange.toString() + ":" + tradingPair.toString());
    } else {
        LOG_ERROR("Failed to subscribe to market data for " + exchange.toString() + ":" + tradingPair.toString());
    }
    
    return success;
}
bool MarketDataInterface::isConnected() const {
    return initialized_.load() && running_.load() && 
           zmqClient_ && zmqClient_->isConnected();
}
bool MarketDataInterface::unsubscribeMarketData(
    const core::Symbol& tradingPair, 
    const std::optional<std::string>& exchangeId) {
    
    if (!initialized_ || !zmqClient_) {
        return false;
    }
    
    core::ExchangeId exchange = core::ExchangeId::fromString(exchangeId.value_or(DEFAULT_EXCHANGE));
    
    // Unsubscribe from topics
    std::string orderBookTopic = "orderbook." + exchange.toString() + "." + tradingPair.toString();
    // std::string tradeTopic = "trade." + exchange.toString() + "." + tradingPair.toString();

    bool success = true;
    success &= zmqClient_->unsubscribe(orderBookTopic);
    if (success) {
        std::lock_guard<std::mutex> sLock(subscriptionMutex_);
        subscribedTopics_.erase(orderBookTopic);
    }
    // success &= zmqClient_->unsubscribe(tradeTopic);
    
    return success;
}

std::map<std::string, double> MarketDataInterface::getTradingFees(
    const core::ExchangeId& exchangeId, 
    const std::optional<std::string>& tradingPair) {
    
    // Default fee structure for Binance
    // In production, this would be fetched from exchange API or configuration
    std::map<std::string, double> fees;
    
    if (exchangeId == core::ExchangeId::binance()) {
        fees["maker"] = 0.001;  // 0.1%
        fees["taker"] = 0.001;  // 0.1%
    } else {
        fees["maker"] = 0.002;  // 0.2% default
        fees["taker"] = 0.002;  // 0.2% default
    }
    
    return fees;
}

// Helper methods
std::string MarketDataInterface::createCacheKey(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair) const {

    return exchangeId.toString() + ":" + tradingPair.toString();
}
 

void MarketDataInterface::processTradeUpdate(
    const std::string& topic, 
    const std::string& message) {
    
    // Similar to processOrderBookUpdate but for trade data
    // For now, we'll focus on order book data
}

void MarketDataInterface::updateMarketDataCache(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair, 
    const data_ingestion::OrderBookUpdate& update) {
    
    // Convert OrderBookUpdate to MarketData
    core::MarketData marketData = convertOrderBookToMarketData(update);
    
    std::string cacheKey = createCacheKey(exchangeId, tradingPair);
    
    // Update cache
    {
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        marketDataCache_[cacheKey] = MarketDataCacheEntry{
            marketData,
            std::chrono::steady_clock::now(),
            true
        };
    }
    
    // Update price history
    {
        std::unique_lock<std::shared_mutex> lock(historyMutex_);
        priceHistories_[cacheKey].addPrice(marketData.timestamp, marketData.price);
    }
    
    // Notify callbacks
    // Copy callbacks under lock, then invoke outside the lock to avoid
    // potential deadlocks if callbacks call back into this object.
    std::vector<std::function<void(const MarketData&)>> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbacksCopy = marketDataCallbacks_;
    }
    for (const auto& callback : callbacksCopy) {
        try {
            callback(marketData);
        } catch (const std::exception& e) {
            LOG_ERROR("MarketData callback threw: {}", e.what());
        } catch (...) {
            LOG_ERROR("MarketData callback threw unknown exception");
        }
    }
}

core::MarketData MarketDataInterface::convertOrderBookToMarketData(
    const data_ingestion::OrderBookUpdate& orderBook) const {
    
    core::MarketData marketData;
    
    marketData.tradingPair = orderBook.tradingPair;
    marketData.exchangeId = orderBook.exchangeId;
    marketData.timestamp = std::chrono::system_clock::now();
    
    // Get best bid and ask
    if (!orderBook.bids.empty()) {
        marketData.bid = orderBook.bids[0].price;
    }
    
    if (!orderBook.asks.empty()) {
        marketData.ask = orderBook.asks[0].price;
    }
    
    // Calculate mid price
    if (marketData.bid > 0 && marketData.ask > 0) {
        marketData.price = (marketData.bid + marketData.ask) / 2.0;
    }
    
    // Calculate total volume at top levels
    double bidVolume = 0.0;
    double askVolume = 0.0;
    
    for (int i = 0; i < std::min(5, static_cast<int>(orderBook.bids.size())); ++i) {
        bidVolume += orderBook.bids[i].quantity;
    }
    
    for (int i = 0; i < std::min(5, static_cast<int>(orderBook.asks.size())); ++i) {
        askVolume += orderBook.asks[i].quantity;
    }
    
    marketData.volume = bidVolume + askVolume;
    
    // These would typically come from ticker data or be calculated over time
    // For now, we'll use placeholder values
    marketData.lastTradePrice = marketData.price;
    marketData.lastTradeQuantity = 0.0;
    marketData.open24h = marketData.price * 0.98;  // Placeholder
    marketData.high24h = marketData.price * 1.02;  // Placeholder
    marketData.low24h = marketData.price * 0.97;   // Placeholder
    marketData.volumeQuote24h = 0.0;
    
    return marketData;
}

// Factory function implementation
//we need to implement updateBalance and getBalance
// Implementation
void MarketDataInterface::updateBalance(const core::ExchangeId& exchangeId, 
                                       const std::string& asset, 
                                       double balance) {
    std::unique_lock<std::shared_mutex> lock(balanceMutex_);
    exchangeBalances_[exchangeId][asset] = balance;
}

double MarketDataInterface::getBalance(const core::ExchangeId& exchangeId, 
                                      const std::string& asset) const {
    std::shared_lock<std::shared_mutex> lock(balanceMutex_);
    
    auto exchangeIt = exchangeBalances_.find(exchangeId);
    if (exchangeIt != exchangeBalances_.end()) {
        auto assetIt = exchangeIt->second.find(asset);
        if (assetIt != exchangeIt->second.end()) {
            return assetIt->second;
        }
    }
    return 0.0;
}

// Update getExchangeBalances to use cached data
std::map<std::string, double> MarketDataInterface::getExchangeBalances(
    const core::ExchangeId& exchangeId) {
    
    std::shared_lock<std::shared_mutex> lock(balanceMutex_);
    
    auto it = exchangeBalances_.find(exchangeId);
    if (it != exchangeBalances_.end()) {
        return it->second;
    }
    
    return {};
}

void MarketDataInterface::connectionMonitorLoop() {
    auto lastBalanceUpdate = std::chrono::steady_clock::now();
    const auto balanceUpdateInterval = std::chrono::seconds(60);
    // Initial short sleep to stagger startup
    std::this_thread::sleep_for(std::chrono::seconds(5));
    int reconnect_attempt = 0;
    const int max_reconnect_attempts = 10;
    
    while (running_) {
        // Wait with a unique_lock only while waiting on the condition variable.
        {
            std::unique_lock<std::mutex> lk(reconnectionMutex_);
            // Wait for either a notification or timeout
            reconnectionCv_.wait_for(lk, std::chrono::seconds(15));
        }

        if (!running_) break;

        if (!checkConnection()) {
            handleDisconnection();
        }

        // Check if we need to reconnect
        if (needsReconnection_.load()) {
            LOG_INFO("Attempting to reconnect ZeroMQ client (attempt={}/{})", 
                     reconnect_attempt + 1, max_reconnect_attempts);

            try {
                // CRITICAL: Disconnect and reset the client completely to avoid
                // any concurrent socket access. This ensures the subscriber thread
                // is stopped before we create a new client.
                if (zmqClient_) {
                    try { 
                        zmqClient_->disconnect();  // This joins the subscriber thread
                        zmqClient_.reset();         // Destroy the old client completely
                    } catch (const std::exception& e) {
                        LOG_WARN("Exception during disconnect: {}", e.what());
                    }
                }

                // Small delay to ensure everything is cleaned up
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Create a fresh client instance
                communication::ZmqClientConfig zmqConfig;
                // zmqConfig.server_pub_endpoint = zmqSubscriberEndpoint_;
                // zmqConfig.server_req_endpoint = zmqPublisherEndpoint_;
                // zmqConfig.auto_reconnect = true;
                // zmqConfig.reconnect_interval = std::chrono::milliseconds(5000);
                // zmqConfig.request_timeout = std::chrono::milliseconds(10000);
                // zmqConfig.io_threads = 1;
                // zmqConfig.subscriber_conflate = false; // Don't conflate for orderbook data
                
                zmqClient_ = std::make_unique<communication::GenericZmqClient>(zmqConfig);
                LOG_INFO("Created new ZeroMQ client instance");

                // Store subscriptions BEFORE connecting
                std::vector<std::string> toApply;
                {
                    std::lock_guard<std::mutex> sLock(subscriptionMutex_);
                    toApply.assign(subscribedTopics_.begin(), subscribedTopics_.end());
                }

                // Register all subscription handlers BEFORE connecting
                // This ensures they're queued and will be applied by the subscriber thread
                for (const auto& topic : toApply) {
                    zmqClient_->subscribe(topic,
                        [this](const std::string& t, const std::string& msg, 
                               const std::unordered_map<std::string, std::string>& meta) {
                            if (t.find("orderbook") != std::string::npos) {
                                this->processSyntheticOrderBook_protobuf(t, msg);
                            } else if (t.find("trade") != std::string::npos) {
                                this->processTradeUpdate(t, msg);
                            }
                        }
                    );
                    LOG_INFO("Registered handler for topic: {}", topic);
                }

                // Now connect - the subscriber thread will apply all subscriptions
                if (zmqClient_->connect()) {
                    needsReconnection_.store(false);
                    reconnect_attempt = 0; // reset attempts on success
                    lastConnectionCheck_ = std::chrono::steady_clock::now();
                    LOG_INFO("Successfully reconnected ZeroMQ client");
                } else {
                    LOG_ERROR("Failed to connect new ZeroMQ client");
                    needsReconnection_.store(true);
                    reconnect_attempt++;
                    
                    if (reconnect_attempt >= max_reconnect_attempts) {
                        LOG_ERROR("Max reconnection attempts reached. Giving up.");
                        reconnect_attempt = 0;
                        // Wait longer before trying again
                        std::this_thread::sleep_for(std::chrono::seconds(60));
                    } else {
                        // Exponential backoff
                        auto delay = std::chrono::milliseconds(1000 * (1 << std::min(reconnect_attempt, 5)));
                        std::this_thread::sleep_for(delay);
                    }
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Exception during reconnection: {}", e.what());
                reconnect_attempt++;
                errorStats_.connectionErrors++;
                
                // Exponential backoff on error
                auto delay = std::chrono::milliseconds(1000 * (1 << std::min(reconnect_attempt, 5)));
                std::this_thread::sleep_for(delay);
            }
        }

        // Periodic balance update check
        auto now = std::chrono::steady_clock::now();
        if (now - lastBalanceUpdate >= balanceUpdateInterval) {
            // Your balance update logic here
            lastBalanceUpdate = now;
        }
    }
    
    LOG_INFO("Connection monitor loop exiting");
}




void MarketDataInterface::handleDisconnection() {
    LOG_WARN("Connection appears to be lost, scheduling reconnection");
    needsReconnection_.store(true);
    errorStats_.connectionErrors++;
    errorStats_.lastError = std::chrono::steady_clock::now();
    // Wake monitor thread so it can react immediately
    reconnectionCv_.notify_one();
}

bool MarketDataInterface::checkConnection() {
    // Basic check: ensure zmq client reports connected status
    try {
        if (zmqClient_) {
            return zmqClient_->isConnected();
        }
    } catch (...) {
        // Treat exceptions as disconnected
    }
    return false;
}
 
// Update destructor
MarketDataInterface::~MarketDataInterface() {
    running_ = false;
    // Wake monitor thread so it can exit promptly
    reconnectionCv_.notify_one();
    if (reconnectionThread_.joinable()) {
        reconnectionThread_.join();
    }
    // Stop processing threads
    processingRunning_.store(false);
    {
        std::lock_guard<std::mutex> lock(inboundQueueMutex_);
        // notify in case thread is waiting
        inboundQueueCv_.notify_all();
    }
    for (auto& t : processingThreads_) {
        if (t.joinable()) t.join();
    }
    processingThreads_.clear();
    
    // ... existing cleanup code ...
    if (zmqClient_) {
        zmqClient_->stop();
    }
    zmqClient_.reset();
    // normalizer_.reset();
    marketDataCache_.clear();
    priceHistories_.clear();
    orderHistoryCache_.clear();
}
// createDataInterface
std::shared_ptr<DataInterface> MarketDataInterface::createDataInterface(const RiskConfig& config) {
    auto instance = std::make_shared<MarketDataInterface>(config);
    if (!instance->initialize()) {
        LOG_ERROR("Failed to initialize MarketDataInterface");
        return nullptr;
    }
    return instance;
}
    
} // namespace risk