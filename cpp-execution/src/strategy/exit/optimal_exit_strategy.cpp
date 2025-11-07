#include "strategy/exit/optimal_exit_strategy.hpp"
#include "common/logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace trading {
namespace strategy {

using json = nlohmann::json;

OptimalExitStrategy::OptimalExitStrategy() {
    // Load default parameters for major crypto pairs
    loadSymbolParameters("config/symbol_volatility_config.json");
}

OptimalExitStrategy::OptimalExitStrategy(const std::string& volatility_config_path) {
    loadSymbolParameters(volatility_config_path);
}

void OptimalExitStrategy::loadSymbolParameters(const std::string& config_path) {
    try {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            LOG_WARN("Could not open volatility config: {}, using defaults", config_path);
            return;
        }
        
        json config;
        file >> config;
        
        // Load per-symbol parameters
        if (config.contains("symbol_volatility_thresholds") && 
            config["symbol_volatility_thresholds"].contains("symbols")) {
            
            for (const auto& [symbol, data] : config["symbol_volatility_thresholds"]["symbols"].items()) {
                ExitParams params;
                
                std::string category = data.value("category", "crypto_minor");
                
                // Adjust parameters based on volatility category
                if (category == "crypto_major") {
                    params.profit_target_bps = 8.0;    // Higher liquidity = tighter targets
                    params.trailing_stop_bps = 4.0;    // Tighter trailing stop
                    params.hard_stop_loss_bps = -3.0;  // Smaller max loss
                    params.max_holding_seconds = 8.0;   // Shorter holding time
                    params.max_spread_bps = 8.0;       // Tighter spread guard for majors
                    params.min_stop_distance_bps = 10.0; // Smaller min stop for majors
                } else if (category == "crypto_minor") {
                    params.profit_target_bps = 12.0;   // Higher volatility = wider targets
                    params.trailing_stop_bps = 6.0;
                    params.hard_stop_loss_bps = -5.0;
                    params.max_holding_seconds = 12.0;
                    params.max_spread_bps = 12.0;      // Wider spread tolerance for minors
                    params.min_stop_distance_bps = 15.0; // Larger min stop for minors
                } else if (category == "crypto_stable") {
                    params.profit_target_bps = 5.0;    // Very stable = tight targets
                    params.trailing_stop_bps = 3.0;
                    params.hard_stop_loss_bps = -2.0;
                    params.max_holding_seconds = 15.0; // Can hold longer
                    params.max_spread_bps = 6.0;       // Very tight spread for stables
                    params.min_stop_distance_bps = 8.0;  // Smallest min stop for stables
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
        
        LOG_INFO("Loaded exit parameters for {} symbols", symbolParams_.size());
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load volatility config: {}", e.what());
    }
}

OptimalExitStrategy::ExitParams OptimalExitStrategy::getParamsForSymbol(const std::string& symbol) const {
    auto it = symbolParams_.find(symbol);
    if (it != symbolParams_.end()) {
        return it->second;
    }
    
    // Default parameters for unknown symbols (treat as minor crypto)
    ExitParams defaults;
    defaults.profit_target_bps = 10.0;
    defaults.trailing_stop_bps = 5.0;
    defaults.hard_stop_loss_bps = -4.0;
    defaults.max_holding_seconds = 10.0;
    defaults.signal_confidence_flip = 0.3;
    defaults.spread_widening_factor = 2.5;
    defaults.max_spread_bps = 10.0;
    defaults.min_stop_distance_bps = 12.0;
    defaults.stop_spread_multiplier = 2.5;
    
    return defaults;
}

void OptimalExitStrategy::startPosition(const std::string& symbol, 
                                       double entry_price,
                                       core::Direction direction,
                                       double confidence,
                                       double spread_ratio) {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    TradeState& trade = activeTrades_[symbol];
    trade.entry_price = entry_price;
    trade.entry_time = std::chrono::steady_clock::now();
    trade.peak_price = entry_price; // Initialize peak at entry
    trade.entry_spread_ratio = spread_ratio;
    trade.entry_direction = direction;
    trade.entry_confidence = confidence;
    trade.symbol = symbol;
    trade.is_active = true;
    
    LOG_INFO("EXIT_STRATEGY: Started position {} @ {:.6f}, direction={}, confidence={:.3f}", 
             symbol, entry_price, (direction == core::Direction::BUY ? "BUY" : "SELL"), confidence);
}

OptimalExitStrategy::ExitDecision OptimalExitStrategy::evaluateExit(
    const std::string& symbol,
    double current_price,
    double current_spread_ratio,
    core::Direction current_signal_direction,
    double current_confidence,
    double bid_price,
    double ask_price) {
    
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    ExitDecision decision;
    
    auto it = activeTrades_.find(symbol);
    if (it == activeTrades_.end() || !it->second.is_active) {
        return decision; // No active position
    }
    
    TradeState& trade = it->second;
    ExitParams params = getParamsForSymbol(symbol);
    
    // GUARD 1: Spread check - (ask-bid)/mid ≤ 0.001 (max 0.1% spread)
    if (bid_price > 0.0 && ask_price > 0.0 && current_price > 0.0) {
        double spread_abs = ask_price - bid_price;
        double spread_relative = spread_abs / current_price; // (ask-bid)/mid
        double spread_bps = spread_relative * 10000.0;
        
        if (spread_bps > params.max_spread_bps) {
            decision.should_exit = true;
            decision.reason = ExitReason::SPREAD_WIDENING;
            decision.expected_pnl = 0.0; // Will be calculated later
            decision.exit_price = current_price;
            return decision;
        }
    }
    
    auto now = std::chrono::steady_clock::now();
    double elapsed_seconds = std::chrono::duration<double>(now - trade.entry_time).count();
    
    // Calculate PnL in basis points
    double pnl_bps = 0.0;
    if (trade.entry_direction == core::Direction::BUY) {
        pnl_bps = (current_price - trade.entry_price) / trade.entry_price * 10000;
        // Update peak for BUY positions
        if (current_price > trade.peak_price) {
            trade.peak_price = current_price;
        }
    } else {
        pnl_bps = (trade.entry_price - current_price) / trade.entry_price * 10000;
        // Update peak for SELL positions (lowest price is "peak")
        if (current_price < trade.peak_price) {
            trade.peak_price = current_price;
        }
    }
    
    decision.expected_pnl = pnl_bps;
    decision.exit_price = current_price;
    
    // LAYER 1: PROFIT TARGET (highest priority) ⭐
    if (pnl_bps >= params.profit_target_bps) {
        decision.should_exit = true;
        decision.reason = ExitReason::PROFIT_TARGET;
        return decision;
    }
    
    // LAYER 2: HARD STOP LOSS (protect capital) 🛡️
    // GUARD 2: Stop distance check - stop_distance ≥ max(2.5×spread, 12 bps)
    double required_stop_distance = params.min_stop_distance_bps;
    if (bid_price > 0.0 && ask_price > 0.0 && current_price > 0.0) {
        double spread_abs = ask_price - bid_price;
        double spread_bps = (spread_abs / current_price) * 10000.0;
        double spread_based_min = spread_bps * params.stop_spread_multiplier;
        required_stop_distance = std::max(required_stop_distance, spread_based_min);
    }
    
    // Adjust hard stop based on guard
    double effective_hard_stop = std::min(params.hard_stop_loss_bps, -required_stop_distance);
    
    if (pnl_bps <= effective_hard_stop) {
        decision.should_exit = true;
        decision.reason = ExitReason::STOP_LOSS;
        return decision;
    }
    
    // LAYER 3: TRAILING STOP (protect profit) 📉
    double drawdown_from_peak_bps = 0.0;
    if (trade.entry_direction == core::Direction::BUY) {
        drawdown_from_peak_bps = (trade.peak_price - current_price) / trade.peak_price * 10000;
    } else {
        drawdown_from_peak_bps = (current_price - trade.peak_price) / trade.peak_price * 10000;
    }
    
    if (drawdown_from_peak_bps >= params.trailing_stop_bps) {
        decision.should_exit = true;
        decision.reason = ExitReason::TRAILING_STOP;
        return decision;
    }
    
    // LAYER 4: SIGNAL REVERSAL (immediate response) ⚡
    if (elapsed_seconds >= 2.0) { // Check after 2s (match training window)
        bool signal_flipped = (current_signal_direction != trade.entry_direction);
        bool confidence_dropped = (current_confidence < params.signal_confidence_flip);
        
        if (signal_flipped || confidence_dropped) {
            decision.should_exit = true;
            decision.reason = ExitReason::SIGNAL_REVERSAL;
            return decision;
        }
    }
    
    // LAYER 5: SPREAD WIDENING (liquidity check) 💧
    if (current_spread_ratio > trade.entry_spread_ratio * params.spread_widening_factor) {
        decision.should_exit = true;
        decision.reason = ExitReason::SPREAD_WIDENING;
        return decision;
    }
    
    // LAYER 6: TIME LIMIT (fallback) ⏰
    if (elapsed_seconds >= params.max_holding_seconds) {
        decision.should_exit = true;
        decision.reason = ExitReason::TIME_LIMIT;
        return decision;
    }
    
    // No exit conditions met - continue holding
    return decision;
}

void OptimalExitStrategy::closePosition(const std::string& symbol, ExitReason reason, double pnl) {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    auto it = activeTrades_.find(symbol);
    if (it != activeTrades_.end()) {
        TradeState& trade = it->second;
        
        auto now = std::chrono::steady_clock::now();
        double held_seconds = std::chrono::duration<double>(now - trade.entry_time).count();
        double peak_pnl = 0.0;
        
        if (trade.entry_direction == core::Direction::BUY) {
            peak_pnl = (trade.peak_price - trade.entry_price) / trade.entry_price * 10000;
        } else {
            peak_pnl = (trade.entry_price - trade.peak_price) / trade.entry_price * 10000;
        }
        
        LOG_INFO("EXIT_STRATEGY: {} | Reason: {} | PnL: {:.2f}bps | Peak: {:.2f}bps | Held: {:.1f}s",
                 symbol, exitReasonToString(reason), pnl, peak_pnl, held_seconds);
        
        trade.is_active = false;
        activeTrades_.erase(it);
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
    return TradeState{};
}

const char* OptimalExitStrategy::exitReasonToString(ExitReason reason) {
    static const char* reason_strings[] = {
        "PROFIT_TARGET", "TRAILING_STOP", "STOP_LOSS",
        "SIGNAL_REVERSAL", "TIME_LIMIT", "SPREAD_WIDENING", "LIQUIDITY_DRIED"
    };
    return reason_strings[static_cast<int>(reason)];
}

void OptimalExitStrategy::updateParameters(const std::string& category, const ExitParams& params) {
    // Update all symbols in this category
    for (auto& [symbol, symbol_params] : symbolParams_) {
        // This would need category lookup - simplified for now
        symbol_params = params;
    }
    
    LOG_INFO("Updated exit parameters for category: {}", category);
}

} // namespace strategy
} // namespace trading