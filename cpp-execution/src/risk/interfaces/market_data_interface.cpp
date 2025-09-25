#include <risk/interfaces/market_data_interface.hpp>
#include "common/logger.hpp"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include "communication/zmq_config.hpp"
#include "communication/zmq_client.hpp"
#include "communication/zmq_manager.hpp"
#include <iostream>

using namespace trading::communication;
namespace trading {
namespace risk {

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
            zmqConfig.server_pub_endpoint = zmqSubscriberEndpoint_;
            zmqConfig.server_req_endpoint = zmqPublisherEndpoint_;
            zmqConfig.auto_reconnect = true;
            zmqConfig.reconnect_interval = std::chrono::milliseconds(5000);
            zmqConfig.request_timeout = std::chrono::milliseconds(10000);
            zmqConfig.io_threads = 1;

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
                // Enqueue-only: push raw message to inbound queue and wake processing thread
                totalReceived_.fetch_add(1, std::memory_order_relaxed);
                MarketDataInterface::RawMessage raw{topic, message, std::chrono::steady_clock::now()};
                {
                    std::unique_lock<std::mutex> qlock(this->inboundQueueMutex_);
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

        // Start processing thread for inbound messages
        try {
            processingRunning_.store(true);
            processingThread_ = std::thread([this]() { this->processQueueLoop(); });
            LOG_INFO("MarketDataInterface", "Started market data processing thread");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start processing thread: {}", e.what());
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
        RawMessage item;
        {
            std::unique_lock<std::mutex> lock(inboundQueueMutex_);
            if (inboundQueue_.empty()) {
                // Wait until notified or check periodically to allow shutdown
                inboundQueueCv_.wait_for(lock, std::chrono::milliseconds(500));
                if (!processingRunning_.load()) break;
                if (inboundQueue_.empty()) continue;
            }
            item = std::move(inboundQueue_.front());
            inboundQueue_.pop_front();
        }

        try {
            // Call existing processing function which does JSON parse, cache updates, callbacks
            auto start = std::chrono::steady_clock::now();
            processSyntheticOrderBook(item.topic, item.message);
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
void MarketDataInterface::processSyntheticOrderBook(const std::string& topic, const std::string& message) {
    try {
        // Defensive: reject excessively large messages early to avoid OOMs
        if (message.size() > maxMessageSize_) {
            errorStats_.dataErrors++;
            LOG_WARN("Dropping oversized market data message (size={} bytes) for topic={}", message.size(), topic);
            return;
        }
        // Quick topic sanity: only handle orderbook topics
        if (topic.find("orderbook.") == std::string::npos) {
            return; // Not a synthetic orderbook message
        }

        if (message.empty()) {
            LOG_ERROR("Invalid JSON message in processSyntheticOrderBook: empty payload");
            return;
        }

        // Parse outer JSON safely
        nlohmann::json json_msg;
        try {
            json_msg = nlohmann::json::parse(message);
        } catch (const std::bad_alloc& e) {
            // Out of memory while parsing JSON - record and drop
            errorStats_.dataErrors++;
            LOG_ERROR("processSyntheticOrderBook: OOM while parsing outer JSON: {}", e.what());
            return;
        } catch (const std::exception& e) {
            LOG_ERROR("processSyntheticOrderBook: failed to parse outer JSON: {}", e.what());
            errorStats_.dataErrors++;
            return;
        }

        // If the useful payload is embedded as a JSON string in "data", parse it
        if (json_msg.contains("data") && json_msg["data"].is_string()) {
            try {
                auto inner = json_msg["data"].get<std::string>();
                if (!inner.empty() && inner.front() == '{') {
                    // Avoid attempting to parse absurdly large embedded payloads
                    if (inner.size() <= maxMessageSize_) {
                        json_msg = nlohmann::json::parse(inner);
                    } else {
                        LOG_WARN("processSyntheticOrderBook: inner 'data' payload too large ({} bytes), skipping inner parse", inner.size());
                    }
                }
            } catch (const std::bad_alloc& e) {
                errorStats_.dataErrors++;
                LOG_ERROR("processSyntheticOrderBook: OOM while parsing inner 'data' JSON: {}", e.what());
            } catch (const std::exception& e) {
                LOG_WARN("processSyntheticOrderBook: failed to parse inner 'data' JSON: {}", e.what());
                // continue using outer json_msg (may still contain useful keys)
            }
        }

        // Extract exchange and symbol (prefer payload, fallback to topic)
        std::string exchange;
        std::string symbol;
        if (json_msg.contains("exchange") && json_msg["exchange"].is_string())
            exchange = json_msg["exchange"].get<std::string>();
        if (json_msg.contains("symbol") && json_msg["symbol"].is_string())
            symbol = json_msg["symbol"].get<std::string>();

        if (exchange.empty() || symbol.empty()) {
            // topic format expected: "orderbook.<EXCHANGE>.<SYMBOL>"
            std::vector<std::string> parts;
            size_t start = 0;
            while (true) {
                size_t pos = topic.find('.', start);
                if (pos == std::string::npos) break;
                parts.push_back(topic.substr(start, pos - start));
                start = pos + 1;
            }
            // push last segment
            if (start < topic.size()) parts.push_back(topic.substr(start));

            if (parts.size() >= 3) {
                if (exchange.empty()) exchange = parts[1];
                if (symbol.empty()) {
                    symbol = parts[2];
                    // topic often uses '_' as separator (ETH_USDT) -> convert to 'ETH/USDT'
                    std::replace(symbol.begin(), symbol.end(), '_', '/');
                }
            }
        }

        if (exchange.empty() || symbol.empty()) {
            LOG_ERROR("processSyntheticOrderBook: missing exchange or symbol (topic={})", topic);
            return;
        }

        // Build MarketData and safely extract best bid/ask
        MarketData data;
        data.exchangeId = core::ExchangeId::fromString(exchange);
        data.tradingPair = core::Symbol::fromString(symbol);
        data.timestamp = std::chrono::system_clock::now();

        auto extractPriceFromLevel = [](const nlohmann::json& level) -> std::optional<double> {
            try {
                if (level.is_array() && level.size() >= 1) {
                    if (level[0].is_string()) return std::stod(level[0].get<std::string>());
                    if (level[0].is_number()) return level[0].get<double>();
                }
            } catch (...) {}
            return std::nullopt;
        };

        std::optional<double> bestBid;
        std::optional<double> bestAsk;

        if (json_msg.contains("bids") && json_msg["bids"].is_array() && !json_msg["bids"].empty()) {
            bestBid = extractPriceFromLevel(json_msg["bids"][0]);
        } else if (json_msg.contains("b") && json_msg["b"].is_array() && !json_msg["b"].empty()) {
            bestBid = extractPriceFromLevel(json_msg["b"][0]);
        }

        if (json_msg.contains("asks") && json_msg["asks"].is_array() && !json_msg["asks"].empty()) {
            bestAsk = extractPriceFromLevel(json_msg["asks"][0]);
        } else if (json_msg.contains("a") && json_msg["a"].is_array() && !json_msg["a"].empty()) {
            bestAsk = extractPriceFromLevel(json_msg["a"][0]);
        }

        if (bestBid.has_value()) data.bid = *bestBid;
        if (bestAsk.has_value()) data.ask = *bestAsk;
        if (bestBid.has_value() && bestAsk.has_value()) {
            data.price = (data.bid + data.ask) / 2.0;
        } else if (json_msg.contains("price") && json_msg["price"].is_number()) {
            data.price = json_msg["price"].get<double>();
        }

        // Update cache key and write under locks (same pattern as before)
        std::string cacheKey = createCacheKey(data.exchangeId, data.tradingPair);

        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            marketDataCache_[cacheKey] = MarketDataCacheEntry{data, std::chrono::steady_clock::now(), true};
        }

        {
            std::unique_lock<std::shared_mutex> hlock(historyMutex_);
            // Only add price if we have a non-zero price
            if (data.price > 0.0) {
                try {
                    priceHistories_[cacheKey].addPrice(data.timestamp, data.price);
                } catch (const std::bad_alloc& e) {
                    errorStats_.dataErrors++;
                    LOG_ERROR("processSyntheticOrderBook: OOM while adding price to history: {}", e.what());
                } catch (const std::exception& e) {
                    errorStats_.dataErrors++;
                    LOG_ERROR("processSyntheticOrderBook: exception while adding price to history: {}", e.what());
                }
            }
        }

        // Copy callbacks then invoke outside lock
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
                LOG_ERROR("MarketData callback OOM: {}", e.what());
            } catch (const std::exception& e) {
                LOG_ERROR("MarketData callback threw: {}", e.what());
            } catch (...) {
                LOG_ERROR("MarketData callback threw unknown exception");
            }
        }

    } catch (const std::bad_alloc& e) {
        // Very aggressive OOM: try to recover gracefully and signal for monitoring
        errorStats_.dataErrors++;
        LOG_ERROR("Failed to process synthetic orderbook (OOM): {}", e.what());
    } catch (const std::exception& e) {
        errorStats_.dataErrors++;
        LOG_ERROR("Failed to process synthetic orderbook: {}", e.what());
    } catch (...) {
        errorStats_.dataErrors++;
        LOG_ERROR("Failed to process synthetic orderbook: unknown error");
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
            processSyntheticOrderBook(topic, message);
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
// Implementation
void MarketDataInterface::connectionMonitorLoop() {
    auto lastBalanceUpdate = std::chrono::steady_clock::now();
    const auto balanceUpdateInterval = std::chrono::seconds(60);
    // Initial short sleep to stagger startup
    std::this_thread::sleep_for(std::chrono::seconds(5));
    int reconnect_attempt = 0;
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

        // Check if we need to resubscribe
        if (needsReconnection_.load()) {
            LOG_INFO("Attempting to reconnect ZeroMQ client");

            try {
                // Do not hold reconnectionMutex_ while performing network ops or calling into zmqClient_
                // Attempt graceful disconnect if the client exists
                if (zmqClient_) {
                    try { zmqClient_->disconnect(); } catch (...) {}
                }

                // Ensure we have a client instance
                if (!zmqClient_) {
                    communication::ZmqClientConfig zmqConfig;
                    zmqConfig.server_pub_endpoint = zmqSubscriberEndpoint_;
                    zmqConfig.server_req_endpoint = zmqPublisherEndpoint_;
                    zmqConfig.auto_reconnect = true;
                    zmqConfig.reconnect_interval = std::chrono::milliseconds(5000);
                    zmqConfig.request_timeout = std::chrono::milliseconds(10000);
                    zmqConfig.io_threads = 1;
                    zmqClient_ = std::make_unique<communication::GenericZmqClient>(zmqConfig);
                    LOG_INFO("Created ZeroMQ client for reconnection");
                }

                // Try to start/connect
                if (!checkConnection()) {
                    handleDisconnection();
                }

                // Check if we need to resubscribe / reconnect
                if (needsReconnection_.load()) {
                    LOG_INFO("Attempting to reconnect ZeroMQ client (attempt={})", reconnect_attempt + 1);

                    // Attempt reconnect with exponential backoff
                    try {
                        // Attempt graceful disconnect if the client exists (don't hold mutex)
                        if (zmqClient_) {
                            try { zmqClient_->disconnect(); } catch (...) {}
                        }

                        // Ensure we have a client instance
                        if (!zmqClient_) {
                            communication::ZmqClientConfig zmqConfig;
                            zmqConfig.server_pub_endpoint = zmqSubscriberEndpoint_;
                            zmqConfig.server_req_endpoint = zmqPublisherEndpoint_;
                            zmqConfig.auto_reconnect = true;
                            zmqConfig.reconnect_interval = std::chrono::milliseconds(5000);
                            zmqConfig.request_timeout = std::chrono::milliseconds(10000);
                            zmqConfig.io_threads = 1;

                            zmqClient_ = std::make_unique<communication::GenericZmqClient>(zmqConfig);
                            LOG_INFO("Created ZeroMQ client for reconnection");
                        }

                        // Try to start/connect
                        if (zmqClient_ && zmqClient_->start()) {
                            needsReconnection_.store(false);
                            reconnect_attempt = 0; // reset attempts on success
                            lastConnectionCheck_ = std::chrono::steady_clock::now();
                            LOG_INFO("Successfully reconnected ZeroMQ client");

                            // Reapply tracked subscriptions so we actually receive messages again
                            std::vector<std::string> toApply;
                            {
                                std::lock_guard<std::mutex> sLock(subscriptionMutex_);
                                toApply.assign(subscribedTopics_.begin(), subscribedTopics_.end());
                            }

                            for (const auto& topic : toApply) {
                                try {
                                    zmqClient_->subscribe(topic,
                                        [this](const std::string& t, const std::string& msg, const std::unordered_map<std::string, std::string>& meta) {
                                            if (t.find("orderbook") != std::string::npos) {
                                                this->processSyntheticOrderBook(t, msg);
                                            } else if (t.find("trade") != std::string::npos) {
                                                this->processTradeUpdate(t, msg);
                                            }
                                        }
                                    );
                                    LOG_INFO("Reapplied subscription for topic: {}", topic);
                                } catch (const std::exception& e) {
                                    errorStats_.subscriptionErrors++;
                                    LOG_ERROR("Failed to reapply subscription for topic {}: {}", topic, e.what());
                                }
                            }
                        } else {
                            // Connection attempt failed — schedule next try with backoff
                            reconnect_attempt++;
                            auto delay = trading::communication::ZmqManager::instance().reconnectDelayMs(reconnect_attempt, std::chrono::milliseconds(500), 10);
                            LOG_WARN("Reconnect attempt {} failed; sleeping for {} ms before retry", reconnect_attempt, delay.count());
                            // Sleep while still allowing shutdown notifications
                            std::unique_lock<std::mutex> lk(reconnectionMutex_);
                            reconnectionCv_.wait_for(lk, delay);
                        }

                        auto now = std::chrono::steady_clock::now();
                        if (now - lastBalanceUpdate > balanceUpdateInterval) {
                            try {
                                auto balances = m_binanceClient.getBalances();
                                for (const auto& [asset, amount] : balances) {
                                    updateBalance(core::ExchangeId::binance(), asset, amount);
                                }
                                lastBalanceUpdate = now;
                            } catch (const std::exception& e) {
                                LOG_ERROR("Failed to update Binance balances: {}", e.what());
                            }
                        }

                    } catch (const std::exception& e) {
                        errorStats_.connectionErrors++;
                        LOG_ERROR("Reconnection failed: {}", e.what());
                    }
                }

                // Keep the cache pruned periodically so stale entries are removed
                pruneMarketDataCache();
            }
            catch (const std::exception& e) {
                errorStats_.connectionErrors++;
                LOG_ERROR("Reconnection process failed: {}", e.what());
            }
        }
    }
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
    // Stop processing thread
    processingRunning_.store(false);
    {
        std::lock_guard<std::mutex> lock(inboundQueueMutex_);
        // notify in case thread is waiting
        inboundQueueCv_.notify_one();
    }
    if (processingThread_.joinable()) {
        processingThread_.join();
    }
    
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
} // namespace trading