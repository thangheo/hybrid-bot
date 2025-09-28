#include "risk/interfaces/market_data_interface.hpp"
#include "common/logger.hpp"
#include <algorithm>

namespace trading {
namespace risk {

MarketDataInterface::MarketDataInterface() {
    LOG_INFO("MarketDataInterface default constructor");
}

MarketDataInterface::MarketDataInterface(const RiskConfig& config) 
    : config_(config) {
    defaultExchange_ = config.defaultExchange;
    LOG_INFO("MarketDataInterface configured with default exchange: {}", defaultExchange_);
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
    
    auto instance = std::make_shared<MarketDataInterface>(config);
    if (!instance->initialize()) {
        LOG_ERROR("Failed to initialize MarketDataInterface");
        return nullptr;
    }
    return instance;
}

void MarketDataInterface::setZmqClient(
    std::unique_ptr<trading::communication::GenericZmqClient> zmqClient) {
    
    if (subscriber_) {
        subscriber_->setZmqClient(std::move(zmqClient));
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
    subConfig.subscriberEndpoint = config_.zmqSubscriberEndpoint;
    subConfig.publisherEndpoint = config_.zmqPublisherEndpoint;
    subConfig.enableConflation = true;
    subConfig.processingThreadCount = 8;  // Could be configurable
    
    subscriber_ = std::make_unique<MarketDataSubscriber>(subConfig);
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

void MarketDataInterface::onMarketDataReceived(const core::MarketData& data) {
    try {
        // Update cache
        core::ExchangeId exchange = data.exchangeId;
        core::Symbol symbol = data.tradingPair;
        std::string cacheKey = createCacheKey(exchange, symbol);
        
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