/**
 * @file config.cpp
 * @brief Implementation of Configuration class for data ingestion
 */

#include "data_ingestion/config/config.hpp"
#include "common/logger.hpp"
#include <fstream>
#include <stdexcept>

namespace trading {
namespace data_ingestion {

Configuration& Configuration::getInstance() {
    static Configuration instance;
    return instance;
}

Configuration::Configuration() {
    // Initialize with default values
    initializeDefaults();
}

bool Configuration::loadFromFile(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open config file: " + configPath);
        }
        
        // For now, just return true. In real implementation,
        // you would parse JSON/YAML here
        return true;
    } catch (const std::exception& e) {
        // Log error
        return false;
    }
}

bool Configuration::loadFromJson(const std::string& json_string) {
    try {
        // For now, just return true. In real implementation,
        // you would parse JSON here
        return true;
    } catch (const std::exception& e) {
        // Log error
        return false;
    }
}

PublisherConfig Configuration::getPublisherConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return publisherConfig_;
}

ExchangeConfig Configuration::getExchangeConfig(const std::string& exchangeId) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = exchangeConfigs_.find(exchangeId);
    if (it != exchangeConfigs_.end()) {
        return it->second;
    }
    return ExchangeConfig{}; // Return default config
}

const ExchangeConfig& Configuration::getExchangeConfig(ExchangeId exchange) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = exchange_configs_.find(exchange);
    if (it != exchange_configs_.end()) {
        return it->second;
    }
    
    // Return a static default config to avoid dangling reference
    static ExchangeConfig defaultConfig;
    return defaultConfig;
}

NormalizationConfig Configuration::getNormalizationConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return normalizationConfig_;
}

StorageConfig Configuration::getStorageConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return storageConfig_;
}

const OrderBookConfig& Configuration::getOrderBookConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return order_book_config_;
}

const DataQualityConfig& Configuration::getDataQualityConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return data_quality_config_;
}

const TimeSyncConfig& Configuration::getTimeSyncConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return time_sync_config_;
}

std::vector<ExchangeId> Configuration::getEnabledExchanges() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return enabled_exchanges_;
}

void Configuration::setPublisherConfig(const PublisherConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    publisherConfig_ = config;
    
    // Sync the alias fields
    publisherConfig_.zmq_endpoint = config.zeromqEndpoint;
    publisherConfig_.zmq_bind_address = config.zeromqEndpoint;
    publisherConfig_.publish_interval_ms = static_cast<int>(config.publishInterval.count());
}

void Configuration::setExchangeConfig(const std::string& exchangeId, const ExchangeConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    exchangeConfigs_[exchangeId] = config;
    
    // Also store in enum-based map if possible
    // This would require a mapping from string to ExchangeId
}

void Configuration::setNormalizationConfig(const NormalizationConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    normalizationConfig_ = config;
}

void Configuration::setStorageConfig(const StorageConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    storageConfig_ = config;
}

