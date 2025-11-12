#pragma once

#include "core/types.hpp"
#include "core/direction.hpp"
#include <chrono>
#include <string>
#include <map>
#include <mutex>
#include <deque>
#include <cmath>

namespace trading {
namespace strategy {

/**
 * ADAPTIVE Exit Strategy với các thông số động dựa trên market microstructure
 * 
 * Key improvements over hardcoded version:
 * 1. Dynamic parameters based on real-time volatility & spread
 * 2. Adaptive thresholds using statistical measures
 * 3. Market regime detection for parameter adjustment
 * 4. Microstructure-aware exit timing
 * 5. Kelly criterion-based position sizing hints
 */
class AdaptiveExitStrategy {
public:
    enum class MarketRegime {
        TRENDING,          // Clear directional move
        MEAN_REVERTING,    // Range-bound, oscillating
        HIGH_VOLATILITY,   // Volatile, unpredictable
        LOW_LIQUIDITY,     // Wide spreads, thin orderbook
        NORMAL            // Balanced conditions
    };

    enum class ExitReason {
        PROFIT_TARGET,
        TRAILING_STOP,
        STOP_LOSS,
        SIGNAL_REVERSAL,
        TIME_LIMIT,
        SPREAD_WIDENING,
        LIQUIDITY_CRISIS,
        VOLATILITY_SPIKE,
        ADVERSE_SELECTION  // Detected toxic flow
    };
    
    struct ExitDecision {
        bool should_exit = false;
        ExitReason reason = ExitReason::TIME_LIMIT;
        double expected_pnl = 0.0;
        double exit_price = 0.0;
        double confidence = 0.0;  // Exit confidence score
    };
    
    struct MarketMicrostructure {
        double volatility_1m = 0.0;      // 1-minute realized volatility
        double spread_ema = 0.0;          // EMA of bid-ask spread
        double spread_std = 0.0;          // Spread standard deviation
        double orderbook_imbalance = 0.0; // Bid/ask volume imbalance
        double trade_intensity = 0.0;     // Trades per second
        double price_impact = 0.0;        // Estimated price impact
        double tick_size = 0.0;           // Minimum price increment
        double volume_profile_skew = 0.0; // Volume distribution asymmetry
    };
    
    struct DynamicParameters {
        // Core parameters (dynamically calculated)
        double profit_target_ticks;      // In ticks, not bps
        double trailing_stop_ticks;      
        double stop_loss_ticks;
        double max_holding_time;         // Seconds
        double min_holding_time;         
        
        // Adaptive thresholds
        double signal_reversal_threshold;
        double spread_emergency_multiplier;
        double volatility_spike_threshold;
        
        // Calculated metrics
        double expected_edge;             // Expected profit per trade
        double sharpe_estimate;           // Risk-adjusted return estimate
        double kelly_fraction;            // Optimal position size hint
    };
    
    struct TradeState {
        double entry_price = 0.0;
        std::chrono::steady_clock::time_point entry_time;
        double peak_price = 0.0;
        double trough_price = 0.0;
        
        // Entry conditions snapshot
        MarketMicrostructure entry_microstructure;
        MarketRegime entry_regime;
        core::Direction entry_direction;
        double entry_confidence = 0.0;
        
        // Dynamic tracking
        double max_favorable_excursion = 0.0;  // MFE
        double max_adverse_excursion = 0.0;    // MAE
        std::deque<double> recent_pnls;        // Rolling PnL history
        double momentum_score = 0.0;           // Current momentum
        
        // Statistical measures
        double pnl_mean = 0.0;
        double pnl_std = 0.0;
        double time_weighted_pnl = 0.0;
        
        bool is_active = false;
        std::string symbol;
        // Stateful exit controls (high-level review)
        bool exit_in_progress = false;                              // An exit is currently being attempted
        std::chrono::steady_clock::time_point last_exit_attempt;    // Last time we tried an exit
        double min_hold_seconds = 0.0;                              // Per-trade min hold override
        double cooldown_seconds = 0.0;                              // Configured cooldown for this trade
        int backoff_attempts = 0;                                   // Exponential backoff counter
        std::chrono::steady_clock::time_point next_allowed_exit_time;// Next allowed exit attempt time
        int consecutive_low_confidence_triggers = 0;                // Consensus counter for low-conf triggers
        std::chrono::steady_clock::time_point last_trigger_time;    // When last trigger incremented
        bool orderable = true;                                      // Local/account orderability flag
        std::string last_exit_failure;                              // Last failure reason (informational)
    };

private:
    // Market data history for adaptive calculations
    struct MarketHistory {
        std::deque<double> volatility_samples;
        std::deque<double> spread_samples;
        std::deque<double> volume_samples;
        std::deque<double> price_returns;
        size_t max_samples = 60;  // 1 minute of data at 1Hz
    };
    
