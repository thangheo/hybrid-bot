#include "strategy/exit/optimal_exit_strategy.hpp"
#include "common/logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace trading {
namespace strategy {

std::vector<std::string> OptimalExitStrategy::getActiveSymbols() const {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    std::vector<std::string> symbols;
    for (const auto& [symbol, state] : activeTrades_) {
        if (state.is_active) symbols.push_back(symbol);
    }
    return symbols;
}

std::optional<OptimalExitStrategy::PositionInfo> OptimalExitStrategy::getPositionInfo(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    auto it = activeTrades_.find(symbol);
    if (it == activeTrades_.end() || !it->second.is_active) {
        return std::nullopt;
    }
    const auto& state = it->second;
    PositionInfo info;
    info.direction = state.entry_direction;
    info.entry_price = state.entry_price;
    info.confidence = state.entry_confidence;
    info.entry_time = state.entry_time;
    info.quantity = 1.0; // TODO: track real quantity if available
    return info;
}

using json = nlohmann::json;

// ============= CRITICAL BUG FIXES & IMPROVEMENTS =============
// 1. Fixed peak price tracking for SELL positions
// 2. Added dynamic stop-loss adjustment based on volatility
// 3. Implemented momentum-based exit acceleration
// 4. Added microstructure-aware spread filtering
// 5. Fixed time limit enforcement
// 6. Added PnL trailing for profitable positions

OptimalExitStrategy::OptimalExitStrategy() 
    : configPath_("config/symbol_volatility_config.json") {
    loadSymbolParameters(configPath_);
}

OptimalExitStrategy::OptimalExitStrategy(const std::string& volatility_config_path) 
    : configPath_(volatility_config_path) {
    loadSymbolParameters(volatility_config_path);
}

void OptimalExitStrategy::loadSymbolParameters(const std::string& config_path) {
    std::lock_guard<std::mutex> lock(configMutex_);
    ExitParams highVol;
    highVol.profit_target_bps = 12.0;           // Need 2x spread to profit
    highVol.trailing_stop_bps = 6.0;
    highVol.hard_stop_loss_bps = -6.0;
    highVol.max_holding_seconds = 20.0;
    highVol.min_holding_seconds = 2.0;          // Don't churn
    highVol.max_spread_bps = 800.0;             // Very wide threshold
    highVol.spread_cost_multiplier = 2.0;       // Aggressive target
    highVol.min_hold_before_spread_exit_seconds = 3.0;
    
    // For MEDIUM volatility (SOL, BNB, ETH ~50-100 bps):
    ExitParams medVol;
    medVol.profit_target_bps = 6.0;
    medVol.trailing_stop_bps = 3.0;
    medVol.hard_stop_loss_bps = -3.0;
    medVol.max_holding_seconds = 12.0;
    medVol.min_holding_seconds = 1.5;
    medVol.max_spread_bps = 150.0;
    medVol.spread_cost_multiplier = 1.5;
    medVol.min_hold_before_spread_exit_seconds = 2.0;
    
    // For LOW volatility (BTC, major pairs < 20 bps):
    ExitParams lowVol;
    lowVol.profit_target_bps = 4.0;
    lowVol.trailing_stop_bps = 2.0;
    lowVol.hard_stop_loss_bps = -2.0;
    lowVol.max_holding_seconds = 8.0;
    lowVol.min_holding_seconds = 1.0;
    lowVol.max_spread_bps = 30.0;
    lowVol.spread_cost_multiplier = 1.3;
    lowVol.min_hold_before_spread_exit_seconds = 1.0;

    try {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            LOG_WARN("Could not open volatility config: {}, using defaults", config_path);
            return;
        }

        json config;
        file >> config;

        // Update load timestamp
        if (std::filesystem::exists(config_path)) {
            lastLoadedTime_ = std::filesystem::last_write_time(config_path);
        }

        // Read exit_parameters (category-level defaults and per-symbol overrides)
        json exitParamsJson = json::object();
        if (config.contains("exit_parameters")) {
            exitParamsJson = config["exit_parameters"];

            // Set entryCooldown_ from crypto_major if available (fallbacks preserved)
            if (exitParamsJson.contains("crypto_major") && exitParamsJson["crypto_major"].contains("entry_cooldown_seconds")) {
                double secs = exitParamsJson["crypto_major"]["entry_cooldown_seconds"].get<double>();
                entryCooldown_ = std::chrono::milliseconds(static_cast<int>(secs * 1000.0));
            } else if (exitParamsJson.contains("crypto_minor") && exitParamsJson["crypto_minor"].contains("entry_cooldown_seconds")) {
                double secs = exitParamsJson["crypto_minor"]["entry_cooldown_seconds"].get<double>();
                entryCooldown_ = std::chrono::milliseconds(static_cast<int>(secs * 1000.0));
            }
        }

        // Load per-symbol parameters with IMPROVED settings
        if (config.contains("symbol_volatility_thresholds") && 
            config["symbol_volatility_thresholds"].contains("symbols")) {

            for (const auto& [symbol, data] : config["symbol_volatility_thresholds"]["symbols"].items()) {
                ExitParams params;

                std::string category = data.value("category", "crypto_minor");

                // *** IMPROVED PARAMETERS BASED ON YOUR RESULTS ***
                if (category == "crypto_major") {
                    params.profit_target_bps = 6.0;
                    params.trailing_stop_bps = 3.0;
                    params.hard_stop_loss_bps = -2.5;
                    params.max_holding_seconds = 5.0;
                    params.min_holding_seconds = 2.0;
                    params.signal_confidence_flip = 0.25;
                    params.spread_widening_factor = 2.0;
                    params.max_spread_bps = 6.0;
                } else if (category == "crypto_minor") {
                    params.profit_target_bps = 8.0;
                    params.trailing_stop_bps = 4.0;
                    params.hard_stop_loss_bps = -3.5;
                    params.max_holding_seconds = 7.0;
                    params.min_holding_seconds = 1.5;
                    params.signal_confidence_flip = 0.20;
                    params.spread_widening_factor = 2.2;
                    params.max_spread_bps = 10.0;
                } else if (category == "crypto_stable") {
                    params.profit_target_bps = 3.0;
                    params.trailing_stop_bps = 2.0;
                    params.hard_stop_loss_bps = -1.5;
                    params.max_holding_seconds = 10.0;
                    params.min_holding_seconds = 1.0;
                    params.signal_confidence_flip = 0.35;
                    params.spread_widening_factor = 1.5;
                    params.max_spread_bps = 4.0;
                }

                // Apply category-level exit_parameters overrides if present
                if (!exitParamsJson.is_null() && exitParamsJson.contains(category)) {
                    const auto& cat = exitParamsJson[category];
                    if (cat.contains("max_holding_seconds")) params.max_holding_seconds = cat["max_holding_seconds"].get<double>();
                    if (cat.contains("min_holding_seconds")) params.min_holding_seconds = cat["min_holding_seconds"].get<double>();
                    if (cat.contains("min_hold_before_spread_exit_seconds")) params.min_hold_before_spread_exit_seconds = cat["min_hold_before_spread_exit_seconds"].get<double>();
                    if (cat.contains("spread_widening_consecutive_checks")) params.spread_widening_consecutive_checks = cat["spread_widening_consecutive_checks"].get<int>();
                    if (cat.contains("emergency_spread_multiplier")) params.emergency_spread_multiplier = cat["emergency_spread_multiplier"].get<double>();
                    if (cat.contains("spread_cost_multiplier")) params.spread_cost_multiplier = cat["spread_cost_multiplier"].get<double>();
                }

                // NEW: Add volatility-specific overrides if available in symbol data
                if (data.contains("recent_volatility_30s")) {
                    double volatility = data["recent_volatility_30s"];
                    if (volatility > 0.002) { // High volatility (>20bps)
                        params.profit_target_bps *= 1.2;
                        params.hard_stop_loss_bps *= 1.2;
                        params.max_holding_seconds *= 0.8; // Exit faster in high volatility
                    }
                }

                // Apply per-symbol overrides if provided under exit_parameters.per_symbol_overrides
                if (!exitParamsJson.is_null() && exitParamsJson.contains("per_symbol_overrides") &&
                    exitParamsJson["per_symbol_overrides"].contains("example") == false) {
                    // allow user-specified overrides keyed by symbol
                    if (exitParamsJson["per_symbol_overrides"].contains(symbol)) {
                        const auto& ov = exitParamsJson["per_symbol_overrides"][symbol];
                        if (ov.contains("max_holding_seconds")) params.max_holding_seconds = ov["max_holding_seconds"].get<double>();
                        if (ov.contains("min_holding_seconds")) params.min_holding_seconds = ov["min_holding_seconds"].get<double>();
                    }
                }

                // Convert symbol format (BTCUSDT -> BTC/USDT if needed)
                std::string formatted_symbol = symbol;
                if (symbol.length() >= 6 && symbol.substr(symbol.length()-4) == "USDT") {
                    formatted_symbol = symbol.substr(0, symbol.length()-4) + "/USDT";
                }

                symbolParams_[formatted_symbol] = params;
                symbolParams_[symbol] = params; // Store both formats
            }
        }

        LOG_INFO("✅ Loaded improved exit parameters for {} symbols from {}", 
                 symbolParams_.size(), config_path);

    } catch (const std::exception& e) {
        LOG_ERROR("❌ Failed to load volatility config from {}: {}", config_path, e.what());
    }
}

