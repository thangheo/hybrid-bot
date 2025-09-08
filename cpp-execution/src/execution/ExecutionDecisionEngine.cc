// #pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cmath>

// Assuming protobuf messages are defined as in the document
#include "../../generated/cpp/trading_messages.pb.h"
class OrderBookAnalyzer {
private:
    struct OrderBookSnapshot {
        std::vector<PriceLevel> bids;
        std::vector<PriceLevel> asks;
        double timestamp;
        double midPrice;
        double spread;
        double imbalance;
    };
    
    std::unordered_map<std::string, std::vector<OrderBookSnapshot>> history_;
    static const size_t MAX_HISTORY_SIZE = 100;

public:
    // Analyze current order book regime
    OrderBookRegime DetectRegime(const std::string& symbol, 
                                const std::vector<PriceLevel>& bids,
                                const std::vector<PriceLevel>& asks) {
        
        double spread = asks[0].price() - bids[0].price();
        double midPrice = (asks[0].price() + bids[0].price()) / 2.0;
        double relative_spread = spread / midPrice;
        
        // Calculate order book imbalance
        double bidVolume = 0, askVolume = 0;
        for (int i = 0; i < std::min(5, (int)bids.size()); ++i) {
            bidVolume += bids[i].quantity();
        }
        for (int i = 0; i < std::min(5, (int)asks.size()); ++i) {
            askVolume += asks[i].quantity();
        }
        
        double imbalance = (bidVolume - askVolume) / (bidVolume + askVolume);
        
        // Calculate depth and liquidity metrics
        double total_depth = bidVolume + askVolume;
        double volatility = CalculateRecentVolatility(symbol);
        
        // Regime classification logic
        if (relative_spread > 0.005 || total_depth < 1000) {
            return OrderBookRegime::THIN;
        } else if (volatility > 0.02 || std::abs(imbalance) > 0.3) {
            return OrderBookRegime::VOLATILE;
        } else if (total_depth > 10000 && relative_spread < 0.001) {
            return OrderBookRegime::LIQUID;
        } else {
            return OrderBookRegime::NORMAL;
        }
    }
    
    double CalculateLiquidityScore(const std::vector<PriceLevel>& bids,
                                  const std::vector<PriceLevel>& asks) {
        double total_volume = 0;
        double weighted_spread = 0;
        
        // Calculate volume-weighted spread in top 5 levels
        for (int i = 0; i < std::min(5, (int)std::min(bids.size(), asks.size())); ++i) {
            double level_volume = bids[i].quantity() + asks[i].quantity();
            double level_spread = asks[i].price() - bids[i].price();
            
            total_volume += level_volume;
            weighted_spread += level_spread * level_volume;
        }
        
        if (total_volume == 0) return 0.0;
        
        weighted_spread /= total_volume;
        
        // Liquidity score: higher volume and tighter spread = higher score
        return std::min(1.0, total_volume / (1000 * (1 + weighted_spread * 100)));
    }

private:
    double CalculateRecentVolatility(const std::string& symbol) {
        // Simplified volatility calculation based on price history
        auto it = history_.find(symbol);
        if (it == history_.end() || it->second.size() < 10) {
            return 0.01; // Default volatility
        }
        
        std::vector<double> returns;
        const auto& snapshots = it->second;
        
        for (size_t i = 1; i < std::min(30ul, snapshots.size()); ++i) {
            double ret = std::log(snapshots[i].midPrice / snapshots[i-1].midPrice);
            returns.push_back(ret);
        }
        
        // Calculate standard deviation
        double mean = 0;
        for (double ret : returns) mean += ret;
        mean /= returns.size();
        
        double variance = 0;
        for (double ret : returns) {
            variance += (ret - mean) * (ret - mean);
        }
        variance /= (returns.size() - 1);
        
        return std::sqrt(variance) * std::sqrt(252); // Annualized
    }
};

class ExecutionDecisionEngine {
private:
    std::unique_ptr<OrderBookAnalyzer> ob_analyzer_;
    
    struct ExecutionParameters {
        double chunk_size_ratio;      // Fraction of total quantity per chunk
        double aggression_level;      // 0.0 (passive) to 1.0 (aggressive)
        int max_chunk_delay_ms;      // Max time between chunks
        double price_improvement_threshold; // Minimum improvement to wait for
        bool use_hidden_orders;
        double max_participation_rate; // Max % of volume to consume
    };

public:
    ExecutionDecisionEngine() : ob_analyzer_(std::make_unique<OrderBookAnalyzer>()) {}
    