void Configuration::initializeDefaults() {
    // Initialize default publisher config
    publisherConfig_.zeromqEndpoint = "tcp://*:5555";
    publisherConfig_.zmq_endpoint = "tcp://*:5555";
    publisherConfig_.zmq_bind_address = "tcp://*:5555";
    publisherConfig_.publisherType = "zeromq";
    publisherConfig_.bufferSize = 1000;
    publisherConfig_.max_queue_size = 1000;
    publisherConfig_.max_publish_rate = 1000;
    publisherConfig_.enableCompression = false;
    publisherConfig_.publish_interval_ms = 100;
    publisherConfig_.heartbeat_interval_ms = 30000;
    publisherConfig_.zmq_linger_ms = 1000;
    publisherConfig_.zmq_hwm = 1000;
    publisherConfig_.publish_raw_data = true;
    publisherConfig_.publish_normalized_data = true;
    publisherConfig_.compression = CompressionAlgorithm::NONE;
    
    // Initialize default normalization config
    normalizationConfig_.enableNormalization = true;
    normalizationConfig_.timestampFormat = "iso8601";
    normalizationConfig_.priceFormat = "decimal";
    normalizationConfig_.volumeFormat = "decimal";
    normalizationConfig_.validateChecksum = true;
    normalizationConfig_.maxDepth = 20;
    
    // Initialize default storage config
    storageConfig_.enableStorage = true;
    storageConfig_.storageType = "influxdb";
    storageConfig_.connectionString = "http://localhost:8086";
    storageConfig_.database = "trading_data";
    storageConfig_.retentionPolicy = "30d";
    storageConfig_.batchSize = 100;
    storageConfig_.flushInterval = std::chrono::seconds(10);
    
    // Initialize default order book config
    order_book_config_.depth = 20;
    order_book_config_.track_orders = false;
    order_book_config_.snapshot_interval_ms = 60000;
    order_book_config_.cleanup_interval_ms = 300000;
    
    // Initialize default data quality config
    data_quality_config_.max_price_deviation = 0.1;
    data_quality_config_.max_gap_ms = 5000;
    data_quality_config_.validation_level = 2;
    data_quality_config_.min_volume_threshold = 0.001;
    data_quality_config_.max_spread_percentage = 0.05;
    data_quality_config_.max_order_age_seconds = 300;
    data_quality_config_.min_order_size = 0.001;
    data_quality_config_.enable_sequence_validation = true;
    data_quality_config_.enable_price_range_filter = true;
    data_quality_config_.enable_order_age_filter = true;
    data_quality_config_.enable_checksum_filter = true;
    data_quality_config_.enable_sequence_gap_filter = true;
    data_quality_config_.max_sequence_gap = 100;
    
    // Initialize default time sync config
    time_sync_config_.ntp_server = "pool.ntp.org";
    time_sync_config_.sync_interval_ms = 300000;
    time_sync_config_.max_drift_ms = 1000;
    time_sync_config_.max_time_skew_ms = 5000;
    time_sync_config_.enable_ntp_sync = true;
    
    // Initialize default exchange configs
    ExchangeConfig binanceConfig;
    binanceConfig.exchangeId = "binance";
    binanceConfig.name = "Binance";
    binanceConfig.websocketUrl = "wss://stream.binance.com:9443/ws";
    binanceConfig.ws_url = "wss://stream.binance.com:9443/ws";
    binanceConfig.restUrl = "https://api.binance.com";
    binanceConfig.rest_url = "https://api.binance.com";
    binanceConfig.enableWebsocket = true;
    binanceConfig.enableRest = true;
    binanceConfig.reconnectDelay = std::chrono::seconds(5);
    binanceConfig.reconnect_interval_ms = 5000;
    binanceConfig.maxReconnectAttempts = 10;
    binanceConfig.max_reconnect_attempts = 10;
    binanceConfig.connection_timeout_ms = 30000;
    binanceConfig.use_authentication = false;
    exchangeConfigs_["binance"] = binanceConfig;
    
    ExchangeConfig coinbaseConfig;
    coinbaseConfig.exchangeId = "coinbase";
    coinbaseConfig.name = "Coinbase";
    coinbaseConfig.websocketUrl = "wss://ws-feed.pro.coinbase.com";
    coinbaseConfig.ws_url = "wss://ws-feed.pro.coinbase.com";
    coinbaseConfig.restUrl = "https://api.pro.coinbase.com";
    coinbaseConfig.rest_url = "https://api.pro.coinbase.com";
    coinbaseConfig.enableWebsocket = true;
    coinbaseConfig.enableRest = true;
    coinbaseConfig.reconnectDelay = std::chrono::seconds(5);
    coinbaseConfig.reconnect_interval_ms = 5000;
    coinbaseConfig.maxReconnectAttempts = 10;
    coinbaseConfig.max_reconnect_attempts = 10;
    coinbaseConfig.connection_timeout_ms = 30000;
    coinbaseConfig.use_authentication = false;
    exchangeConfigs_["coinbase"] = coinbaseConfig;
    
    // Initialize enabled exchanges
    enabled_exchanges_ = {
        core::ExchangeId::binance(),
        core::ExchangeId::coinbase()
    };
}

} // namespace data_ingestion
} // namespace trading