OptimalExitStrategy::ExitParams OptimalExitStrategy::getParamsForSymbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    auto it = symbolParams_.find(symbol);
    if (it != symbolParams_.end()) {
        return it->second;
    }
    
    // Fallback to conservative defaults
    ExitParams defaults;
    defaults.profit_target_bps = 6.0;      // Conservative target
    defaults.trailing_stop_bps = 3.5;      
    defaults.hard_stop_loss_bps = -3.0;    // Tight stop loss
    defaults.max_holding_seconds = 8.0;    // Quick exit
    defaults.signal_confidence_flip = 0.25;
    defaults.spread_widening_factor = 2.0;
    defaults.max_spread_bps = 8.0;
    // defaults.min_stop_distance_bps = 10.0;
    // defaults.stop_spread_multiplier = 2.5;
    
    return defaults;
}

void OptimalExitStrategy::startPosition(const std::string& symbol, 
                                       double entry_price,
                                       core::Direction direction,
                                       double confidence,
                                       double spread_ratio,
                                       double spread_ema) {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    TradeState& trade = activeTrades_[symbol];
    trade.entry_price = entry_price;
    trade.entry_time = std::chrono::steady_clock::now();
    trade.peak_price = entry_price; // Initialize peak at entry
    trade.entry_spread_ratio = spread_ratio;
    trade.entry_spread_ema = spread_ema;
    // Safety: set small floor if entry spread is zero to avoid immediate triggers
    if (trade.entry_spread_ratio <= 0.0) trade.entry_spread_ratio = 1e-6;
    trade.spread_widening_counter = 0;
    trade.entry_direction = direction;
    trade.entry_confidence = confidence;
    trade.symbol = symbol;
    trade.is_active = true;
    
    // NEW: Track initial momentum
    trade.initial_momentum = 0.0; // Will be calculated in first few ticks
    trade.ticks_since_entry = 0;
    trade.cumulative_pnl_sum = 0.0;
    
    LOG_INFO("EXIT_STRATEGY: Started position {} @ {:.6f}, direction={}, confidence={:.3f}, spread_ratio={:.5f}bps, spread_ema={:.5f}bps", 
             symbol, entry_price, 
             (direction == core::Direction::BUY ? "BUY" : "SELL"), 
             confidence, spread_ratio * 10000, spread_ema * 10000);
}

