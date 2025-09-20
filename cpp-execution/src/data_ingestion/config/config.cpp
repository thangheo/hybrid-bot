/**
 * @file config.cpp
 * @brief Implementation of Configuration class for data ingestion
 */

#include "data_ingestion/config/config.hpp"
#include "common/logger.hpp"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <nlohmann/json.hpp>
#include "communication/zmq_config.hpp"

using namespace trading::communication;

namespace trading {
namespace data_ingestion {

Configuration& Configuration::getInstance() {
    static Configuration instance;
    return instance;
}

Configuration::Configuration() {
    initializeDefaults();
}

bool Configuration::loadFromFile(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "Cannot open config file: " << configPath << std::endl;
            return false;
        }
        
        nlohmann::json configJson;
        file >> configJson;
        
        return loadFromJson(configJson.dump());
        
    } catch (const std::exception& e) {
        std::cerr << "Config loading error: " << e.what() << std::endl;
        return false;
    }
}

bool Configuration::loadFromJson(const std::string& jsonString) {
    try {
        auto configJson = nlohmann::json::parse(jsonString);
        std::lock_guard<std::mutex> lock(configMutex_);
        
        if (configJson.contains("publisher")) {
            loadPublisherConfig(configJson["publisher"]);
        }
        
        if (configJson.contains("exchanges")) {
            loadExchangeConfigs(configJson["exchanges"]);
        }
        
        if (configJson.contains("normalization")) {
            loadNormalizationConfig(configJson["normalization"]);
        }
        
        if (configJson.contains("order_book")) {
            loadOrderBookConfig(configJson["order_book"]);
        }
        
        if (configJson.contains("data_quality")) {
            loadDataQualityConfig(configJson["data_quality"]);
        }
        
        if (configJson.contains("time_sync")) {
            loadTimeSyncConfig(configJson["time_sync"]);
        }
        
        if (configJson.contains("storage")) {
            loadStorageConfig(configJson["storage"]);
        }
        
        if (configJson.contains("enabled_exchanges")) {
            loadEnabledExchanges(configJson["enabled_exchanges"]);
        }
       
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        return false;
    }
}

void Configuration::loadPublisherConfig(const nlohmann::json& pubJson) {

    publisherConfig_.intel_endpoint = pubJson.value("market_intel_endpoint", "tcp://*:5555");
    publisherConfig_.internal_endpoint = pubJson.value("orderbook_endpoint", "tcp://*:5557");
    publisherConfig_.request_endpoint = pubJson.value("trading_intent_endpoint", "tcp://*:5556");
    // publisherConfig_. = pubJson.value("publisher_type", "zeromq");
    // publisherConfig_.bufferSize = pubJson.value("buffer_size", 10000);
    publisherConfig_.max_intel_queue_size = pubJson.value("market_intel_max_queue_size", 10);
    publisherConfig_.max_internal_queue_size = pubJson.value("orderbook_max_queue_size", 1000);
    // publisherConfig_.re = pubJson.value("trading_intent_max_queue_size", 10);
    publisherConfig_.max_intel_publish_rate = pubJson.value("intel_max_publish_rate", 50);
    publisherConfig_.max_internal_publish_rate = pubJson.value("internal_max_publish_rate", 5000);
    // publisherConfig_.req = pubJson.value("trading_intent_max_publish_rate", 1000);
    publisherConfig_.intel_publish_interval = std::chrono::milliseconds(pubJson.value("intel_publish_interval_ms", 15000));
    publisherConfig_.internal_publish_interval = std::chrono::milliseconds(pubJson.value("internal_publish_interval_ms", 10));
    // publisherConfig_.req = std::chrono::milliseconds(pubJson.value("trading_intent_publish_interval_ms", 1000));
    publisherConfig_.queue_timeout = std::chrono::milliseconds(pubJson.value("queue_timeout_ms", 100));
    publisherConfig_.intel_hwm = pubJson.value("intel_zmq_hwm", 10000);
    publisherConfig_.internal_hwm = pubJson.value("internal_zmq_hwm", 10000);
    publisherConfig_.request_hwm = pubJson.value("trading_intent_zmq_hwm", 10000);
    publisherConfig_.intel_linger = pubJson.value("intel_zmq_linger_ms", 1000);
    publisherConfig_.internal_linger = pubJson.value("internal_zmq_linger_ms", 1000);
    publisherConfig_.request_linger = pubJson.value("trading_intent_zmq_linger_ms", 1000);
    publisherConfig_.intel_immediate = pubJson.value("intel_immediate", true);
    publisherConfig_.internal_immediate = pubJson.value("internal_immediate", true);
    publisherConfig_.request_hwm = pubJson.value("request_hwm", 1000);
    publisherConfig_.request_timeout_ms = pubJson.value("request_timeout_ms", 30000);
    publisherConfig_.intel_publisher_threads = pubJson.value("publisher_threads", 2);
    publisherConfig_.request_handler_threads = pubJson.value("request_handler_threads", 4);
    publisherConfig_.io_threads = pubJson.value("io_threads", 2);
    publisherConfig_.enable_monitoring = pubJson.value("enable_monitoring", true);
    publisherConfig_.stats_interval = std::chrono::seconds(pubJson.value("stats_interval_seconds", 60));
    publisherConfig_.enable_heartbeat = pubJson.value("enable_heartbeat", true);
    
}

