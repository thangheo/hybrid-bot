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
using namespace trading::data_ingestion;

namespace trading {
namespace intelligence {

struct OrderBookMetrics {
    double bid_volume_5 = 0;
    double ask_volume_5 = 0;
    double bid_volume_10 = 0;
    double ask_volume_10 = 0;
    double total_volume = 0;
    double imbalance = 0;
    double depth_imbalance = 0;
    double weighted_mid = 0;
};

struct FlowMetrics {
    double net_flow_5s = 0;
    double net_flow_30s = 0;
    double aggressive_buy_ratio = 0.5;
    double aggressive_sell_ratio = 0.5;
};

struct RegimeResult {
    trading::OrderBookRegime regime;
    double confidence;
};
struct CoinProfile {
    std::string symbol;
    std::string market_tier;
    double avg_volume_24h;
    double typical_spread;
    double typical_depth;
    double volatility_factor;
    double min_liquid_volume;
    double min_thin_volume;
    double tight_spread_threshold;
    double wide_spread_threshold;
    
    double volume_ema = 0;
    double spread_ema = 0;
    double depth_ema = 0;
    int sample_count = 0;
};
 
 
class AdaptiveMarketIntelligence {
    
public:
    AdaptiveMarketIntelligence();
    ~AdaptiveMarketIntelligence() = default;
    
    trading::MarketIntelligence GenerateAdaptiveMarketIntelligence(
    const std::string& symbol,
    std::shared_ptr<OrderBook> orderbook) ;

private:
    std::unordered_map<std::string, CoinProfile> profiles_;
    std::mutex profiles_mutex_;
    void InitializeDefaultProfiles_conservative() ;

    void InitializeDefaultProfiles();
    CoinProfile& GetOrCreateProfile(const std::string& symbol);
    void UpdateProfile(CoinProfile& profile, double volume, double spread, double depth);
    void AdaptThresholds(CoinProfile& profile);
    
    // OrderBookMetrics CalculateOrderBookMetrics(
    //     const std::vector<trading::data_ingestion::PriceLevel>& bids,
    //     const std::vector<trading::data_ingestion::PriceLevel>& asks
    // );
        
    OrderBookMetrics CalculateOrderBookMetrics(
        const std::vector<PriceLevel>& bids,
        const std::vector<PriceLevel>& asks) ;
        
    double CalculateAdaptiveLiquidityRatio(double volume, const CoinProfile& profile);
    double EstimateVolatility(const std::string& symbol, double spread, 
                            std::shared_ptr<trading::data_ingestion::OrderBook> orderbook);
    FlowMetrics AnalyzeFlowMetrics(double imbalance, double volatility);
    RegimeResult ClassifyOrderBookRegimeAdaptive(
        const OrderBookMetrics& metrics,
        double spread,
        const CoinProfile& profile
    );
    
    std::vector<trading::PriceLevel> ConvertPriceLevels(
        const std::vector<trading::data_ingestion::PriceLevel>& input
    );
};

} // namespace intelligence
} // namespace trading