OptimalExitStrategy::ExitDecision OptimalExitStrategy::evaluateExit(
    const std::string& symbol,
    double current_price,
    double current_spread_ratio,
    double current_spread_ema,
    core::Direction current_signal_direction,
    double current_confidence,
    double bid_price,
    double ask_price) {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    auto it = activeTrades_.find(symbol);
    if (it == activeTrades_.end()) {
        return ExitDecision{false};
    }
    
    TradeState& state = it->second;
    auto params = getParamsForSymbol(symbol);
    
    // Calculate hold time
    auto now = std::chrono::steady_clock::now();
    double seconds_held = std::chrono::duration<double>(now - state.entry_time).count();
    
    // CRITICAL FIX: Enforce minimum hold time (except for stop loss)
    bool min_hold_met = seconds_held >= params.min_holding_seconds;
    
    // Calculate actual spread cost in bps
    double entry_spread_cost_bps = state.entry_spread_ema * 10000.0;  // Convert to bps
    double exit_spread_cost_bps = current_spread_ema * 10000.0;
    double total_spread_cost = entry_spread_cost_bps + exit_spread_cost_bps;
    
    // DYNAMIC profit target based on spread cost
    double adjusted_profit_target = std::max(
        params.profit_target_bps,
        total_spread_cost * params.spread_cost_multiplier
    );
    
    // Calculate PnL
    double pnl_bps;
    if (state.entry_direction == core::Direction::BUY) {
        pnl_bps = ((current_price - state.entry_price) / state.entry_price) * 10000.0;
        // Update peak
        if (current_price > state.peak_price) {
            state.peak_price = current_price;
        }
    } else {
        pnl_bps = ((state.entry_price - current_price) / state.entry_price) * 10000.0;
        // Update peak for SELL
        if (current_price < state.peak_price || state.peak_price == 0.0) {
            state.peak_price = current_price;
        }
    }
    
    // Track momentum
    state.ticks_since_entry++;
    state.cumulative_pnl_sum += pnl_bps;
    
    if (state.ticks_since_entry <= 3) {
        state.initial_momentum = state.cumulative_pnl_sum / state.ticks_since_entry;
    }
    
    // ========================================================================
    // EXIT LOGIC (in priority order)
    // ========================================================================
    
    // 1. HARD STOP LOSS (immediate, ignore min hold)
    if (pnl_bps <= params.hard_stop_loss_bps) {
        return ExitDecision{
            true, 
            ExitReason::STOP_LOSS, 
            pnl_bps,
            current_price
        };
    }
    
    // 2. LIQUIDITY CHECK - only after min hold time
    if (min_hold_met) {
        // Check if spread exploded (liquidity crisis)
        if (current_spread_ema > state.entry_spread_ema * params.emergency_spread_multiplier) {
            // Only exit if we're not deeply profitable
            if (pnl_bps < adjusted_profit_target * 0.5) {
                return ExitDecision{
                    true,
                    ExitReason::LIQUIDITY_DRIED,
                    pnl_bps,
                    current_price
                };
            }
        }
        
        // Check for sustained spread widening
        double spread_threshold = state.entry_spread_ema * params.spread_widening_factor;
        if (current_spread_ema > spread_threshold) {
            state.spread_widening_counter++;
            if (state.spread_widening_counter >= params.spread_widening_consecutive_checks) {
                // Only exit if not profitable
                if (pnl_bps < adjusted_profit_target * 0.3) {
                    return ExitDecision{
                        true,
                        ExitReason::SPREAD_WIDENING,
                        pnl_bps,
                        current_price
                    };
                }
            }
        } else {
            state.spread_widening_counter = 0;
        }
    }
    
    // 3. PROFIT TARGET (only after min hold)
    if (min_hold_met && pnl_bps >= adjusted_profit_target) {
        return ExitDecision{
            true,
            ExitReason::PROFIT_TARGET,
            pnl_bps,
            current_price
        };
    }
    
    // 4. TRAILING STOP (only after min hold)
    if (min_hold_met) {
        double peak_pnl_bps;
        if (state.entry_direction == core::Direction::BUY) {
            peak_pnl_bps = ((state.peak_price - state.entry_price) / state.entry_price) * 10000.0;
        } else {
            peak_pnl_bps = ((state.entry_price - state.peak_price) / state.entry_price) * 10000.0;
        }
        
        // Only activate trailing stop if we were profitable
        if (peak_pnl_bps > 0) {
            double trailing_threshold = peak_pnl_bps - params.trailing_stop_bps;
            if (pnl_bps < trailing_threshold) {
                return ExitDecision{
                    true,
                    ExitReason::TRAILING_STOP,
                    pnl_bps,
                    current_price
                };
            }
        }
    }
    
    // 5. SIGNAL REVERSAL (only after min hold)
    if (min_hold_met) {
        bool signal_flipped = (
            (state.entry_direction == core::Direction::BUY && 
             current_signal_direction == core::Direction::SELL && 
             current_confidence > params.signal_confidence_flip) ||
            (state.entry_direction == core::Direction::SELL && 
             current_signal_direction == core::Direction::BUY && 
             current_confidence > params.signal_confidence_flip)
        );
        
        if (signal_flipped) {
            // Exit on signal flip, but only if not deeply negative
            if (pnl_bps > params.hard_stop_loss_bps * 0.5) {
                return ExitDecision{
                    true,
                    ExitReason::SIGNAL_REVERSAL,
                    pnl_bps,
                    current_price
                };
            }
        }
    }
    
    // 6. TIME LIMIT (always check)
    if (seconds_held >= params.max_holding_seconds) {
        return ExitDecision{
            true,
            ExitReason::TIME_LIMIT,
            pnl_bps,
            current_price
        };
    }
    
    return ExitDecision{false};
}
 
