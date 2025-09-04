#include "market_intelligence/adaptive_intelligence.hpp"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <data_ingestion/orderbook/order_book.hpp>

#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cmath>

using namespace trading::data_ingestion;

namespace trading {
namespace intelligence {
 
   
AdaptiveMarketIntelligence::AdaptiveMarketIntelligence() {
    InitializeDefaultProfiles();
}
    void AdaptiveMarketIntelligence::InitializeDefaultProfiles() {
    // Generated based on comprehensive market analysis for Q1 2025
    // Data sources: CoinMarketCap, Binance market data, trading volume analysis
    // Methodology: Tier-based classification with volatility-adjusted parameters
    
    // TIER 1 - Major Caps (Highest liquidity, ETF correlation)
    profiles_["BTCUSDT"] = {
        "BTCUSDT", "btc", 80000000000, 0.00001, 4629630, 1.0,
        1000000000, 166666667, 0.000005, 0.00003
    };
    profiles_["ETHUSDT"] = {
        "ETHUSDT", "major", 25000000000, 0.000154, 868056, 1.3,
        208333333, 31250000, 0.000077, 0.000462
    };

    // TIER 2 - Large Caps (Strong institutional interest)
    profiles_["SOLUSDT"] = {
        "SOLUSDT", "major", 8000000000, 0.000135, 277778, 1.3,
        66666667, 10000000, 0.000068, 0.000405
    };
    profiles_["XRPUSDT"] = {
        "XRPUSDT", "major", 12000000000, 0.00015, 416667, 1.3,
        100000000, 15000000, 0.000075, 0.00045
    };
    profiles_["ADAUSDT"] = {
        "ADAUSDT", "alt", 2500000000, 0.0002, 57870, 1.8,
        15625000, 2083333, 0.0001, 0.0006
    };
    profiles_["AVAXUSDT"] = {
        "AVAXUSDT", "alt", 1200000000, 0.01, 27778, 1.8,
        7500000, 1000000, 0.005, 0.03
    };
    profiles_["DOTUSDT"] = {
        "DOTUSDT", "alt", 800000000, 0.0017, 18519, 1.8,
        5000000, 666667, 0.00085, 0.0051
    };
    profiles_["LINKUSDT"] = {
        "LINKUSDT", "alt", 1000000000, 0.0058, 23148, 1.8,
        6250000, 833333, 0.0029, 0.0174
    };
    profiles_["UNIUSDT"] = {
        "UNIUSDT", "defi", 600000000, 0.0042, 10417, 2.5,
        2500000, 375000, 0.0021, 0.0126
    };

    // TIER 3 - Mid Caps (Growing ecosystems)
    profiles_["ATOMUSDT"] = {
        "ATOMUSDT", "alt", 200000000, 0.0017, 4630, 1.8,
        1250000, 166667, 0.00085, 0.0051
    };
    profiles_["ALGOUSDT"] = {
        "ALGOUSDT", "alt", 150000000, 0.00007, 3472, 1.8,
        937500, 125000, 0.000035, 0.00021
    };
    profiles_["AAVEUSDT"] = {
        "AAVEUSDT", "defi", 400000000, 0.105, 6944, 2.5,
        1666667, 250000, 0.0525, 0.315
    };

    // TIER 4 - Layer 2 & Scaling (High growth potential)
    profiles_["ARBUSDT"] = {
        "ARBUSDT", "l2", 300000000, 0.00025, 6250, 2.2,
        1500000, 225000, 0.000125, 0.00075
    };
    profiles_["OPUSDT"] = {
        "OPUSDT", "l2", 250000000, 0.000625, 5208, 2.2,
        1250000, 187500, 0.000313, 0.001875
    };
    profiles_["INJUSDT"] = {
        "INJUSDT", "defi", 180000000, 0.0081, 3125, 2.5,
        750000, 112500, 0.00405, 0.0243
    };
    profiles_["SUIUSDT"] = {
        "SUIUSDT", "l2", 1800000000, 0.001175, 37500, 2.2,
        9000000, 1350000, 0.000588, 0.003525
    };

    // TIER 5 - Specialized/Oracle/Infrastructure
    profiles_["TRBUSDT"] = {
        "TRBUSDT", "oracle", 50000000, 0.04, 579, 3.0,
        166667, 20833, 0.02, 0.12
    };
    profiles_["BANDUSDT"] = {
        "BANDUSDT", "oracle", 30000000, 0.00064, 347, 3.0,
        100000, 12500, 0.00032, 0.00192
    };
    profiles_["CHZUSDT"] = {
        "CHZUSDT", "utility", 60000000, 0.000035, 833, 2.8,
        225000, 30000, 0.000018, 0.000105
    };
    profiles_["ENJUSDT"] = {
        "ENJUSDT", "utility", 40000000, 0.000087, 556, 2.8,
        150000, 20000, 0.000044, 0.000262
    };

    // TIER 6 - DeFi Protocols (High volatility, regime-sensitive)
    profiles_["YFIUSDT"] = {
        "YFIUSDT", "defi", 100000000, 11.1, 1736, 2.5,
        416667, 62500, 5.55, 33.3
    };
    profiles_["1INCHUSDT"] = {
        "1INCHUSDT", "defi", 50000000, 0.00018, 868, 2.5,
        208333, 31250, 0.00009, 0.00054
    };

    // TIER 7 - Emerging/Trending (High beta to market regimes)
    profiles_["DOGEUSDT"] = {
        "DOGEUSDT", "meme", 5000000000, 0.000175, 46296, 4.0,
        10416667, 1666667, 0.000087, 0.000525
    };
    profiles_["WIFUSDT"] = {
        "WIFUSDT", "meme", 200000000, 0.001075, 1852, 4.0,
        416667, 66667, 0.000538, 0.003225
    };

    /*
    Profile Parameter Explanation:
    
    1. symbol: Token symbol (e.g., "BTCUSDT")
    2. market_tier: Classification ("btc", "major", "alt", "defi", "l2", "oracle", "utility", "meme")
    3. avg_volume_24h: Average 24-hour trading volume in USD
    4. typical_spread: Typical bid-ask spread as ratio of price
    5. typical_depth: Typical order book depth in USD
    6. volatility_factor: Multiplier for volatility (BTC=1.0, others scaled relative)
    7. min_liquid_volume: Minimum volume threshold for LIQUID regime (USD/hour)
    8. min_thin_volume: Minimum volume threshold for THIN regime (USD/hour)  
    9. tight_spread_threshold: Threshold for tight spread classification
    10. wide_spread_threshold: Threshold for wide spread classification
    
    Tier Characteristics:
    - BTC: Ultra-stable, highest liquidity, volatility factor 1.0
    - MAJOR: High liquidity, institutional adoption, volatility factor 1.3
    - ALT: Medium liquidity, established projects, volatility factor 1.8
    - DEFI: Protocol volatility, regime-sensitive, volatility factor 2.5
    - L2: Scaling solutions, growth potential, volatility factor 2.2
    - ORACLE: Specialized use case, lower liquidity, volatility factor 3.0
    - UTILITY: Gaming/entertainment tokens, volatility factor 2.8
    - MEME: Highest volatility, sentiment-driven, volatility factor 4.0
    
    Usage in Trading System:
    - Execution parameters automatically adjust based on tier
    - Risk management scales with volatility factors
    - Order book regime detection uses tier-specific thresholds
    - Market intelligence normalizes metrics per token characteristics
    */
}