    // Main decision function
    ExecutionParameters DecideExecutionStrategy(const TradingIntent& intent,
                                              const std::vector<PriceLevel>& bids,
                                              const std::vector<PriceLevel>& asks,
                                              const MarketIntelligence& market_intel) {
        
        ExecutionParameters params = GetBaseParameters(intent.execution_style());
        
        // Adjust based on market regime
        AdjustForMarketRegime(params, intent.market_regime(), intent.signal_strength());
        
        // Adjust based on volatility regime
        AdjustForVolatilityRegime(params, intent.volatility_regime());
        
        // Adjust based on ETF flow regime
        AdjustForETFFlowRegime(params, intent.etf_flow_regime());
        
        // Adjust based on confidence and urgency
        AdjustForConfidenceAndUrgency(params, intent.confidence(), intent.urgency_factor());
        
        // Adjust based on order book regime
        OrderBookRegime ob_regime = ob_analyzer_->DetectRegime(
            intent.symbol(), bids, asks);
        AdjustForOrderBookRegime(params, ob_regime);
        
        // Adjust based on real-time market conditions
        AdjustForMarketConditions(params, market_intel);
        
        // Apply risk constraints
        ApplyRiskConstraints(params, intent);
        
        return params;
    }
    
    // Calculate optimal chunk size for this execution
    double CalculateOptimalChunkSize(const TradingIntent& intent,
                                   const ExecutionParameters& params,
                                   const MarketIntelligence& market_intel) {
        
        double base_chunk = intent.target_quantity() * params.chunk_size_ratio;
        
        // Adjust for market impact concerns
        if (intent.max_market_impact() < 0.01) {
            base_chunk *= 0.5; // Smaller chunks for low impact requirement
        }
        
        // Adjust for liquidity
        double liquidity_factor = std::min(1.0, market_intel.liquidity_ratio() / 0.1);
        base_chunk *= liquidity_factor;
        
        // Don't exceed participation rate
        double recent_volume = EstimateRecentVolume(market_intel);
        double max_chunk = recent_volume * params.max_participation_rate;
        
        return std::min(base_chunk, max_chunk);
    }
    
    // Determine order type and pricing strategy
    struct OrderDecision {
        enum OrderType { MARKET, LIMIT, STOP_LIMIT, ICEBERG, HIDDEN };
        OrderType order_type;
        double limit_price;
        double stop_price;
        bool post_only;
        int time_in_force_ms;
    };
    
