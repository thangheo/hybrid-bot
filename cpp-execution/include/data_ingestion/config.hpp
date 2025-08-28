/**
 * @file config.hpp
 * @brief Configuration structures for all data ingestion components
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */
#ifndef DATA_INGESTION_CONFIG_HPP
#define DATA_INGESTION_CONFIG_HPP

#include <string>
#include <map>
#include <vector>
#include <memory>
// #include "core/market_types.hpp"
#include "core/market_types.hpp"
namespace trading {
namespace data_ingestion {

// Configuration for exchange connections
struct ExchangeConfig {
    ExchangeId id;
    std::string name;
    std::string ws_url;
    std::string rest_url;
    std::string api_key;
    std::string api_secret;
    int connection_timeout_ms;
    int reconnect_interval_ms;
    int max_reconnect_attempts;
    std::vector<Symbol> symbols;
    bool use_authentication;
    std::map<std::string, std::string> additional_params;
};

// Configuration for order book handling
struct OrderBookConfig {
    int depth;                 // Maximum depth to maintain
    bool track_orders;         // Whether to track individual orders
    int snapshot_interval_ms;  // How often to request full snapshots
    int cleanup_interval_ms;   // How often to clean up stale data
};

// Configuration for data quality control
struct DataQualityConfig {
    double max_price_deviation;  // Maximum allowed price deviation
    int max_gap_ms;              // Maximum allowed time gap between updates
    int validation_level;        // Level of validation to perform (0-3)
};

// Configuration for time synchronization
struct TimeSyncConfig {
    std::string ntp_server;
    int sync_interval_ms;
    int max_drift_ms;
};

// Configuration for data publishing
struct PublisherConfig {
    std::string zmq_bind_address;
    int publish_interval_ms;
    bool publish_raw_data;
    bool publish_normalized_data;
    std::vector<std::string> topics;
};

// Main configuration class
class Configuration {
public:
    static Configuration& getInstance();
    
    bool loadFromFile(const std::string& filename);
    bool loadFromJson(const std::string& json_string);
    
    const ExchangeConfig& getExchangeConfig(ExchangeId exchange) const;
    const OrderBookConfig& getOrderBookConfig() const;
    const DataQualityConfig& getDataQualityConfig() const;
    const TimeSyncConfig& getTimeSyncConfig() const;
    const PublisherConfig& getPublisherConfig() const;
    
    std::vector<ExchangeId> getEnabledExchanges() const;
    
private:
    Configuration();
    Configuration(const Configuration&) = delete;
    Configuration& operator=(const Configuration&) = delete;
    
    std::map<ExchangeId, ExchangeConfig> exchange_configs_;
    OrderBookConfig order_book_config_;
    DataQualityConfig data_quality_config_;
    TimeSyncConfig time_sync_config_;
    PublisherConfig publisher_config_;
    std::vector<ExchangeId> enabled_exchanges_;
};

} // namespace data_ingestion
} // namespace trading
#endif // DATA_INGESTION_CONFIG_HPP