    // Trong adaptive_intelligence.cpp
std::vector<trading::PriceLevel> AdaptiveMarketIntelligence::ConvertPriceLevels(
    const std::vector<trading::data_ingestion::PriceLevel>& input) {
    
    std::vector<trading::PriceLevel> result;
    result.reserve(input.size());
    
    for (const auto& level : input) {
        trading::PriceLevel converted;
        converted.set_price(level.getPrice());          // Hoặc level.price
        converted.set_quantity(level.getQuantity());    // Hoặc level.quantity  
        // converted.set_order_count(level.getOrderCount()); // Hoặc level.order_count
        result.push_back(converted);
    }
    
    return result;
}

    
MarketIntelligence AdaptiveMarketIntelligence::GenerateAdaptiveMarketIntelligence(
    const std::string& symbol,
    std::shared_ptr<OrderBook> orderbook) {
    
    MarketIntelligence intel;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
        
    intel.set_symbol(symbol);
    intel.set_timestamp(now);
    
    // Get or create profile for this symbol
    auto& profile = GetOrCreateProfile(symbol);
    
    // Get order book data
    auto bids = orderbook->getBids(10); // Get deeper book data
    auto asks = orderbook->getAsks(10);
    
    if (bids.empty() || asks.empty()) {
        return intel; // Return empty intelligence
    }
    
    // Basic calculations
    double best_bid = bids[0].getPrice();
    double best_ask = asks[0].getPrice();
    double spread = best_ask - best_bid;
    double mid_price = (best_bid + best_ask) / 2.0;
    double relative_spread = spread / mid_price;
    
    intel.set_bid_ask_spread(relative_spread);
    
    // Calculate order book depth and imbalance
    //     OrderBookMetrics CalculateOrderBookMetrics(
    // const std::vector<PriceLevel>& bids,
    // const std::vector<PriceLevel>& asks) {
    auto converted_bids = ConvertPriceLevels(bids);
    auto converted_asks = ConvertPriceLevels(asks);
    OrderBookMetrics metrics = CalculateOrderBookMetrics(converted_bids, converted_asks);
    
    intel.set_order_book_imbalance(metrics.imbalance);
    intel.set_depth_imbalance(metrics.depth_imbalance);
    intel.set_liquidity_ratio(CalculateAdaptiveLiquidityRatio(metrics.total_volume, profile));
    
    // Update profile with current observations
    UpdateProfile(profile, metrics.total_volume, relative_spread, metrics.total_volume);
    
    // Advanced volatility estimation
    double volatility = EstimateVolatility(symbol, relative_spread, orderbook);
    intel.set_realized_volatility_1m(volatility);
    
    // Flow analysis (if trade data available)
    FlowMetrics flow = AnalyzeFlowMetrics(metrics.imbalance, volatility);
    intel.set_net_flow_5s(flow.net_flow_5s);
    intel.set_net_flow_30s(flow.net_flow_30s);
    intel.set_aggressive_buy_ratio(flow.aggressive_buy_ratio);
    intel.set_aggressive_sell_ratio(flow.aggressive_sell_ratio);
    
    // Adaptive regime classification
    auto regime_result = ClassifyOrderBookRegimeAdaptive(
        metrics, relative_spread, profile);
        
    intel.set_current_regime(regime_result.regime);
    intel.set_regime_confidence(regime_result.confidence);
    
    return intel;
}


CoinProfile& AdaptiveMarketIntelligence::GetOrCreateProfile(const std::string& symbol) {
    auto it = profiles_.find(symbol);
    if (it != profiles_.end()) {
        return it->second;
    }
    
    // Create default profile for unknown symbol
    std::string base_symbol = symbol.substr(0, symbol.find("USDT"));
    profiles_[symbol] = {
        symbol, "unknown", 50000000, 0.001, 2000, 2.0,
        2000, 300, 0.0005, 0.005
    };
    
    return profiles_[symbol];
}
    
OrderBookMetrics AdaptiveMarketIntelligence::CalculateOrderBookMetrics(
    const std::vector<PriceLevel>& bids,
    const std::vector<PriceLevel>& asks) {
    
    OrderBookMetrics metrics;
    
    // Calculate volumes at different depths
    for (size_t i = 0; i < std::min(size_t(5), bids.size()); ++i) {
        metrics.bid_volume_5 += bids[i].quantity();
    }
    
    for (size_t i = 0; i < std::min(size_t(5), asks.size()); ++i) {
        metrics.ask_volume_5 += asks[i].quantity();
    }
    
    for (size_t i = 0; i < std::min(size_t(10), bids.size()); ++i) {
        metrics.bid_volume_10 += bids[i].quantity();
    }
    
    for (size_t i = 0; i < std::min(size_t(10), asks.size()); ++i) {
        metrics.ask_volume_10 += asks[i].quantity();
    }
    
    metrics.total_volume = metrics.bid_volume_10 + metrics.ask_volume_10;
    
    // Surface imbalance (top 5 levels)
    if (metrics.bid_volume_5 + metrics.ask_volume_5 > 0) {
        metrics.imbalance = (metrics.bid_volume_5 - metrics.ask_volume_5) / 
                            (metrics.bid_volume_5 + metrics.ask_volume_5);
    }
    
    // Depth imbalance (levels 6-10)
    double deep_bid = metrics.bid_volume_10 - metrics.bid_volume_5;
    double deep_ask = metrics.ask_volume_10 - metrics.ask_volume_5;
    if (deep_bid + deep_ask > 0) {
        metrics.depth_imbalance = (deep_bid - deep_ask) / (deep_bid + deep_ask);
    }
    
    return metrics;
}

double AdaptiveMarketIntelligence::CalculateAdaptiveLiquidityRatio(double volume, const CoinProfile& profile) {
    // Use adaptive threshold based on symbol's characteristics
    double threshold = profile.min_liquid_volume;
    
    // Adjust for current market conditions
    if (profile.sample_count > 100 && profile.volume_ema > 0) {
        threshold = profile.volume_ema * 0.8; // 80% of average volume
    }
    
    return std::min(1.0, volume / threshold);
}

void AdaptiveMarketIntelligence::UpdateProfile(CoinProfile& profile, double volume, double spread, double depth) {
    const double alpha = 0.1; // EMA smoothing factor
    
    if (profile.sample_count == 0) {
        profile.volume_ema = volume;
        profile.spread_ema = spread;
        profile.depth_ema = depth;
    } else {
        profile.volume_ema = alpha * volume + (1 - alpha) * profile.volume_ema;
        profile.spread_ema = alpha * spread + (1 - alpha) * profile.spread_ema;
        profile.depth_ema = alpha * depth + (1 - alpha) * profile.depth_ema;
    }
    
    profile.sample_count++;
    
    // Periodically update thresholds based on learned patterns
    if (profile.sample_count % 1000 == 0) {
        AdaptThresholds(profile);
    }
}

void AdaptiveMarketIntelligence::AdaptThresholds(CoinProfile& profile) {
    // Adapt thresholds based on observed patterns
    profile.min_liquid_volume = profile.volume_ema * 1.5;
    profile.min_thin_volume = profile.volume_ema * 0.3;
    profile.tight_spread_threshold = profile.spread_ema * 0.5;
    profile.wide_spread_threshold = profile.spread_ema * 2.0;
}

double AdaptiveMarketIntelligence::EstimateVolatility(const std::string& symbol, double spread, 
                        std::shared_ptr<OrderBook> orderbook) {
    // More sophisticated volatility estimation
    auto& profile = profiles_[symbol];
    (void)orderbook; // suppress unused parameter warning
    // Base volatility from spread
    double spread_vol = spread * profile.volatility_factor;
    
    // TODO: Add price movement-based volatility if historical data available
    // double price_vol = CalculatePriceVolatility(symbol);
    
    return spread_vol * 10; // Scale factor
}

FlowMetrics AdaptiveMarketIntelligence::AnalyzeFlowMetrics(double imbalance, double volatility) {
    FlowMetrics flow;
    
    // Estimate flow based on imbalance and volatility
    flow.net_flow_5s = imbalance * volatility;
    flow.net_flow_30s = imbalance * volatility * 0.7; // Decay over time
    
    // Estimate aggressive ratios
    flow.aggressive_buy_ratio = std::max(0.0, std::min(1.0, 0.5 + imbalance * 0.3));
    flow.aggressive_sell_ratio = std::max(0.0, std::min(1.0, 0.5 - imbalance * 0.3));
    
    return flow;
}

RegimeResult AdaptiveMarketIntelligence::ClassifyOrderBookRegimeAdaptive(
    const OrderBookMetrics& metrics,
    double spread,
    const CoinProfile& profile) {
    
    RegimeResult result;
    result.confidence = 0.8;
    
    // Use adaptive thresholds
    double liquid_threshold = profile.sample_count > 0 ? 
        profile.volume_ema * 1.5 : profile.min_liquid_volume;
    double thin_threshold = profile.sample_count > 0 ? 
        profile.volume_ema * 0.3 : profile.min_thin_volume;
    double tight_spread = profile.sample_count > 0 ? 
        profile.spread_ema * 0.5 : profile.tight_spread_threshold;
    double wide_spread = profile.sample_count > 0 ? 
        profile.spread_ema * 2.0 : profile.wide_spread_threshold;
    
    // Multi-factor classification
    int liquid_score = 0;
    int thin_score = 0;
    int volatile_score = 0;
    
    // Volume factors
    if (metrics.total_volume > liquid_threshold) liquid_score += 2;
    if (metrics.total_volume < thin_threshold) thin_score += 2;
    
    // Spread factors
    if (spread < tight_spread) liquid_score += 1;
    if (spread > wide_spread) thin_score += 2;
    
    // Imbalance factors
    if (std::abs(metrics.imbalance) > 0.3) volatile_score += 2;
    if (std::abs(metrics.depth_imbalance) > 0.4) volatile_score += 1;
    
    // Determine regime based on scores
    if (liquid_score >= 2 && thin_score == 0 && volatile_score < 2) {
        result.regime = trading::OB_LIQUID;
        result.confidence = 0.9;
    } else if (thin_score >= 2) {
        result.regime = trading::OB_THIN;
        result.confidence = 0.8;
    } else if (volatile_score >= 2) {
        result.regime = trading::OB_VOLATILE;
        result.confidence = 0.75;
    } else {
        result.regime = trading::OB_NORMAL;
        result.confidence = 0.6;
    }
    
    return result;
}

} // namespace intelligence  
} // namespace trading