#include <risk/interfaces/market_data_interface.hpp>
#include "common/logger.hpp"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include "communication/zmq_config.hpp"
#include "communication/zmq_client.hpp"

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
        // Initialize ZeroMQ client
        ZmqClientConfig zmqConfig;
        zmqConfig.server_pub_endpoint = zmqSubscriberEndpoint_;
        zmqConfig.server_req_endpoint = zmqPublisherEndpoint_;
        zmqConfig.auto_reconnect = true;
        zmqConfig.reconnect_interval = std::chrono::milliseconds(5000);
        zmqConfig.request_timeout = std::chrono::milliseconds(10000);
        zmqConfig.io_threads = 1;
        
        
        zmqClient_ = std::make_unique<GenericZmqClient>(zmqConfig);

        // Start ZeroMQ client BEFORE subscribing so subscriptions succeed
        if (!zmqClient_->connect()) {
            LOG_ERROR("Failed to connect ZeroMQ client");
            return false;
        }

        RequestHandler requestHandler = 
            [this](const std::string& request_data, 
                   const std::unordered_map<std::string, std::string>& metadata) -> std::string {
            // Handle incoming requests if needed
            return "ACK"; // Simple acknowledgment for now
        };

        // Initialize order book normalizer
        normalizer_ = std::make_shared<data_ingestion::OrderBookNormalizer>(
            [this](const data_ingestion::OrderBookUpdate& update) {
                // This callback is called when order book is normalized
                updateMarketDataCache(update.exchangeId, update.tradingPair, update);
            }
        );
 
        zmqClient_->subscribe("orderbook.",  // or "orderbook.*" if your ZMQ supports wildcards
            [this](const std::string& topic, const std::string& message, const auto&) {
                this->processSyntheticOrderBook(topic, message);
            });
        // Start ZeroMQ client
        if (!zmqClient_->connect()) {
            LOG_ERROR("Failed to start ZeroMQ client");
            return false;
        }
        
        initialized_.store(true);
        running_.store(true);
        
        LOG_INFO("MarketDataInterface initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize MarketDataInterface: " + std::string(e.what()));
        return false;
    }
}
void MarketDataInterface::processSyntheticOrderBook(const std::string& topic, const std::string& message) {
    try {
        auto json_msg = nlohmann::json::parse(message);
        std::string exchange = json_msg["exchange"];
        std::string symbol = json_msg["symbol"];
        // Parse bids/asks
        auto bids = json_msg["bids"];
        auto asks = json_msg["asks"];
        // struct MarketData {
        //     core::Symbol tradingPair;              // Trading pair symbol (e.g., "BTC/USDT")
        //     ExchangeId exchangeId;               // Exchange identifier
        //     Timestamp timestamp;                  // Timestamp of the data
        //     double price;                         // Current market price
        //     double volume;                        // Trading volume
        //     double bid;                           // Best bid price
        //     double ask;                           // Best ask price
        //     double lastTradePrice;                // Last trade price
        //     double lastTradeQuantity;             // Last trade quantity
        //     double open24h;                       // Opening price 24 hours ago
        //     double high24h;                       // Highest price in last 24 hours
        //     double low24h;                        // Lowest price in last 24 hours
        //     double volumeQuote24h;                // 24-hour volume in quote currency
        //     double priceChange24h;
        //     double priceChangePercent24h;

    

        MarketData data;
        data.exchangeId = core::ExchangeId::fromString(exchange);
        data.tradingPair = core::Symbol::fromString(symbol);
        data.timestamp = std::chrono::system_clock::now();
        if (!bids.empty() && !asks.empty()) {
            data.bid = bids[0][0]; // Best bid price
            data.ask = asks[0][0]; // Best ask price
            data.price = (data.bid + data.ask) / 2.0; // Mid price
        }
        //update cache directly here
        std::string cacheKey = createCacheKey(data.exchangeId, data.tradingPair);
        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            marketDataCache_[cacheKey] = MarketDataCacheEntry{
                data,
                std::chrono::steady_clock::now(),
                true
            };
        }
        // For simplicity, we won't calculate volume, lastTradePrice, etc. here
        // In a real implementation, you'd extract these from the message if available

        
        // ...convert to your internal types and update cache/state...
        //this is the already process orderbook update, like this
        // b'{"data":"{\\"asks\\":[[4518.98,0.1263],[4518.99,0.0096],[4519.0,0.0455],[4519.01,0.0036],[4519.02,1.1072],[4519.03,0.0467],[4519.04,0.0048],[4519.05,1.1012],[4519.06,0.0101],[4519.08,0.0676],[4519.12,0.'


    } catch (const std::exception& e) {
        LOG_ERROR("Failed to process synthetic orderbook: " + std::string(e.what()));
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
           // Check if cache is still valid (within 10ms)
           auto now = std::chrono::steady_clock::now();
           auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
               now - entry.lastUpdate).count();
           if (age <= cacheValidityDuration_.count() && entry.isValid) {
               return entry.data;  // Automatic conversion to std::optional
           }
       }
   }
   
   // If not in cache or expired, try to get from order book normalizer
   try {
       auto orderBookUpdate = normalizer_->getOrderBook(exchange, tradingPair);
       // Convert to MarketData
       MarketData marketData = convertOrderBookToMarketData(orderBookUpdate);
       
       // Update cache
       {
           std::unique_lock<std::shared_mutex> lock(cacheMutex_);
           marketDataCache_[cacheKey] = MarketDataCacheEntry{
               marketData,
               std::chrono::steady_clock::now(),
               true
           };
       }
       
       return marketData;  // Automatic conversion to std::optional
       
   } catch (const std::exception& e) {
       LOG_ERROR("Failed to get market data: " + std::string(e.what()));
       return std::nullopt;  // Proper way to indicate failure
   }
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
    std::string orderBookTopic = "market.orderbook." + exchange.toString() + "." + tradingPair.toString();
    std::string tradeTopic = "market.trade." + exchange.toString() + "." + tradingPair.toString();

    bool success = true;
    MessageHandler handler = [this](const std::string& topic, const std::string& message,
                                  const std::unordered_map<std::string, std::string>& metadata) {
        if (topic.find("orderbook") != std::string::npos) {
            processOrderBookUpdate(topic, message);
        } else if (topic.find("trade") != std::string::npos) {
            processTradeUpdate(topic, message);
        }
    };
    success &= zmqClient_->subscribe(orderBookTopic, handler);
    success &= zmqClient_->subscribe(tradeTopic, handler);
    
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
    std::string orderBookTopic = "market.orderbook." + exchange.toString() + "." + tradingPair.toString();
    std::string tradeTopic = "market.trade." + exchange.toString() + "." + tradingPair.toString();

    bool success = true;
    success &= zmqClient_->unsubscribe(orderBookTopic);
    success &= zmqClient_->unsubscribe(tradeTopic);
    
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