void Configuration::loadExchangeConfigs(const nlohmann::json& exchangesJson) {
    exchangeConfigs_.clear();
    exchange_configs_.clear();
    
    for (auto& [exchangeName, exchangeJson] : exchangesJson.items()) {
        ExchangeConfig config;
        
        std::string exchangeIdStr = exchangeJson.value("exchange_id", exchangeName);
        config.exchangeId = exchangeIdStr;
        config.id = ExchangeId::fromString(exchangeIdStr);
        config.name = exchangeJson.value("name", exchangeName);
        
        config.websocketUrl = exchangeJson.value("ws_url", "");
        config.ws_url = config.websocketUrl;
        config.restUrl = exchangeJson.value("rest_url", "");
        config.rest_url = config.restUrl;
        
        config.enableWebsocket = exchangeJson.value("enable_websocket", true);
        config.enableRest = exchangeJson.value("enable_rest", true);
        config.connection_timeout_ms = exchangeJson.value("connection_timeout_ms", 30000);
        config.reconnect_interval_ms = exchangeJson.value("reconnect_interval_ms", 5000);
        config.reconnectDelay = std::chrono::seconds(config.reconnect_interval_ms / 1000);
        config.maxReconnectAttempts = exchangeJson.value("max_reconnect_attempts", 10);
        config.max_reconnect_attempts = config.maxReconnectAttempts;
        
        config.use_authentication = exchangeJson.value("use_authentication", false);
        config.api_key = exchangeJson.value("api_key", "");
        config.api_secret = exchangeJson.value("api_secret", "");
        
        if (exchangeJson.contains("symbols") && exchangeJson["symbols"].is_array()) {
            config.symbols.clear();
            for (const auto& symbolJson : exchangeJson["symbols"]) {
                if (symbolJson.is_object()) {
                    std::string base = symbolJson.value("base", "");
                    std::string quote = symbolJson.value("quote", "");
                    config.symbols.emplace_back(base, quote);
                }
            }
        }
        
        exchangeConfigs_[exchangeName] = config;
        exchange_configs_[config.id] = config;
    }
}

void Configuration::loadNormalizationConfig(const nlohmann::json& normJson) {
    normalizationConfig_.enableNormalization = normJson.value("enable_normalization", true);
    normalizationConfig_.timestampFormat = normJson.value("timestamp_format", "iso8601");
    normalizationConfig_.priceFormat = normJson.value("price_format", "decimal");
    normalizationConfig_.volumeFormat = normJson.value("volume_format", "decimal");
    normalizationConfig_.validateChecksum = normJson.value("validate_checksum", true);
    normalizationConfig_.maxDepth = normJson.value("max_depth", 20);
}

void Configuration::loadOrderBookConfig(const nlohmann::json& obJson) {
    order_book_config_.depth = obJson.value("depth", 20);
    order_book_config_.track_orders = obJson.value("track_orders", false);
    order_book_config_.snapshot_interval_ms = obJson.value("snapshot_interval_ms", 60000);
    order_book_config_.cleanup_interval_ms = obJson.value("cleanup_interval_ms", 300000);
}

void Configuration::loadDataQualityConfig(const nlohmann::json& dqJson) {
    data_quality_config_.max_price_deviation = dqJson.value("max_price_deviation", 0.1);
    data_quality_config_.max_gap_ms = dqJson.value("max_gap_ms", 5000);
    data_quality_config_.validation_level = dqJson.value("validation_level", 2);
    data_quality_config_.min_volume_threshold = dqJson.value("min_volume_threshold", 0.001);
    data_quality_config_.max_spread_percentage = dqJson.value("max_spread_percentage", 0.05);
    data_quality_config_.max_order_age_seconds = dqJson.value("max_order_age_seconds", 300.0);
    data_quality_config_.min_order_size = dqJson.value("min_order_size", 0.001);
    data_quality_config_.enable_sequence_validation = dqJson.value("enable_sequence_validation", true);
    data_quality_config_.enable_price_range_filter = dqJson.value("enable_price_range_filter", true);
    data_quality_config_.enable_order_age_filter = dqJson.value("enable_order_age_filter", true);
    data_quality_config_.enable_checksum_filter = dqJson.value("enable_checksum_filter", true);
    data_quality_config_.enable_sequence_gap_filter = dqJson.value("enable_sequence_gap_filter", true);
    data_quality_config_.max_sequence_gap = dqJson.value("max_sequence_gap", 100.0);
}