    OrderDecision DecideOrderType(const TradingIntent& intent,
                                 const ExecutionParameters& params,
                                 const std::vector<PriceLevel>& bids,
                                 const std::vector<PriceLevel>& asks) {
        
        OrderDecision decision;
        
        double bestBid = bids[0].price();
        double bestAsk = asks[0].price();
        double midPrice = (bestBid + bestAsk) / 2.0;
        
        if (params.aggression_level > 0.8) {
            // Aggressive: use market orders
            decision.order_type = OrderDecision::MARKET;
            decision.post_only = false;
            decision.time_in_force_ms = 1000; // 1 second
            
        } else if (params.aggression_level > 0.6) {
            // Moderately aggressive: limit at best price
            decision.order_type = OrderDecision::LIMIT;
            decision.limit_price = (intent.side() == TradeSide::BUY) ? bestAsk : bestBid;
            decision.post_only = false;
            decision.time_in_force_ms = 5000;
            
        } else if (params.aggression_level > 0.3) {
            // Balanced: limit with slight improvement
            decision.order_type = OrderDecision::LIMIT;
            double improvement = (bestAsk - bestBid) * 0.3;
            if (intent.side() == TradeSide::BUY) {
                decision.limit_price = bestBid + improvement;
            } else {
                decision.limit_price = bestAsk - improvement;
            }
            decision.post_only = true;
            decision.time_in_force_ms = 15000;
            
        } else {
            // Passive: limit at mid or better
            decision.order_type = params.use_hidden_orders ? 
                OrderDecision::HIDDEN : OrderDecision::LIMIT;
            
            decision.limit_price = midPrice;
            decision.post_only = true;
            decision.time_in_force_ms = 30000;
        }
        
        // Apply price constraints from intent
        if (intent.side() == TradeSide::BUY && intent.max_price() > 0) {
            decision.limit_price = std::min(decision.limit_price, intent.max_price());
        } else if (intent.side() == TradeSide::SELL && intent.min_price() > 0) {
            decision.limit_price = std::max(decision.limit_price, intent.min_price());
        }
        
        return decision;
    }

private:
    ExecutionParameters GetBaseParameters(ExecutionStyle style) {
        ExecutionParameters params;
        
        switch (style) {
            case ExecutionStyle::AGGRESSIVE:
                params.chunk_size_ratio = 0.3;
                params.aggression_level = 0.9;
                params.max_chunk_delay_ms = 500;
                params.price_improvement_threshold = 0.0;
                params.use_hidden_orders = false;
                params.max_participation_rate = 0.3;
                break;
                
            case ExecutionStyle::BALANCED:
                params.chunk_size_ratio = 0.2;
                params.aggression_level = 0.5;
                params.max_chunk_delay_ms = 2000;
                params.price_improvement_threshold = 0.001;
                params.use_hidden_orders = false;
                params.max_participation_rate = 0.2;
                break;
                
            case ExecutionStyle::PASSIVE:
                params.chunk_size_ratio = 0.1;
                params.aggression_level = 0.2;
                params.max_chunk_delay_ms = 5000;
                params.price_improvement_threshold = 0.002;
                params.use_hidden_orders = true;
                params.max_participation_rate = 0.1;
                break;
                
            case ExecutionStyle::STEALTH:
                params.chunk_size_ratio = 0.05;
                params.aggression_level = 0.1;
                params.max_chunk_delay_ms = 10000;
                params.price_improvement_threshold = 0.003;
                params.use_hidden_orders = true;
                params.max_participation_rate = 0.05;
                break;
        }
        
        return params;
    }
    
    void AdjustForMarketRegime(ExecutionParameters& params, 
                              MarketRegime regime, 
                              double signal_strength) {
        switch (regime) {
            case MarketRegime::EUPHORIA:
                // Market is euphoric - be more aggressive to capture momentum
                params.aggression_level *= 1.3;
                params.chunk_size_ratio *= 1.2;
                params.max_chunk_delay_ms *= 0.7;
                break;
                
            case MarketRegime::PANIC:
                // Market is panicking - be very aggressive but smaller sizes
                params.aggression_level *= 1.5;
                params.chunk_size_ratio *= 0.6;
                params.max_chunk_delay_ms *= 0.5;
                break;
                
            case MarketRegime::ACCUMULATION:
                // Accumulation phase - be patient for better prices
                params.aggression_level *= 0.7;
                params.price_improvement_threshold *= 1.5;
                params.max_chunk_delay_ms *= 1.5;
                break;
                
            case MarketRegime::DISTRIBUTION:
                // Distribution phase - moderate aggression
                params.aggression_level *= 0.9;
                params.chunk_size_ratio *= 0.9;
                break;
                
            case MarketRegime::SIDEWAYS:
                // Sideways market - very patient execution
                params.aggression_level *= 0.6;
                params.price_improvement_threshold *= 2.0;
                params.max_chunk_delay_ms *= 2.0;
                break;
        }
        
        // Amplify adjustments based on signal strength
        if (signal_strength > 0.8) {
            params.aggression_level *= 1.2;
            params.chunk_size_ratio *= 1.1;
        } else if (signal_strength < 0.3) {
            params.aggression_level *= 0.8;
            params.chunk_size_ratio *= 0.9;
        }
    }
    
    void AdjustForVolatilityRegime(ExecutionParameters& params, 
                                  VolatilityRegime vol_regime) {
        switch (vol_regime) {
            case VolatilityRegime::HIGH_VOL:
                // High volatility - smaller chunks, faster execution
                params.chunk_size_ratio *= 0.7;
                params.max_chunk_delay_ms *= 0.6;
                params.price_improvement_threshold *= 0.5;
                break;
                
            case VolatilityRegime::LOW_VOL:
                // Low volatility - can use larger chunks, be more patient
                params.chunk_size_ratio *= 1.3;
                params.max_chunk_delay_ms *= 1.5;
                params.price_improvement_threshold *= 1.5;
                break;
                
            case VolatilityRegime::MEDIUM_VOL:
                // Medium volatility - no major adjustments
                break;
        }
    }
    
