#pragma once

#include "../../generated/cpp/proto/trading_messages.pb.h"
#include "data_ingestion/orderbook/order_book.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>
#include <deque>
#include <fstream>
#include <numeric>
#include <filesystem>
using namespace trading::data_ingestion;

namespace trading {
namespace intelligence {

// Separate statistical models for adaptive learning
struct VolumeStatistics {
    std::deque<double> volume_history;
    double percentile_10 = 0;               // 10th percentile (thin threshold)
    double percentile_50 = 0;               // Median
    double percentile_90 = 0;               // 90th percentile (liquid threshold)
    size_t max_history_size = 500;
    
    void updateStatistics();
    void addVolume(double volume);
    double getVolumePercentile(double current_volume) const;
    void saveToStream(std::ostream& stream) const;
    void loadFromStream(std::istream& stream);
};

struct SpreadStatistics {
    std::deque<double> spread_history;
    double percentile_25 = 0;               // Tight spread threshold
    double percentile_75 = 0;               // Wide spread threshold  
    size_t max_history_size = 500;
    
    void updateStatistics();
    void addSpread(double spread);
    double getSpreadPercentile(double current_spread) const;
    void saveToStream(std::ostream& stream) const;
    void loadFromStream(std::istream& stream);
};

// Mutually exclusive regime probabilities
struct RegimeProbabilities {
    double liquid_prob = 0.0;
    double normal_prob = 0.0;
    double thin_prob = 0.0;
    double volatile_prob = 0.0;
    
    void normalize();
    std::pair<trading::OrderBookRegime, double> getBestRegime() const;
};

// Enhanced coin profile with persistent statistics
struct CoinProfile {
    std::string symbol;
    std::string market_tier;
    double volatility_factor = 1.0;
    
    // Statistical models instead of fixed thresholds
    VolumeStatistics volume_stats;
    SpreadStatistics spread_stats;
    
    // Persistence
    std::string persistence_file;
    
    // Backward compatibility fields (deprecated but kept for transition)
    double min_liquid_volume = 0;          // Deprecated: now computed from percentiles
    double min_thin_volume = 0;            // Deprecated: now computed from percentiles
    double tight_spread_threshold = 0;     // Deprecated: now computed from percentiles
    double wide_spread_threshold = 0;      // Deprecated: now computed from percentiles
    
    // Legacy EMA fields (deprecated)
    double volume_ema = 0;                 // Deprecated: replaced by volume_stats
    double spread_ema = 0;                 // Deprecated: replaced by spread_stats
    double depth_ema = 0;                  // Deprecated: not used
    int sample_count = 0;                  // Deprecated: replaced by history size
    
    // Constructor
    CoinProfile() = default;
    CoinProfile(const std::string& sym, const std::string& tier, double vol_factor);
    bool hasEnoughData() const;

    // Persistence methods
    void saveToFile() const;
    void loadFromFile();
   

    void updateStatistics(double volume, double spread);
};

// Simplified order book metrics
struct OrderBookMetrics {
    double bid_volume_5 = 0;
    double ask_volume_5 = 0;
    double total_volume = 0;
    double imbalance = 0;
    double depth_imbalance = 0;
};

// Flow analysis results
struct FlowMetrics {
    double net_flow_5s = 0;
    double net_flow_30s = 0;
    double aggressive_buy_ratio = 0.5;
    double aggressive_sell_ratio = 0.5;
};

// Regime classification result
struct RegimeResult {
    trading::OrderBookRegime regime;
    double confidence;
};

class AdaptiveMarketIntelligence {
public:
    AdaptiveMarketIntelligence();
    ~AdaptiveMarketIntelligence();
    
    // Primary interface methods
    trading::MarketIntelligence GenerateAdaptiveMarketIntelligence(
        const std::string& symbol,
        std::shared_ptr<OrderBook> orderbook);
    
    // New improved interface methods
    trading::MarketIntelligence generateIntelligence(
        const std::string& symbol,
        const std::vector<trading::PriceLevel>& bids,
        const std::vector<trading::PriceLevel>& asks);
    
    // Utility methods
    bool hasEnoughData() const;
    bool hasEnoughData(const std::string& symbol) const;
    void printStatistics(const std::string& symbol) const;
    // Utility methods
    // Debug and emergency methods (for backward compatibility)
    void DebugSampleCounts();
    void ForceAdaptiveMode();
    void ApplyEmergencyStaticThresholds();
    
    // Persistence control
    void saveAllProfiles();
    void setSaveFrequency(size_t frequency) { save_frequency_ = frequency; }

private:

    std::unordered_map<std::string, std::unique_ptr<CoinProfile>> profiles_;
    mutable std::mutex profiles_mutex_;  // Add 'mutable' keyword

    // Configuration
    size_t update_counter_ = 0;
    size_t save_frequency_ = 50;  // Save every N updates
    
    // Initialization methods
    void initializeProfiles();
    void InitializeDefaultProfiles();  // Legacy method name for compatibility
    
    // Profile management
    CoinProfile* getOrCreateProfile(const std::string& symbol);
    CoinProfile& GetOrCreateProfile(const std::string& symbol);  // Legacy method signature
    
    // Market analysis methods
    OrderBookMetrics calculateOrderBookMetrics(
        const std::vector<trading::PriceLevel>& bids,
        const std::vector<trading::PriceLevel>& asks) const;
    
    OrderBookMetrics CalculateOrderBookMetrics(  // Legacy method signature
        const std::vector<PriceLevel>& bids,
        const std::vector<PriceLevel>& asks);
    
    RegimeProbabilities calculateRegimeProbabilities(
        double volume_percentile,
        double spread_percentile, 
        double imbalance,
        double volatility_factor) const;
    
    // Legacy methods for backward compatibility
    double CalculateAdaptiveLiquidityRatio(double volume, const CoinProfile& profile);
    void UpdateProfile(CoinProfile& profile, double volume, double spread, double depth);
    void AdaptThresholds(CoinProfile& profile);
    
    double EstimateVolatility(const std::string& symbol, double spread,
                             std::shared_ptr<trading::data_ingestion::OrderBook> orderbook);
    
    FlowMetrics AnalyzeFlowMetrics(double imbalance, double volatility);
    
    RegimeResult ClassifyOrderBookRegimeAdaptive(
        const OrderBookMetrics& metrics,
        double spread,
        const CoinProfile& profile);
    
    std::vector<trading::PriceLevel> ConvertPriceLevels(
        const std::vector<trading::data_ingestion::PriceLevel>& input);
    
    // Utility methods
    double calculateLiquidityRatio(double volume, const CoinProfile& profile) const;
    double estimateSimpleVolatility(double spread, double volatility_factor) const;
    FlowMetrics estimateFlowMetrics(double imbalance, double total_volume) const;
};

} // namespace intelligence
} // namespace trading