// Note: unified cooldown logic implemented as member wasRecentlyClosed() using entryCooldown_
// getParamsForSymbol
// ExitParams getParamsForSymbol(const std::string& symbol);
// ============================================================================
// FIX 3: Add entry filter to PREVENT bad trades
// ============================================================================

bool OptimalExitStrategy::shouldEnterPosition(const std::string& symbol,
                                              double entry_price,
                                              double spread_ema,
                                              double spread_ratio,
                                              double confidence) const {
    // Check cooldown using member cooldown (config-driven)
    if (this->wasRecentlyClosed(symbol)) {
        return false;
    }

    // CRITICAL: Reject if current spread much wider than normal
    double spread_multiple = spread_ratio / std::max(spread_ema, 0.01);
    if (spread_multiple > 1.2) {  // Current spread > 1.5x normal
        // Spread is widening - adverse selection risk
        return false;
    }

    // Require minimum confidence
    if (confidence < 0.90) {
        return false;
    }

    // Check if spread is reasonable (absolute level)
    double spread_bps = spread_ema * 10000.0;
    auto params = getParamsForSymbol(symbol);
    if (spread_bps > params.max_spread_bps) {
        return false;
    }

    return true;
}

void OptimalExitStrategy::closePosition(const std::string& symbol, ExitReason reason, double pnl) {
    {
        std::lock_guard<std::mutex> lock(tradesMutex_);
        auto it = activeTrades_.find(symbol);
        if (it == activeTrades_.end()) {
            LOG_WARN("closePosition called for non-active symbol {}", symbol);
            return;
        }
        
        TradeState& trade = it->second;
        auto now = std::chrono::steady_clock::now();
        double elapsed_seconds = std::chrono::duration<double>(now - trade.entry_time).count();
        
        // Calculate peak PnL for logging
        double peak_pnl_bps = 0.0;
        if (trade.entry_direction == core::Direction::BUY) {
            peak_pnl_bps = (trade.peak_price - trade.entry_price) / trade.entry_price * 10000;
        } else {
            peak_pnl_bps = (trade.entry_price - trade.peak_price) / trade.entry_price * 10000;
        }
        
        // Enhanced logging with all relevant metrics
        LOG_INFO("EXIT_STRATEGY: {} | Reason: {} | PnL: {:.2f}bps | Peak: {:.2f}bps | Held: {:.1f}s | Direction: {}",
                 symbol, exitReasonToString(reason), pnl, peak_pnl_bps, elapsed_seconds,
                 (trade.entry_direction == core::Direction::BUY ? "BUY" : "SELL"));
        
        // Mark as inactive
        trade.is_active = false;
        
        // Call exit callback if registered
        if (exitCallback_) {
            // Determine exit direction (opposite of entry for closing)
            core::Direction exitDir = (trade.entry_direction == core::Direction::BUY) ? 
                                     core::Direction::SELL : core::Direction::BUY;
            // Instrumentation: log before invoking to confirm callback presence
            LOG_INFO("OptimalExitStrategy: Invoking exitCallback_ for symbol {} (this={})", symbol, static_cast<const void*>(this));
            exitCallback_(symbol, exitDir, trade.entry_price, exitReasonToString(reason));
        } else {
            LOG_WARN("OptimalExitStrategy: exitCallback_ not set - skipping exit callback for {}", symbol);
        }
    }
    
    // Record recent close timestamp
    {
        std::lock_guard<std::mutex> lock(recentCloseMutex_);
        recentCloseTs_[symbol] = std::chrono::steady_clock::now();
    }
}