    void AdjustForETFFlowRegime(ExecutionParameters& params, 
                               ETFFlowRegime flow_regime) {
        switch (flow_regime) {
            case ETFFlowRegime::POSITIVE_FLOW:
                // Positive ETF flows - align timing with institutional flow
                params.aggression_level *= 1.1;
                params.max_participation_rate *= 1.2;
                break;
                
            case ETFFlowRegime::NEGATIVE_FLOW:
                // Negative ETF flows - be more contrarian
                params.aggression_level *= 0.9;
                params.price_improvement_threshold *= 1.3;
                break;
                
            case ETFFlowRegime::NEUTRAL_FLOW:
                // Neutral flows - no major adjustments
                break;
        }
    }
    
    void AdjustForConfidenceAndUrgency(ExecutionParameters& params,
                                      double confidence,
                                      double urgency) {
        // Higher confidence = more aggressive
        params.aggression_level *= (0.5 + confidence);
        params.chunk_size_ratio *= (0.7 + confidence * 0.6);
        
        // Higher urgency = faster execution
        params.max_chunk_delay_ms *= (1.5 - urgency);
        params.aggression_level *= (0.8 + urgency * 0.4);
    }
    
    void AdjustForOrderBookRegime(ExecutionParameters& params,
                                 OrderBookRegime ob_regime) {
        switch (ob_regime) {
            case OrderBookRegime::LIQUID:
                // Liquid market - can be more aggressive
                params.chunk_size_ratio *= 1.2;
                params.max_participation_rate *= 1.3;
                break;
                
            case OrderBookRegime::THIN:
                // Thin market - be very careful
                params.chunk_size_ratio *= 0.5;
                params.aggression_level *= 0.6;
                params.max_participation_rate *= 0.3;
                params.use_hidden_orders = true;
                break;
                
            case OrderBookRegime::VOLATILE:
                // Volatile order book - smaller chunks, faster decisions
                params.chunk_size_ratio *= 0.7;
                params.max_chunk_delay_ms *= 0.7;
                break;
                
            case OrderBookRegime::NORMAL:
                // Normal conditions - no major adjustments
                break;
        }
    }
    
    void AdjustForMarketConditions(ExecutionParameters& params,
                                  const MarketIntelligence& intel) {
        // Adjust for aggressive flow imbalance
        double flow_imbalance = intel.aggressive_buy_ratio() - intel.aggressive_sell_ratio();
        if (std::abs(flow_imbalance) > 0.3) {
            params.aggression_level *= (1.0 + std::abs(flow_imbalance) * 0.5);
        }
        
        // Adjust for liquidity
        if (intel.liquidity_ratio() < 0.5) {
            params.chunk_size_ratio *= intel.liquidity_ratio() / 0.5;
        }
    }
    
    void ApplyRiskConstraints(ExecutionParameters& params,
                             const TradingIntent& intent) {
        // Ensure we don't exceed risk limits
        if (intent.max_market_impact() > 0) {
            double max_chunk_for_impact = intent.target_quantity() * intent.max_market_impact();
            params.chunk_size_ratio = std::min(params.chunk_size_ratio,
                                             max_chunk_for_impact / intent.target_quantity());
        }
        
        // Respect slippage tolerance
        if (intent.max_slippage_tolerance() < 0.005) {
            params.aggression_level *= 0.7; // Be more passive for low slippage tolerance
        }
        
        // Respect execution time constraints
        if (intent.max_execution_time_ms() < 30000) {
            params.aggression_level *= 1.3; // Be more aggressive for tight time constraints
            params.max_chunk_delay_ms = std::min(params.max_chunk_delay_ms,
                                               intent.max_execution_time_ms() / 10);
        }
    }
    
    double EstimateRecentVolume(const MarketIntelligence& intel) {
        // Estimate recent volume based on flow metrics
        double volume_5s = std::abs(intel.net_flow_5s());
        double volume_30s = std::abs(intel.net_flow_30s());
        
        // Extrapolate to per-second volume
        return std::max(volume_5s / 5.0, volume_30s / 30.0);
    }
};