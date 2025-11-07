#pragma once

#include "core/types.hpp"
#include "core/direction.hpp"
#include <chrono>
#include <string>
#include <map>
#include <mutex>

namespace trading {
namespace strategy {

/**
 * Optimal Exit Strategy for HFT with 2s training windows
 * Replaces simple "NEUTRAL = SELL" logic with multi-layer exit system
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
        core::Direction entry_direction = core::Direction::NEUTRAL;
        double entry_confidence = 0.0;
        std::string symbol;
        bool is_active = false;
    };
    
private:
    // Configurable parameters based on volatility category
    struct ExitParams {
        double profit_target_bps = 8.0;        // 8bps profit target
        double trailing_stop_bps = 5.0;        // 5bps trailing stop
        double hard_stop_loss_bps = -4.0;      // -4bps max loss
        double max_holding_seconds = 10.0;     // 10s timeout
        double signal_confidence_flip = 0.3;   // signal reversal threshold
        double spread_widening_factor = 2.5;   // exit if spread > 2.5x entry
    };
    
    // Per-symbol exit parameters (based on volatility config)
    std::map<std::string, ExitParams> symbolParams_;
    
    // Active trades tracking
    std::map<std::string, TradeState> activeTrades_;
    mutable std::mutex tradesMutex_;
    
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
                      double spread_ratio);
    
    /**
     * Evaluate exit decision for active position
     */
    ExitDecision evaluateExit(const std::string& symbol,
                             double current_price,
                             double current_spread_ratio,
                             core::Direction current_signal_direction,
                             double current_confidence);
    
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
};

} // namespace strategy
} // namespace trading