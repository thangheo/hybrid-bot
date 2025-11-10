#pragma once

#include "core/types.hpp"
#include "core/direction.hpp"
#include <chrono>
#include <string>
#include <map>
#include <mutex>
#include <filesystem>
#include <functional>

namespace trading {
namespace strategy {

/**
 * IMPROVED Optimal Exit Strategy for HFT with adaptive exits
 * 
 * Key improvements:
 * - Fixed peak tracking for SELL positions 
 * - Dynamic parameter adjustment based on time and momentum
 * - Faster signal reversal detection
 * - Momentum-based exit acceleration
 * - Time-based stop tightening
 * - Strict time limit enforcement
 */
class OptimalExitStrategy {
public:
    enum class ExitReason {
        PROFIT_TARGET,
        TRAILING_STOP,
        STOP_LOSS,
        SIGNAL_REVERSAL,
        TIME_LIMIT,
        SPREAD_WIDENING,
        LIQUIDITY_DRIED
    };
    
    struct ExitDecision {
        bool should_exit = false;
        ExitReason reason = ExitReason::TIME_LIMIT;
        double expected_pnl = 0.0;
        double exit_price = 0.0;
    };
    
    struct TradeState {
        double entry_price = 0.0;
        std::chrono::steady_clock::time_point entry_time;
        double peak_price = 0.0;
        double entry_spread_ratio = 0.0;
    double entry_spread_ema = 0.0; // EMA / smoothed spread (same units as entry_spread_ratio)
        core::Direction entry_direction = core::Direction::NEUTRAL;
        double entry_confidence = 0.0;
        std::string symbol;
        bool is_active = false;
        
        // NEW: Enhanced tracking fields
        double initial_momentum = 0.0;     // Average PnL/tick in first 3 ticks
        int ticks_since_entry = 0;         // Number of evaluation ticks
        double cumulative_pnl_sum = 0.0;   // Sum of PnL values for momentum calc
        int spread_widening_counter = 0;   // consecutive checks where spread widening condition held
    };
    
private:
    // IMPROVED parameters with tighter controls
    struct ExitParams {
        double profit_target_bps = 6.0;        // Reduced: Take profits earlier
        double trailing_stop_bps = 3.5;        // Tighter trailing stop
        double hard_stop_loss_bps = -3.0;      // Tighter max loss
        double max_holding_seconds = 8.0;      // Reduced: Exit faster
        double signal_confidence_flip = 0.25;  // More sensitive to reversals
        double spread_widening_factor = 2.0;   // More sensitive to spread changes
        double max_spread_bps = 8.0;           // Tighter spread requirement
        double min_stop_distance_bps = 10.0;   // Minimum stop distance
        double stop_spread_multiplier = 2.5;   // Stop distance multiplier
        // Emergency parameters
        double emergency_spread_multiplier = 3.0; // instant spread multiple to treat as emergency
        double min_hold_before_spread_exit_seconds = 0.05; // ignore spread-based exits within first X seconds
        int spread_widening_consecutive_checks = 2; // require condition for N consecutive evaluates
    };
    
    // Per-symbol exit parameters (based on volatility config)
    std::map<std::string, ExitParams> symbolParams_;
    mutable std::mutex configMutex_;  // Thread-safe config operations
    
    // Runtime reload tracking
    std::string configPath_;
    std::filesystem::file_time_type lastLoadedTime_;
    
    // Active trades tracking
    std::map<std::string, TradeState> activeTrades_;
    mutable std::mutex tradesMutex_;

    // Track recent close timestamps for per-symbol cooldowns
    std::map<std::string, std::chrono::steady_clock::time_point> recentCloseTs_;
    mutable std::mutex recentCloseMutex_;
    std::chrono::milliseconds entryCooldown_{std::chrono::milliseconds(2000)}; // 2s cooldown
    
    // Load parameters from volatility config
    void loadSymbolParameters(const std::string& config_path);
    ExitParams getParamsForSymbol(const std::string& symbol) const;
    
public:
    OptimalExitStrategy();
    explicit OptimalExitStrategy(const std::string& volatility_config_path);
    
    /**
     * Start tracking a new position
     */
    void startPosition(const std::string& symbol, 
                      double entry_price,
                      core::Direction direction,
                      double confidence,
                      double spread_ratio,
                      double spread_ema = 0.0);
    
    /**
     * Evaluate exit decision for active position
     * IMPROVED: Now with momentum tracking and dynamic adjustments
     */
    [[nodiscard]] ExitDecision evaluateExit(const std::string& symbol,
                             double current_price,
                             double current_spread_ratio,
                             double current_spread_ema,
                             core::Direction current_signal_direction,
                             double current_confidence,
                             double bid_price = 0.0,
                             double ask_price = 0.0);
    
    /**
     * Close position (called after exit)
     */
    void closePosition(const std::string& symbol, ExitReason reason, double pnl);
    
    /**
     * Check if symbol has active position
     */
    bool hasActivePosition(const std::string& symbol) const;
    
    /**
     * Get active trade state for symbol
     */
    TradeState getTradeState(const std::string& symbol) const;
    
    /**
     * Get exit reason as string for logging
     */
    static const char* exitReasonToString(ExitReason reason);
    
    /**
     * Update parameters for symbol category
     */
    void updateParameters(const std::string& category, const ExitParams& params);
    
    /**
     * Reload configuration if file has been modified
     */
    void reloadIfStale();
    
    /**
     * Register callback for exit execution
     */
    using ExitCallback = std::function<void(const std::string& symbol, core::Direction exitDirection, double currentPrice, const std::string& reason)>;
    void registerExitCallback(const ExitCallback& callback);

public:
    // Check if symbol was recently closed (within cooldown)
    bool wasRecentlyClosed(const std::string& symbol) const;
    // Configure cooldown duration
    void setEntryCooldown(std::chrono::milliseconds d) { entryCooldown_ = d; }

private:
    ExitCallback exitCallback_;
};

} // namespace strategy
} // namespace trading