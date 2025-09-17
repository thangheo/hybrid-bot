/**
 * @file config.hpp
 * @brief Configuration management for data ingestion component
 */

#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <mutex>
#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "communication/zmq_server.hpp"
#include "communication/zmq_config.hpp"
// Forward declaration for nlohmann::json
// using ZmqServerConfig = trading::common::ZmqServerConfig;

// Forward declaration for nlohmann::json
namespace nlohmann {
    // class basic_json;
    using json = basic_json<>;
}
namespace trading {
namespace data_ingestion {


/**
 * Configuration for exchange connections
 */
struct ExchangeConfig {
    // Basic identification
    ExchangeId id;
    std::string exchangeId;  // String version for compatibility
    std::string name;
    
    // Connection URLs
    std::string websocketUrl;
    std::string ws_url;  // Alias for compatibility
    std::string restUrl;
    std::string rest_url;  // Alias for compatibility
    
    // Authentication
    std::string api_key;
    std::string api_secret;
    bool use_authentication = false;
    bool enableWebsocket = true;
    bool enableRest = true;
    
    // Connection management
    std::chrono::seconds reconnectDelay{5};
    int connection_timeout_ms = 30000;
    int reconnect_interval_ms = 5000;
    int maxReconnectAttempts = 10;
    int max_reconnect_attempts = 10;  // Alias for compatibility
    
    // Trading pairs
    std::vector<Symbol> symbols;
    
    // Additional parameters
    std::map<std::string, std::string> apiCredentials;
    std::map<std::string, std::string> additional_params;
};

/**
 * Configuration for data normalization
 */
struct NormalizationConfig {
    bool enableNormalization = true;
    std::string timestampFormat = "iso8601";
    std::string priceFormat = "decimal";
    std::string volumeFormat = "decimal";
    bool validateChecksum = true;
    int maxDepth = 20;
};

/**
 * Configuration for order book handling
 */
struct OrderBookConfig {
    int depth = 20;                 // Maximum depth to maintain
    bool track_orders = false;      // Whether to track individual orders
    int snapshot_interval_ms = 60000;  // How often to request full snapshots
    int cleanup_interval_ms = 300000;   // How often to clean up stale data
};

/**
 * Configuration for data quality control
 */
struct DataQualityConfig {
    double max_price_deviation = 0.1;  // Maximum allowed price deviation (10%)
    int max_gap_ms = 5000;              // Maximum allowed time gap between updates
    int validation_level = 2;           // Level of validation to perform (0-3)
    double min_volume_threshold = 0.001; // Minimum volume threshold
    double max_spread_percentage = 0.05; // Maximum allowed spread percentage (5%)
    double max_order_age_seconds = 300;  // Maximum age of orders to consider
    double min_order_size = 0.001;       // Minimum order size to consider
    
    // Feature flags
    bool enable_sequence_validation = true;
    bool enable_price_range_filter = true;
    bool enable_order_age_filter = true;
    bool enable_checksum_filter = true;
    bool enable_sequence_gap_filter = true;
    
    // Range limits
    std::vector<double> max_prices;
    std::vector<double> min_prices;
    double max_sequence_gap = 100;
    
    // Custom validation rules
    std::map<std::string, std::string> validation_rules;
};

/**
 * Configuration for time synchronization
 */
struct TimeSyncConfig {
    std::string ntp_server = "pool.ntp.org";
    int sync_interval_ms = 300000;  // 5 minutes
    int max_drift_ms = 1000;        // 1 second
    int max_time_skew_ms = 5000;    // 5 seconds
    bool enable_ntp_sync = true;
};

/**
 * Configuration for data storage
 */
struct StorageConfig {
    bool enableStorage = true;
    std::string storageType = "influxdb";
    std::string connectionString = "http://localhost:8086";
    std::string database = "trading_data";
    std::string retentionPolicy = "30d";
    int batchSize = 100;
    std::chrono::seconds flushInterval{10};
};

/**
 * Central configuration manager for data ingestion
 */
class Configuration {
public:
    // Singleton access
    static Configuration& getInstance();
    
    // Configuration loading
    bool loadFromFile(const std::string& configPath);
    bool loadFromJson(const std::string& json_string);
    
    // Getters
    ZmqServerConfig getPublisherConfig() const;
    ExchangeConfig getExchangeConfig(const std::string& exchangeId) const;
    const ExchangeConfig& getExchangeConfig(ExchangeId exchange) const;
    NormalizationConfig getNormalizationConfig() const;
    StorageConfig getStorageConfig() const;
    const OrderBookConfig& getOrderBookConfig() const;
    const DataQualityConfig& getDataQualityConfig() const;
    const TimeSyncConfig& getTimeSyncConfig() const;
    
    // Get enabled exchanges
    std::vector<ExchangeId> getEnabledExchanges() const;
    
    // Setters
    void setPublisherConfig(const ZmqServerConfig& config);
    void setExchangeConfig(const std::string& exchangeId, const ExchangeConfig& config);
    void setNormalizationConfig(const NormalizationConfig& config);
    void setStorageConfig(const StorageConfig& config);

private:
    Configuration();
    ~Configuration() = default;
    
    // Delete copy constructor and assignment operator
    Configuration(const Configuration&) = delete;
    Configuration& operator=(const Configuration&) = delete;
    
    void initializeDefaults();
    
    mutable std::mutex configMutex_;
    ZmqServerConfig publisherConfig_;
    std::map<std::string, ExchangeConfig> exchangeConfigs_;
    std::map<ExchangeId, ExchangeConfig> exchange_configs_;  // For enum-based access
    NormalizationConfig normalizationConfig_;
    StorageConfig storageConfig_;
    OrderBookConfig order_book_config_;
    DataQualityConfig data_quality_config_;
    TimeSyncConfig time_sync_config_;
    std::vector<ExchangeId> enabled_exchanges_;
    // Add these JSON loading helper method declarations:
   void loadPublisherConfig(const nlohmann::json& pubJson);
    void loadExchangeConfigs(const nlohmann::json& exchangesJson);
    void loadNormalizationConfig(const nlohmann::json& normJson);
    void loadOrderBookConfig(const nlohmann::json& obJson);
    void loadDataQualityConfig(const nlohmann::json& dqJson);
    void loadTimeSyncConfig(const nlohmann::json& tsJson);
    void loadStorageConfig(const nlohmann::json& storageJson);
    void loadEnabledExchanges(const nlohmann::json& enabledJson);
};

} // namespace data_ingestion
} // namespace trading