    std::map<std::string, MarketHistory> marketHistory_;
    std::map<std::string, TradeState> activeTrades_;
    std::map<std::string, std::deque<double>> historicalPnLs_;  // For Sharpe calculation
    
    mutable std::mutex tradesMutex_;
    mutable std::mutex historyMutex_;
    
    // Configuration
    double predictionWindow_ = 60.0;  // 1 minute in seconds
    double riskFreeRate_ = 0.0;       // For Sharpe calculation
    // High-level review configuration (practical defaults)
    double min_hold_factor_ = 0.5;               // min_hold >= min_hold_factor_ * predictionWindow_
    double cooldown_seconds_default_ = 10.0;     // default cooldown between exit attempts
    int consensus_required_count_ = 3;           // number of repeated low-conf triggers to accept exit
    double consensus_confidence_threshold_ = 0.75; // required confidence to bypass consensus
    int volatility_spike_sustain_samples_ = 3;   // how many successive samples to treat vol spike as sustained
    double consensus_window_seconds_ = 30.0;     // window to gather repeated triggers
    double min_notional_default_ = 10.0;         // min notional check for orderability (placeholder)
    
    // Statistical functions
    double calculatePercentile(const std::deque<double>& data, double percentile) const;
    double calculateStandardDeviation(const std::deque<double>& data) const;
    double calculateSkewness(const std::deque<double>& data) const;
    double calculateAutoCorrelation(const std::deque<double>& data, int lag) const;
    // utility pre-checks and bookkeeping for exit attempts
    bool preCheckOrderability(const std::string& symbol, double required_notional) const;
    void registerExitAttemptResult(const std::string& symbol, bool success, const std::string& reason);
    
    // Market regime detection
    MarketRegime detectMarketRegime(const MarketMicrostructure& micro, 
                                   const MarketHistory& history) const;
    
    // Dynamic parameter calculation
    DynamicParameters calculateDynamicParameters(
        const std::string& symbol,
        const MarketMicrostructure& current_micro,
        const MarketRegime& regime) const;
    
    // Adverse selection detection
    double calculateAdverseSelectionScore(
        const TradeState& state,
        const MarketMicrostructure& current_micro) const;

public:
    AdaptiveExitStrategy(double prediction_window_seconds = 60.0);
    
    /**
     * Update market microstructure data
     */
    void updateMarketData(
        const std::string& symbol,
        double volatility_1m,
        double spread_ema,
        double orderbook_imbalance,
        double trade_intensity,
        double price_return);
    
    /**
     * Start tracking a new position with adaptive parameters
     */
    void startPosition(
        const std::string& symbol,
        double entry_price,
        core::Direction direction,
        double confidence,
        const MarketMicrostructure& microstructure);
    
    /**
     * Evaluate exit with dynamic thresholds
     */
    ExitDecision evaluateExit(
        const std::string& symbol,
        double current_price,
        const MarketMicrostructure& current_micro,
        core::Direction current_signal_direction,
        double current_confidence);
    
    /**
     * Get current dynamic parameters for symbol
     */
    DynamicParameters getCurrentParameters(const std::string& symbol) const;
    
    /**
     * Calculate optimal position size using Kelly Criterion
     */
    double calculateOptimalPositionSize(
        const std::string& symbol,
        double available_capital,
        double max_position_fraction = 0.25) const;
    
    /**
     * Should enter position based on market conditions
     */
    bool shouldEnterPosition(
        const std::string& symbol,
        const MarketMicrostructure& micro,
        double confidence) const;
    
    /**
     * Close position and update statistics
     */
    void closePosition(const std::string& symbol, 
                      ExitReason reason, 
                      double final_pnl);
    
    /**
     * Get trade statistics for performance analysis
     */
    struct TradeStatistics {
        int total_trades = 0;
        int winning_trades = 0;
        double avg_win = 0.0;
        double avg_loss = 0.0;
        double sharpe_ratio = 0.0;
        double profit_factor = 0.0;
        double max_drawdown = 0.0;
        std::map<ExitReason, int> exit_reason_counts;
    };
    
    TradeStatistics getStatistics(const std::string& symbol) const;
    
    /**
     * Helpers
     */
    bool hasActivePosition(const std::string& symbol) const;
    TradeState getTradeState(const std::string& symbol) const;
    static const char* exitReasonToString(ExitReason reason);
    // getActiveSymbols
    std::vector<std::string> getActiveSymbols() const {
        std::lock_guard<std::mutex> lock(tradesMutex_);
        std::vector<std::string> symbols;
        for (const auto& pair : activeTrades_) {
            if (pair.second.is_active) {
                symbols.push_back(pair.first);
            }
        }
        return symbols;
    }
};

} // namespace strategy
} // namespace trading
