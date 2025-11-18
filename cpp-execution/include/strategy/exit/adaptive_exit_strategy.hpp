#pragma once

#include "core/types.hpp"
#include "core/direction.hpp"
#include "core/trading_signal.hpp"
#include <strategy/core_analysis/orderbook_regime_detector.hpp>
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
        int volatility_window_seconds = 60; // For dynamic calculations
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
        MarketRegimeType entry_regime;
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
        int vol_spike_count = 0;    // Number of spikes to confirm
        double quantity = 0.0;
        bool is_active = false;
        bool pending_exit = false;
        std::string symbol;
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
    // historicalPnLs_ stores realized trade PnL in basis points (bps). Do NOT
    // populate this with tick-level or expected PnL values; use
    // recordRealizedPnlBps() when a trade is actually closed and realized.

    // Store latest strategy/execution signal per symbol for exit logic
    std::unordered_map<std::string, core::TradingSignal> latestSignalBySymbol_;
    mutable std::mutex latestSignalMutex_;
    
    mutable std::mutex tradesMutex_;
    mutable std::mutex historyMutex_;
    
    // Cache for current market regime per symbol
    std::unordered_map<std::string, MarketRegimeType> regimeCache_;
    mutable std::mutex regimeMutex_;
    
    // Configuration
    double predictionWindow_ = 60000;  // Default prediction window in milliseconds (60s)
    double riskFreeRate_ = 0.0;       // For Sharpe calculation
    
    // Statistical functions
    double calculatePercentile(const std::deque<double>& data, double percentile) const;
    double calculateStandardDeviation(const std::deque<double>& data) const;
    double calculateSkewness(const std::deque<double>& data) const;
    double calculateAutoCorrelation(const std::deque<double>& data, int lag) const;
    
    // Market regime detection
    MarketRegimeType detectMarketRegime(const MarketMicrostructure& micro, 
                                   const MarketHistory& history) const;
    
    // Dynamic parameter calculation
    DynamicParameters calculateDynamicParameters(
        const std::string& symbol,
        const MarketMicrostructure& current_micro,
        const MarketRegimeType& regime) const;
    
    // Adverse selection detection
    double calculateAdverseSelectionScore(
        const TradeState& state,
        const MarketMicrostructure& current_micro) const;

    // Internal helper that assumes callers already hold tradesMutex_
    double calculateTotalPnLUnlocked(const std::string& symbol, double final_pnl) const;

public:
    AdaptiveExitStrategy(double prediction_window_seconds = 180);
    
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
     * Update current market regime for a symbol
     */
    void updateRegime(const std::string& symbol, const MarketMicrostructure& microstructure);
    
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
        double current_confidence,
        const std::vector<core::PriceTarget>& signal_targets = std::vector<core::PriceTarget>(),
        const core::Duration& signal_ttl = core::Duration::zero(),
        const core::Timestamp& signal_generation_time = core::Timestamp());

    /**
     * Update latest signal seen for a symbol. The exit engine will consult
     * this stored signal when evaluateExit is called without explicit signal params.
     */
    void updateLatestSignal(const std::string& symbol, const core::TradingSignal& signal);

    /**
     * Clear stored signal for a symbol (e.g., after full execution or cancellation).
     */
    void clearLatestSignal(const std::string& symbol);
    
    /**
     * Get current dynamic parameters for symbol
     */
    DynamicParameters getCurrentParameters(const std::string& symbol) const;
     /**
     * Get current market regime for symbol
     */
    MarketRegimeType getCurrentMarketRegime(const std::string& symbol) const;
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
     * Record a realized trade-level PnL expressed in basis points (bps).
     * This should be called only when a trade is actually closed and the
     * realized PnL is known (e.g., from execution/fill confirmation).
     */
    void recordRealizedPnlBps(const std::string& symbol, double realized_pnl_bps);

    /**
     * Compute and record a simulated realized PnL (in bps) using the stored
     * TradeState entry_price and the provided exit_price. This is intended for
     * simulation/monitoring where no execution/fill is present but the
     * strategy wants to record the PnL of a signal pair (entry->exit).
     */
    // void recordSimulatedRealizedPnl(const std::string& symbol, double exit_price, ExitReason reason);

    /**
     * Mark a position as having an exit in-flight. This avoids immediate closing
     * of trade state while we wait for execution/fill confirmation.
     */
    void markPendingExit(const std::string& symbol);
    
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
     * Calculate total PnL for a closed position. This aggregates recent tracked
     * PnL samples and the final realized PnL to produce a single summary value.
     */
    double calculateTotalPnL(const std::string& symbol, double final_pnl) const;
    
    /**
     * Helpers
     */
    /**
     * Compute signal PnL in basis points (bps) from entry->exit prices and direction.
     * This is a pure price-based calculation (exit_price vs entry_price) and is
     * intended to measure the PnL of the signal itself (independent of any
     * execution/risk layer). Returns 0.0 for invalid entry_price.
     */
    static double computeSignalPnlBps(double entry_price, double exit_price, core::Direction direction);
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