void Configuration::loadTimeSyncConfig(const nlohmann::json& tsJson) {
    time_sync_config_.ntp_server = tsJson.value("ntp_server", "pool.ntp.org");
    time_sync_config_.sync_interval_ms = tsJson.value("sync_interval_ms", 300000);
    time_sync_config_.max_drift_ms = tsJson.value("max_drift_ms", 1000);
    time_sync_config_.max_time_skew_ms = tsJson.value("max_time_skew_ms", 5000);
    time_sync_config_.enable_ntp_sync = tsJson.value("enable_ntp_sync", true);
}

void Configuration::loadStorageConfig(const nlohmann::json& storageJson) {
    storageConfig_.enableStorage = storageJson.value("enable_storage", false);
    storageConfig_.storageType = storageJson.value("storage_type", "influxdb");
    storageConfig_.connectionString = storageJson.value("connection_string", "http://localhost:8086");
    storageConfig_.database = storageJson.value("database", "crypto_trading");
    storageConfig_.retentionPolicy = storageJson.value("retention_policy", "30d");
    storageConfig_.batchSize = storageJson.value("batch_size", 100);
    
    int flushSeconds = storageJson.value("flush_interval_seconds", 10);
    storageConfig_.flushInterval = std::chrono::seconds(flushSeconds);
}

void Configuration::loadEnabledExchanges(const nlohmann::json& enabledJson) {
    enabled_exchanges_.clear();
    if (enabledJson.is_array()) {
        for (const auto& exchangeStr : enabledJson) {
            std::string exchStr = exchangeStr.get<std::string>();
            ExchangeId exchId = ExchangeId::fromString(exchStr);
            if (exchId.isValid()) {
                enabled_exchanges_.push_back(exchId);
            }
        }
    }
}

// All your existing getter and setter methods remain unchanged
ZmqServerConfig Configuration::getPublisherConfig() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return publisherConfig_;
}

ExchangeConfig Configuration::getExchangeConfig(const std::string& exchangeId) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = exchangeConfigs_.find(exchangeId);
    if (it != exchangeConfigs_.end()) {
        return it->second;
    }
    return ExchangeConfig{};
}

const ExchangeConfig& Configuration::getExchangeConfig(ExchangeId exchange) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = exchange_configs_.find(exchange);
    if (it != exchange_configs_.end()) {
        return it->second;
    }
    
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

void Configuration::setPublisherConfig(const ZmqServerConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    publisherConfig_ = config;
    // publisherConfig_.enable_monitoring = config.enable_monitoring;
    // why? we just copied the whole struct
}

void Configuration::setExchangeConfig(const std::string& exchangeId, const ExchangeConfig& config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    exchangeConfigs_[exchangeId] = config;
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
        
    normalizationConfig_.enableNormalization = true;
    normalizationConfig_.timestampFormat = "iso8601";
    normalizationConfig_.priceFormat = "decimal";
    normalizationConfig_.volumeFormat = "decimal";
    normalizationConfig_.validateChecksum = true;
    normalizationConfig_.maxDepth = 20;
    
    storageConfig_.enableStorage = true;
    storageConfig_.storageType = "influxdb";
    storageConfig_.connectionString = "http://localhost:8086";
    storageConfig_.database = "trading_data";
    storageConfig_.retentionPolicy = "30d";
    storageConfig_.batchSize = 100;
    storageConfig_.flushInterval = std::chrono::seconds(10);
    
    order_book_config_.depth = 20;
    order_book_config_.track_orders = false;
    order_book_config_.snapshot_interval_ms = 60000;
    order_book_config_.cleanup_interval_ms = 300000;
    
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
    
    time_sync_config_.ntp_server = "pool.ntp.org";
    time_sync_config_.sync_interval_ms = 300000;
    time_sync_config_.max_drift_ms = 1000;
    time_sync_config_.max_time_skew_ms = 5000;
    time_sync_config_.enable_ntp_sync = true;
    
    enabled_exchanges_ = {
        ExchangeId::binance(),
        ExchangeId::coinbase()
    };
}

} // namespace data_ingestion
} // namespace trading