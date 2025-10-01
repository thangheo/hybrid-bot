#include "risk/interfaces/market_data_interface.hpp"
#include "common/logger.hpp"
#include <algorithm>
// Needed for normalizeSymbolForExchange utility
#include "execution/exchange_api_client.hpp"

namespace trading {
namespace risk {

// Shared singleton instance used by all factory overloads to ensure a single
// MarketDataInterface (and therefore a single MarketDataCache) across the
// process. Previously each overload had its own function-local static which
// resulted in multiple singletons and duplicated caches.
namespace {
    std::shared_ptr<DataInterface> g_marketDataInterface_singleton;
    std::mutex g_marketDataInterface_singleton_mutex;
}

MarketDataInterface::MarketDataInterface() {
    LOG_INFO("MarketDataInterface default constructor this={}", static_cast<const void*>(this));
}

MarketDataInterface::MarketDataInterface(const RiskConfig& config) 
    : config_(config) {
    defaultExchange_ = config.defaultExchange;
    LOG_INFO("MarketDataInterface configured this={} with default exchange: {}", static_cast<const void*>(this), defaultExchange_);
}

MarketDataInterface::~MarketDataInterface() {
    if (initialized_.load()) {
        shutdown();
    }
}

bool MarketDataInterface::initialize() {
    if (initialized_.load()) {
        LOG_WARN("MarketDataInterface already initialized");
        return true;
    }
    
    try {
        LOG_INFO("Initializing MarketDataInterface components");
        
        initializeComponents();
        
        // Register callback to update cache when data arrives
        subscriber_->registerCallback(
            [this](const core::MarketData& data) {
                this->onMarketDataReceived(data);
            });
        
        // Start subscriber
        if (!subscriber_->initialize()) {
            LOG_ERROR("Failed to initialize market data subscriber");
            return false;
        }

        // Auto-subscribe to orderbook updates (wildcard) so internal consumers receive orderbook messages
        try {
            if (!subscriber_->subscribePattern(ORDERBOOK_UPDATES_TOPIC)) {
                LOG_WARN("Failed to auto-subscribe to {}", ORDERBOOK_UPDATES_TOPIC);
            } else {
                LOG_INFO("Auto-subscribed to {}", ORDERBOOK_UPDATES_TOPIC);
            }
        } catch (const std::exception& e) {
            LOG_WARN("Exception during auto-subscribe: {}", e.what());
        }
        
        initialized_.store(true);
        LOG_INFO("MarketDataInterface initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize MarketDataInterface: {}", e.what());
        return false;
    }
}

void MarketDataInterface::shutdown() {
    LOG_INFO("Shutting down MarketDataInterface");
    
    if (subscriber_) {
        subscriber_->shutdown();
    }
    
    if (cache_) {
        cache_->clear();
    }
    
    initialized_.store(false);
    LOG_INFO("MarketDataInterface shutdown complete");
}

std::optional<MarketData> MarketDataInterface::getMarketData(
    const core::Symbol& tradingPair,
    const std::optional<std::string>& exchangeId) {
    
    if (!initialized_.load()) {
        LOG_WARN("MarketDataInterface not initialized");
        return std::nullopt;
    }
    
    core::ExchangeId exchange = core::ExchangeId::fromString(
        exchangeId.value_or(defaultExchange_));
    std::string cacheKey = createCacheKey(exchange, tradingPair);
    
    return cache_->getMarketData(cacheKey);
}

double MarketDataInterface::getCurrentPrice(
    const core::Symbol& tradingPair, 
    const std::optional<std::string>& exchangeId) {
    
    if (!initialized_.load()) {
        return 0.0;
    }
    
    core::ExchangeId exchange = core::ExchangeId::fromString(
        exchangeId.value_or(defaultExchange_));
    std::string cacheKey = createCacheKey(exchange, tradingPair);
    
    auto price = cache_->getCurrentPrice(cacheKey);
    if (!price.has_value()) {
        LOG_DEBUG("Price not found in cache for {}:{}, cacheKey={}", exchange.toString(), tradingPair.toString(), cacheKey);
        //throw std::runtime_error("Price not found in cache");
    }
    return price.value_or(0.0);
}

std::vector<std::pair<Timestamp, double>> MarketDataInterface::getHistoricalPrices(
    const core::Symbol& tradingPair,
    const std::string& interval,
    int count,
    const std::optional<std::string>& exchangeId) {
    
    if (!initialized_.load()) {
        return {};
    }
    
    core::ExchangeId exchange = core::ExchangeId::fromString(
        exchangeId.value_or(defaultExchange_));
    std::string cacheKey = createCacheKey(exchange, tradingPair);
    
    return cache_->getHistoricalPrices(cacheKey, count);
}

double MarketDataInterface::getVolatility(
    const core::Symbol& tradingPair, 
    const std::string& timeframe, 
    const std::optional<std::string>& exchangeId) {
    
    if (!initialized_.load()) {
        return 0.0;
    }
    
    core::ExchangeId exchange = core::ExchangeId::fromString(
        exchangeId.value_or(defaultExchange_));
    std::string cacheKey = createCacheKey(exchange, tradingPair);
    
    return cache_->getVolatility(cacheKey, timeframe);
}

std::vector<OrderRequest> MarketDataInterface::getOrderHistory(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair, 
    int limit) {
    
    std::string cacheKey = createCacheKey(exchangeId, tradingPair);
    
    std::shared_lock<std::shared_mutex> lock(orderHistoryMutex_);
    auto it = orderHistoryCache_.find(cacheKey);
    
    if (it != orderHistoryCache_.end()) {
        const auto& orders = it->second;
        
        std::vector<OrderRequest> result;
        int count = std::min(limit, static_cast<int>(orders.size()));
        
        for (int i = 0; i < count; ++i) {
            result.push_back(orders[orders.size() - 1 - i]);  // Most recent first
        }
        
        return result;
    }
    
    return {};
}

std::map<std::string, double> MarketDataInterface::getExchangeBalances(
    const core::ExchangeId& exchangeId) {
    
    std::shared_lock<std::shared_mutex> lock(balanceMutex_);
    
    auto it = balanceCache_.find(exchangeId);
    if (it != balanceCache_.end()) {
        return it->second;
    }
    
    return {};
}

void MarketDataInterface::registerMarketDataCallback(
    const std::function<void(const MarketData&)>& callback) {
    
    if (!subscriber_) {
        LOG_ERROR("Cannot register callback - subscriber not initialized");
        return;
    }
    
    subscriber_->registerCallback(callback);
}

bool MarketDataInterface::subscribeMarketData(
    const core::Symbol& tradingPair, 
    const std::optional<std::string>& exchangeId) {
    
    if (!initialized_.load() || !subscriber_) {
        LOG_ERROR("Cannot subscribe - MarketDataInterface not initialized");
        return false;
    }
    
    return subscriber_->subscribe(tradingPair, exchangeId);
}

bool MarketDataInterface::unsubscribeMarketData(
    const core::Symbol& tradingPair, 
    const std::optional<std::string>& exchangeId) {
    
    if (!subscriber_) {
        return false;
    }
    
    return subscriber_->unsubscribe(tradingPair, exchangeId);
}

bool MarketDataInterface::isConnected() const {
    return initialized_.load() && subscriber_ && subscriber_->isConnected();
}

std::map<std::string, double> MarketDataInterface::getTradingFees(
    const core::ExchangeId& exchangeId, 
    const std::optional<std::string>& tradingPair) {
    
    // Default fee structure - in production this would come from exchange API
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

void MarketDataInterface::updateBalance(const core::ExchangeId& exchangeId, 
                                       const std::string& asset, 
                                       double balance) {
    std::unique_lock<std::shared_mutex> lock(balanceMutex_);
    balanceCache_[exchangeId][asset] = balance;
}

double MarketDataInterface::getBalance(const core::ExchangeId& exchangeId, 
                                      const std::string& asset) const {
    std::shared_lock<std::shared_mutex> lock(balanceMutex_);
    
    auto exchangeIt = balanceCache_.find(exchangeId);
    if (exchangeIt != balanceCache_.end()) {
        auto assetIt = exchangeIt->second.find(asset);
        if (assetIt != exchangeIt->second.end()) {
            return assetIt->second;
        }
    }
    return 0.0;
}

std::shared_ptr<DataInterface> MarketDataInterface::createDataInterface(
    const RiskConfig& config) {
    std::lock_guard<std::mutex> lg(g_marketDataInterface_singleton_mutex);
    if (g_marketDataInterface_singleton) return g_marketDataInterface_singleton;

    auto instance = std::make_shared<MarketDataInterface>(config);
    if (!instance->initialize()) {
        LOG_ERROR("Failed to initialize MarketDataInterface");
        return nullptr;
    }
    g_marketDataInterface_singleton = instance;
    LOG_INFO("MarketDataInterface singleton created (this={})", static_cast<const void*>(instance.get()));
    return g_marketDataInterface_singleton;
}

std::shared_ptr<DataInterface> MarketDataInterface::createDataInterface(
    const RiskConfig& config,
    std::unique_ptr<trading::communication::GenericZmqClient> injectedClient,
    std::shared_ptr<zmq::context_t> context) {
    std::lock_guard<std::mutex> lg(g_marketDataInterface_singleton_mutex);
    if (g_marketDataInterface_singleton) return g_marketDataInterface_singleton;

    auto instance = std::make_shared<MarketDataInterface>(config);
    // inject context and client before initialization
    if (context) instance->setZmqContext(context);
    if (injectedClient) instance->setZmqClient(std::move(injectedClient));

    if (!instance->initialize()) {
        LOG_ERROR("Failed to initialize MarketDataInterface (injected)");
        return nullptr;
    }
    g_marketDataInterface_singleton = instance;
    LOG_INFO("MarketDataInterface singleton (injected) created (this={})", static_cast<const void*>(instance.get()));
    return g_marketDataInterface_singleton;
}

void MarketDataInterface::setZmqClient(
    std::unique_ptr<trading::communication::GenericZmqClient> zmqClient) {
    // Store injected client for use when creating/configuring subscriber
    injectedZmqClient_ = std::move(zmqClient);
    if (subscriber_) {
        subscriber_->setZmqClient(std::move(injectedZmqClient_));
    }
}

MarketDataInterface::HealthStatus MarketDataInterface::getHealthStatus() const {
    HealthStatus status;
    
    if (cache_) {
        status.cacheStats = cache_->getStats();
        status.cacheHealthy = status.cacheStats.validEntries > 0;
    }
    
    if (subscriber_) {
        status.subscriberStats = subscriber_->getStats();        // ✅ Now works
        status.subscriberHealthy = subscriber_->isConnected();
        status.processorStats = subscriber_->getProcessorStats(); // ✅ Now works
    }
    
    status.restClientHealthy = true;  // Placeholder
    return status;
}

void MarketDataInterface::initializeComponents() {
    // Configure cache
    configureCache();
    
    // Configure subscriber
    configureSubscriber();
    
    // Configure REST client
    configureBinanceClient();
}

void MarketDataInterface::configureCache() {
    MarketDataCacheConfig cacheConfig;
    cacheConfig.cacheValidityDuration = std::chrono::milliseconds(
        config_.marketDataCacheDurationMs);
    cacheConfig.maxHistorySize = 1000;  // Could be configurable
    cacheConfig.enableVolatilityCalculation = true;
    
    cache_ = std::make_unique<MarketDataCache>(cacheConfig);
    LOG_INFO("MarketDataCache configured");
}

void MarketDataInterface::configureSubscriber() {
    SubscriberConfig subConfig;
    // subscriberEndpoint is the REQ endpoint the client uses to talk to server
    // publisherEndpoint is the PUB endpoint(s) the SUB socket should connect to
    subConfig.subscriberEndpoint = config_.marketDataRequestEndpoint; // server REQ/REP
    subConfig.publisherEndpoint = config_.zmqSubscriberEndpoint; // server PUB endpoint
    subConfig.enableConflation = true;
    subConfig.processingThreadCount = 8;  // Could be configurable

    LOG_INFO("Configuring MarketDataSubscriber: publisherEndpoint='{}' subscriberEndpoint='{}'",
             subConfig.publisherEndpoint, subConfig.subscriberEndpoint);
    subscriber_ = std::make_unique<MarketDataSubscriber>(subConfig);
    // If a full client was injected, hand it to the subscriber so it uses the
    // shared client/context (required for inproc transport). Otherwise set
    // the context if available.
    if (injectedZmqClient_) {
        subscriber_->setZmqClient(std::move(injectedZmqClient_));
    } else if (zmqContext_) {
        subscriber_->setZmqContext(zmqContext_);
    }
    
    LOG_INFO("MarketDataSubscriber configured");
}

void MarketDataInterface::configureBinanceClient() {
    std::string apiKey = std::getenv("BINANCE_API_KEY") ? 
        std::getenv("BINANCE_API_KEY") : "";
    std::string apiSecret = std::getenv("BINANCE_API_SECRET") ? 
        std::getenv("BINANCE_API_SECRET") : "";
    
    if (!apiKey.empty() && !apiSecret.empty()) {
        binanceClient_.setApiKey(apiKey, apiSecret);
        LOG_INFO("Binance client configured");
    } else {
        LOG_WARN("Binance API credentials not set");
    }
}

double MarketDataInterface::getPriceFromRest(const core::Symbol& tradingPair, const std::optional<std::string>& exchangeId) {
    try {
        // Normalize symbol string for REST client
        std::string sym = trading::execution::util::normalizeSymbolForExchange(tradingPair.toString());
        // Use the internal BinanceClient to fetch a synchronous price
        double p = 0.0;
        try {
            p = binanceClient_.getPrice(sym);
        } catch (const std::exception& ex) {
            LOG_WARN("Binance REST price lookup failed for {}: {}", sym, ex.what());
            return 0.0;
        }
        return p;
    } catch (const std::exception& ex) {
        LOG_ERROR("getPriceFromRest error for {}: {}", tradingPair.toString(), ex.what());
        return 0.0;
    }
}

void MarketDataInterface::onMarketDataReceived(const core::MarketData& data) {
    try {
        // Update cache
        core::ExchangeId exchange = data.exchangeId;
        core::Symbol symbol = data.tradingPair;
        std::string cacheKey = createCacheKey(exchange, symbol);
        //print for every 100 messages
        static int messageCount = 0;
        // if (++messageCount % 1 == 0) {
        //     LOG_DEBUG("Received market data for {}:{}, cacheKey={}", exchange.toString(), symbol.toString(), cacheKey);
        // }
        cache_->updateMarketData(cacheKey, data);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing received market data: {}", e.what());
    }
}

std::string MarketDataInterface::createCacheKey(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair) const {
    return MarketDataCache::createCacheKey(exchangeId, tradingPair);
}

} // namespace risk
} // namespace trading