void MarketDataInterface::processOrderBookUpdate(
    const std::string& topic, 
    const std::string& message) {
    
    try {
        // Parse topic to extract exchange and trading pair
        // Format: "market.orderbook.binance.BTC/USDT"
        std::vector<std::string> parts;
        std::stringstream ss(topic);
        std::string part;
        
        while (std::getline(ss, part, '.')) {
            parts.push_back(part);
        }
        
        if (parts.size() >= 4) {
            core::ExchangeId exchange = core::ExchangeId::fromString(parts[2]);
            core::Symbol tradingPair = parts[3];
            
            // Create MarketDataUpdate from the message
            core::MarketDataUpdate update;
            update.exchangeId = exchange;
            update.tradingPair = tradingPair;
            update.updateType = "orderbook";
            update.timestamp = std::chrono::system_clock::now();
            update.data = std::vector<uint8_t>(message.begin(), message.end());
            
            // Process through normalizer
            normalizer_->processMarketData(update);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing order book update: " + std::string(e.what()));
    }
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
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        for (const auto& callback : marketDataCallbacks_) {
            callback(marketData);
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
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (!checkConnection()) {
            handleDisconnection();
        }
        
        // Check if we need to resubscribe
        if (needsReconnection_.load()) {
            LOG_INFO("Attempting to reconnect ZeroMQ client");
            
            try {
                // Stop existing client
                if (zmqClient_) {
                    zmqClient_->disconnect();
                }
                
                // Reinitialize
                communication::ZmqClientConfig zmqConfig;
                zmqConfig.server_pub_endpoint = zmqSubscriberEndpoint_;
                zmqConfig.server_req_endpoint = zmqPublisherEndpoint_;
                zmqConfig.auto_reconnect = true;
                zmqConfig.reconnect_interval = std::chrono::milliseconds(5000);
                zmqConfig.request_timeout = std::chrono::milliseconds(10000);   

                zmqClient_ = std::make_unique<communication::GenericZmqClient>(zmqConfig);
               
                // zmqClient_->subscribe("market.orderbook.*", [](const std::string& topic, 
                //                          const std::string& data,
                //                          const auto& metadata) {
                //     // Process orderbook update
                //     (void)metadata;
                    
                //     LOG_INFO("Received orderbook update on topic: " + topic);
                // });

               
                
                if (zmqClient_->start()) {
                    needsReconnection_.store(false);
                    LOG_INFO("Successfully reconnected ZeroMQ client");
                    
                    // Resubscribe to all topics
                    // You'd need to track subscriptions for this
                }
                
            } catch (const std::exception& e) {
                errorStats_.connectionErrors++;
                LOG_ERROR("Reconnection failed: " + std::string(e.what()));
            }
        }
    }
}

bool MarketDataInterface::checkConnection() {
    if (!zmqClient_ || !zmqClient_->isConnected()) {
        return false;
    }
    
    // Check if we're receiving data
    auto now = std::chrono::steady_clock::now();
    
    // If no data for 30 seconds, consider connection dead
    bool hasRecentData = false;
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        for (const auto& [key, entry] : marketDataCache_) {
            auto dataAge = std::chrono::duration_cast<std::chrono::seconds>(
                now - entry.lastUpdate
            ).count();
            
            if (dataAge < 30) {
                hasRecentData = true;
                break;
            }
        }
    }
    
    return hasRecentData;
}

void MarketDataInterface::handleDisconnection() {
    LOG_WARN("Connection appears to be lost, scheduling reconnection");
    needsReconnection_.store(true);
    errorStats_.connectionErrors++;
    errorStats_.lastError = std::chrono::steady_clock::now();
}
 
// Update destructor
MarketDataInterface::~MarketDataInterface() {
    running_ = false;
    
    if (reconnectionThread_.joinable()) {
        reconnectionThread_.join();
    }
    
    // ... existing cleanup code ...
    if (zmqClient_) {
        zmqClient_->stop();
    }
    zmqClient_.reset();
    normalizer_.reset();
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