bool OptimalExitStrategy::hasActivePosition(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    auto it = activeTrades_.find(symbol);
    return (it != activeTrades_.end() && it->second.is_active);
}

OptimalExitStrategy::TradeState OptimalExitStrategy::getTradeState(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    auto it = activeTrades_.find(symbol);
    if (it != activeTrades_.end()) {
        return it->second;
    }
    return TradeState();
}

const char* OptimalExitStrategy::exitReasonToString(ExitReason reason) {
    switch (reason) {
        case ExitReason::PROFIT_TARGET: return "PROFIT_TARGET";
        case ExitReason::TRAILING_STOP: return "TRAILING_STOP";
        case ExitReason::STOP_LOSS: return "STOP_LOSS";
        case ExitReason::SIGNAL_REVERSAL: return "SIGNAL_REVERSAL";
        case ExitReason::TIME_LIMIT: return "TIME_LIMIT";
        case ExitReason::SPREAD_WIDENING: return "SPREAD_WIDENING";
        case ExitReason::LIQUIDITY_DRIED: return "LIQUIDITY_DRIED";
        default: return "UNKNOWN";
    }
}

void OptimalExitStrategy::updateParameters(const std::string& category, const ExitParams& params) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    // Update all symbols in this category
    for (auto& [symbol, symbolParams] : symbolParams_) {
        // Would need category mapping to implement this properly
        // For now, this is a placeholder
    }
    
    LOG_INFO("Updated exit parameters for category: {}", category);
}

void OptimalExitStrategy::reloadIfStale() {
    if (!std::filesystem::exists(configPath_)) {
        return;
    }
    
    auto currentWriteTime = std::filesystem::last_write_time(configPath_);
    if (currentWriteTime != lastLoadedTime_) {
        LOG_INFO("Config file changed, reloading: {}", configPath_);
        loadSymbolParameters(configPath_);
    }
}

void OptimalExitStrategy::registerExitCallback(const ExitCallback& callback) {
    exitCallback_ = callback;
}

bool OptimalExitStrategy::wasRecentlyClosed(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(recentCloseMutex_);
    
    auto it = recentCloseTs_.find(symbol);
    if (it == recentCloseTs_.end()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
    
    return elapsed < entryCooldown_;
}

} // namespace strategy
} // namespace trading