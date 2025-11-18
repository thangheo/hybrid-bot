#include "strategy/exit/adaptive_exit_strategy.hpp" 
#include "strategy/core_analysis/orderbook_regime_detector.hpp"
#include <numeric>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <mutex>
#include <common/logger.hpp>
namespace trading {
namespace strategy {

// File-scoped tracking to avoid spamming WARN logs for missing/invalid tick_size
static std::unordered_set<std::string> g_tickSizeWarnedSymbols;
static std::mutex g_tickWarnMutex;

// AdaptiveExitStrategy::AdaptiveExitStrategy(double prediction_window_seconds) 
//     : predictionWindow_(prediction_window_seconds) {}
AdaptiveExitStrategy::AdaptiveExitStrategy(double prediction_window_seconds) 
    : predictionWindow_(prediction_window_seconds * 1000.0) {
        // Set history depth based on prediction window
        size_t default_samples = static_cast<size_t>(std::max(1.0, prediction_window_seconds));
        // store as member for potential reuse in other routines
        default_history_samples_ = default_samples;
        //init history.max_samples to default_samples
        // for all symbols when they are first seen    

    }  // Convert to milliseconds
void AdaptiveExitStrategy::updateMarketData(
    const std::string& symbol,
    double volatility_1m,
    double spread_ema,
    double orderbook_imbalance,
    double trade_intensity,
    double price_return) {
    std::lock_guard<std::mutex> lock(historyMutex_);
    auto& history = marketHistory_[symbol];

    bool was_empty = history.volatility_samples.empty() && history.spread_samples.empty() && history.price_returns.empty();

    // Initialize max_samples on first observation for this symbol.
    if (was_empty) {
        // predictionWindow_ stored in ms; default_history_samples_ is in seconds sampling at ~1Hz
        history.max_samples = default_history_samples_;
        LOG_DEBUG("AdaptiveExitStrategy: initialized history for {} with max_samples={} (prediction_window_seconds={:.1f})",
                  symbol, history.max_samples, predictionWindow_/1000.0);
    }

    // Update rolling windows
    history.volatility_samples.push_back(volatility_1m);
    history.spread_samples.push_back(spread_ema);
    history.volume_samples.push_back(trade_intensity);
    history.price_returns.push_back(price_return);

    // Maintain window size
    while (history.volatility_samples.size() > history.max_samples) {
        history.volatility_samples.pop_front();
    }
    while (history.spread_samples.size() > history.max_samples) {
        history.spread_samples.pop_front();
    }
    while (history.volume_samples.size() > history.max_samples) {
        history.volume_samples.pop_front();
    }
    while (history.price_returns.size() > history.max_samples) {
        history.price_returns.pop_front();
    }

}

void AdaptiveExitStrategy::ensureHistoryInitialized(const std::string& symbol) {
    std::lock_guard<std::mutex> lk(historyMutex_);
    auto it = marketHistory_.find(symbol);
    if (it == marketHistory_.end()) return; // nothing to init
    if (it->second.max_samples == 0) {
        it->second.max_samples = default_history_samples_;
        LOG_DEBUG("AdaptiveExitStrategy: ensureHistoryInitialized set max_samples for {} -> {} samples",
                  symbol, it->second.max_samples);
    }
}

size_t AdaptiveExitStrategy::getHistoryMaxSamples(const std::string& symbol) const {
    std::lock_guard<std::mutex> lk(historyMutex_);
    auto it = marketHistory_.find(symbol);
    if (it == marketHistory_.end()) return 0;
    return it->second.max_samples;
}

void AdaptiveExitStrategy::setPredictionWindow(double prediction_window_seconds) {
    std::lock_guard<std::mutex> lk(historyMutex_);
    predictionWindow_ = prediction_window_seconds * 1000.0;
    // update default sample count assuming approximate 1Hz updates
    default_history_samples_ = static_cast<size_t>(std::max(1.0, prediction_window_seconds));

    // resize and trim existing histories
    for (auto& kv : marketHistory_) {
        auto& history = kv.second;
        history.max_samples = default_history_samples_;

        while (history.volatility_samples.size() > history.max_samples) history.volatility_samples.pop_front();
        while (history.spread_samples.size() > history.max_samples) history.spread_samples.pop_front();
        while (history.volume_samples.size() > history.max_samples) history.volume_samples.pop_front();
        while (history.price_returns.size() > history.max_samples) history.price_returns.pop_front();
    }

    LOG_INFO("AdaptiveExitStrategy::setPredictionWindow: new window={}s, default_history_samples_={}", predictionWindow_/1000.0, default_history_samples_);
}

void AdaptiveExitStrategy::updateRegime(const std::string& symbol, const MarketMicrostructure& microstructure) {
    std::lock_guard<std::mutex> lock(historyMutex_);
    auto it = marketHistory_.find(symbol);
    if (it != marketHistory_.end()) {
        MarketRegimeType regime = detectMarketRegime(microstructure, it->second);
        std::lock_guard<std::mutex> regime_lock(regimeMutex_);
        regimeCache_[symbol] = regime;
    }
}

MarketRegimeType AdaptiveExitStrategy::detectMarketRegime(
    const MarketMicrostructure& micro,
    const MarketHistory& history) const {
    // Delegate regime detection to OrderbookRegimeDetector
    trading::strategy::core_analysis::OrderbookRegimeDetector det;
    return det.detect(micro, history.volatility_samples, history.spread_samples, history.price_returns);
}

AdaptiveExitStrategy::DynamicParameters AdaptiveExitStrategy::calculateDynamicParameters(
    const std::string& symbol,
    const MarketMicrostructure& cur,
    const MarketRegimeType& regime) const 
{
    DynamicParameters p;

    // ===== Safety tick =====
    double tick = cur.tick_size;
    if (tick <= 0.0) {
        // return p;  // Cannot compute dynamic parameters without valid tick size
        double mp = cur.mid_price > 0 ? cur.mid_price : 1.0;
        tick = mp * 1e-6;     // fallback theo mid price
    }

    // Bound tick để tránh explode
    tick = std::clamp(tick, 1e-8, 1.0);

    // ===== Convert volatility and spread to ticks =====
    double vol_ticks   = cur.volatility_1m / tick;
    double spr_ticks   = cur.spread_ema / tick;

    // tránh zero → min stability
    vol_ticks = std::max(vol_ticks, 1e-6);
    spr_ticks = std::max(spr_ticks, 1e-6);

    // ===== Prediction window (2s) =====
    double pw_sec = predictionWindow_ / 1000.0;   // 2000 ms = 2s
    double time_factor = std::sqrt(pw_sec);       // scale đúng đơn vị

    // ===== Regime parameters =====
    double alpha = 2.0;
    double beta  = 2.5;
    double gamma = 0.4;
    double decay = 1.0;

    switch (regime) {
        case MarketRegimeType::STRESS:
            alpha = 1.2;  beta = 5.0;  gamma = 0.2;  decay = 0.4;
            break;

        case MarketRegimeType::HIGH_VOLATILITY:
            alpha = 3.0;  beta = 2.5;  gamma = 0.35; decay = 0.8;
            break;

        case MarketRegimeType::LOW_LIQUIDITY:
            alpha = 1.8;  beta = 4.0;  gamma = 0.3;  decay = 0.7;
            break;

        case MarketRegimeType::TRENDING:
            alpha = 2.5;  beta = 2.0;  gamma = 0.55; decay = 1.5;
            break;

        case MarketRegimeType::MEAN_REVERTING:
            alpha = 1.4;  beta = 3.0;  gamma = 0.25; decay = 0.5;
            break;

        case MarketRegimeType::QUIET:
            alpha = 1.2;  beta = 2.0;  gamma = 0.3;  decay = 1.0;
            break;

        case MarketRegimeType::NORMAL:
        default:
            break;
    }

    // ===== Profit target =====
    double pt = alpha * vol_ticks * time_factor + beta * spr_ticks;
    pt = std::clamp(pt, 0.5, 50.0);     // floor 0.5 ticks

    p.profit_target_ticks = pt;

    // ===== Stop loss =====
    double sl = -1.645 * vol_ticks * time_factor - spr_ticks;

    if (regime == MarketRegimeType::HIGH_VOLATILITY) sl *= 1.5;
    if (regime == MarketRegimeType::LOW_LIQUIDITY)   sl *= 1.3;
    if (regime == MarketRegimeType::STRESS)          sl *= 2.0;

    sl = std::max(sl, -50.0);            // floor SL
    p.stop_loss_ticks = sl;

    // ===== Trailing =====
    p.trailing_stop_ticks = std::max(0.5, gamma * pt);

    // ===== Holding time =====
    double T_max = pw_sec * decay;
    // double T_min = std::max(1.0, std::max( (spr_ticks / vol_ticks) * 2.0, pw_sec * 0.5 ));
    double T_min = std::max(1.0, pw_sec * 0.5);  // Should use pw_sec, not hardcoded
    p.max_holding_time = T_max;
    p.min_holding_time = T_min;

    // ===== Signal reversal threshold =====
    double base_rev = 0.2 + 0.3 * (cur.volatility_1m / 0.001);
    base_rev = std::min(base_rev, 0.7);

    if (regime == MarketRegimeType::STRESS) base_rev = 0.8;
    if (regime == MarketRegimeType::QUIET)  base_rev = 0.15;

    p.signal_reversal_threshold = base_rev;

    // ===== Emergency multipliers =====
    p.spread_emergency_multiplier = 3.0 + spr_ticks / 10.0;
    p.volatility_spike_threshold = std::clamp(std::max(4.0, vol_ticks * 3.0), 4.0, 100.0);

    // ===== Expected edge + Kelly =====
    auto it = historicalPnLs_.find(symbol);
    if (it != historicalPnLs_.end() && it->second.size() >= 50) {

        const auto& pnls = it->second;

        double wins = std::count_if(pnls.begin(), pnls.end(), [](double x){return x>0;});
        double wr = wins / pnls.size();

        double avgW=0, avgL=0; int wc=0, lc=0;
        for(double x: pnls){
            if(x>0){avgW+=x; wc++;}
            else if(x<0){avgL+=-x; lc++;}
        }

        if (wc>0) avgW/=wc;
        if (lc>0) avgL/=lc;

        if (avgL>0) {
            double b = avgW/avgL;
            double k = (wr*b - (1-wr)) / b;
            p.kelly_fraction = std::clamp(k, 0.0, 0.25);
        }
        double avgW_bps = (avgW / cur.mid_price) * 10000.0;
        double avgL_bps = (avgL / cur.mid_price) * 10000.0;

        p.expected_edge = wr*avgW_bps - (1-wr)*avgL_bps;   // bps
        // p.expected_edge = wr*avgW - (1-wr)*avgL;

        double stdp = calculateStandardDeviation(pnls);
        p.sharpe_estimate = (stdp>0) ? (p.expected_edge/stdp * std::sqrt(252.0)) : 0.5;

    } else {

        p.kelly_fraction = 0.02;
        // Default expected edge: express in basis points (bps) to match
        // historicalPnLs_ which are stored as bps (see evaluateExit -> decision.expected_pnl)
        // spr_ticks is number of ticks; convert ticks -> price -> bps
        double spread_price = cur.spread_ema; // raw price spread
        double spread_bps = (cur.mid_price > 0.0) ? (spread_price / cur.mid_price * 10000.0) : (spread_price * 10000.0);
        // p.expected_edge = spread_bps * 0.1; // default: 10% of spread in bps
        p.expected_edge = spread_bps * 0.25;         // 25% của spread
        p.expected_edge = std::clamp(p.expected_edge, 
                                    0.2,            // min 0.2 bps
                                    5.0);           // max 5 bps

        p.sharpe_estimate = 0.5;
    }

    return p;
}

void AdaptiveExitStrategy::startPosition(
    const std::string& symbol,
    double entry_price,
    core::Direction direction,
    double confidence,
    const MarketMicrostructure& microstructure) {
    
    // Ensure history is initialized for symbol before using it in regime detection.
    ensureHistoryInitialized(symbol);
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    auto& state = activeTrades_[symbol];
    // Guard against duplicate startPosition calls: if a position is already
    // being tracked as active for this symbol, do not overwrite it. This can
    // happen when the system calls startPosition both at signal generation
    // and again on execution confirmation. Ignore duplicate starts to avoid
    // corrupting entry_time/entry_price and inflating historical stats.
    if (state.is_active) {
        // LOG_WARN("AdaptiveExitStrategy::startPosition called but position already active for {} - ignoring duplicate start", symbol);
        return;
    }
    state.entry_price = entry_price;
    state.entry_time = std::chrono::steady_clock::now();
    state.peak_price = entry_price;
    state.trough_price = entry_price;
    state.entry_microstructure = microstructure;
    state.entry_direction = direction;
    state.entry_confidence = confidence;
    state.symbol = symbol;
    state.is_active = true;
    
    // LOG symbols and map
    LOG_DEBUG("AdaptiveExitStrategy: started tracking position on {} at price {:.6f} dir={} conf={:.3f}",
             symbol, entry_price,
             (direction == core::Direction::BUY ? "BUY" : "SELL"),
             confidence);
    // Detect entry regime
    auto it = marketHistory_.find(symbol);
    if (it != marketHistory_.end()) {
        state.entry_regime = getCurrentMarketRegime(symbol);
    } else {
        state.entry_regime = MarketRegimeType::UNKNOWN;
    }
    
    // Reset tracking
    state.recent_pnls.clear();
    state.max_favorable_excursion = 0.0;
    state.max_adverse_excursion = 0.0;
    state.momentum_score = 0.0;
}

AdaptiveExitStrategy::ExitDecision AdaptiveExitStrategy::evaluateExit(
    const std::string& symbol,
    double current_price,
    const MarketMicrostructure& current_micro,
    core::Direction current_signal_direction,
    double current_confidence,
    const std::vector<core::PriceTarget>& signal_targets,
    const core::Duration& signal_ttl,
    const core::Timestamp& signal_generation_time) {
    
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    auto it = activeTrades_.find(symbol);
    if (it == activeTrades_.end() || !it->second.is_active) {
        return ExitDecision{false};
    }
    
    auto& state = it->second;
    
    // Get current dynamic parameters
    auto regime = detectMarketRegime(current_micro, marketHistory_[symbol]);
    auto params = calculateDynamicParameters(symbol, current_micro, regime);

    // If caller didn't pass signal targets/ttl, consult stored latest signal for this symbol
    std::vector<core::PriceTarget> actual_targets = signal_targets;
    core::Duration actual_ttl = signal_ttl;
    core::Timestamp actual_generation_time = signal_generation_time;
    if (actual_targets.empty() && actual_ttl == core::Duration::zero()) {
        std::lock_guard<std::mutex> lk(latestSignalMutex_);
        auto sit = latestSignalBySymbol_.find(symbol);
        if (sit != latestSignalBySymbol_.end()) {
            const auto& s = sit->second;
            // check validity by TTL/expiration
            auto now_ts = core::now();
            if (s.expirationTime.time_since_epoch().count() != 0) {
                if (now_ts < s.expirationTime) {
                    actual_targets = s.targets;
                    actual_ttl = s.ttl;
                    actual_generation_time = s.generationTime;
                } else {
                    // expired: clear stored entry
                    // we will clear outside the locked map to be safe
                }
            } else {
                // if expirationTime not set, use generationTime+ttl
                if ((s.generationTime + s.ttl) > now_ts) {
                    actual_targets = s.targets;
                    actual_ttl = s.ttl;
                    actual_generation_time = s.generationTime;
                }
            }
        }
    }
    
    // Calculate PnL in ticks
    double pnl_ticks = 0.0;
    double tick_now = current_micro.tick_size > 0.0 ? current_micro.tick_size : 1e-8;
    if (current_micro.tick_size <= 0.0) {
        // Warn only once per symbol
        std::lock_guard<std::mutex> lk(g_tickWarnMutex);
        if (g_tickSizeWarnedSymbols.find(symbol) == g_tickSizeWarnedSymbols.end()) {
            LOG_WARN("AdaptiveExitStrategy::evaluateExit: current_micro.tick_size <= 0 ({}). Using fallback tick for symbol={}", current_micro.tick_size, symbol);
            g_tickSizeWarnedSymbols.insert(symbol);
        }
    }

    if (state.entry_direction == core::Direction::BUY) {
        pnl_ticks = (current_price - state.entry_price) / tick_now;
    } else {
        pnl_ticks = (state.entry_price - current_price) / tick_now;
    }
    
    // Update tracking
    state.recent_pnls.push_back(pnl_ticks);
    if (state.recent_pnls.size() > 10) {
        state.recent_pnls.pop_front();
    }
    
    // Update MFE/MAE
    state.max_favorable_excursion = std::max(state.max_favorable_excursion, pnl_ticks);
    state.max_adverse_excursion = std::min(state.max_adverse_excursion, pnl_ticks);
    
    // Update peak/trough
    if (state.entry_direction == core::Direction::BUY) {
        state.peak_price = std::max(state.peak_price, current_price);
    } else {
        state.trough_price = std::min(state.trough_price, current_price);
    }
    
    // Calculate momentum (rate of PnL change)
    if (state.recent_pnls.size() >= 3) {
        double recent_sum = std::accumulate(state.recent_pnls.begin(), 
                                           state.recent_pnls.end(), 0.0);
        state.momentum_score = recent_sum / state.recent_pnls.size();
    }
    
    // Time held
    auto now = std::chrono::steady_clock::now();
    double seconds_held = std::chrono::duration<double>(now - state.entry_time).count();
    
    ExitDecision decision;
    decision.exit_price = current_price;
    // Protect against zero/invalid entry_price when converting to relative expected PnL (bps)
    if (state.entry_price <= 0.0) {
        LOG_WARN("AdaptiveExitStrategy::evaluateExit: entry_price <= 0 ({}). expected_pnl set to 0 for symbol={}", state.entry_price, symbol);
        decision.expected_pnl = 0.0;
    } else {
        // Use dedicated helper to compute signal PnL in bps to avoid mixing tick-level
        // and expected/realized PnL semantics.
        decision.expected_pnl = AdaptiveExitStrategy::computeSignalPnlBps(state.entry_price, current_price, state.entry_direction);
    }
    
    // ===== EXIT CONDITIONS (Priority Order) =====
    
    // Skip remaining checks if minimum hold time not met
    if (seconds_held < params.min_holding_time) {
        return decision;
    }

    // 1. STOP LOSS (immediate)
    if (pnl_ticks <= params.stop_loss_ticks) {
        decision.should_exit = true;
        decision.reason = ExitReason::STOP_LOSS;
        decision.confidence = 1.0;
      
        return decision;
    }
    
    // 2. VOLATILITY SPIKE (protect capital)
    // Use a stricter rule: require multiple consecutive spikes and that the pnl is not near target
    if (current_micro.volatility_1m > state.entry_microstructure.volatility_1m * params.volatility_spike_threshold) {
        state.vol_spike_count++;
        // Require at least 3 consecutive spikes and that current PnL is less than 25% of profit target
        if (state.vol_spike_count >= 3 && pnl_ticks < params.profit_target_ticks * 0.25) {
            decision.should_exit = true;
            decision.reason = ExitReason::VOLATILITY_SPIKE;
            // Confidence scales with how bad the relative spike is
            double rel_spike = current_micro.volatility_1m / std::max(1e-12, state.entry_microstructure.volatility_1m);
            decision.confidence = std::min(1.0, 0.5 + (rel_spike - params.volatility_spike_threshold) / (params.volatility_spike_threshold * 2.0));
            
            return decision;
        }
    } else {
        state.vol_spike_count = 0; // reset if no spike
    }
    
    // 3. ADVERSE SELECTION DETECTION
    double adverse_score = calculateAdverseSelectionScore(state, current_micro);
    if (adverse_score > 0.7 && pnl_ticks < 0) {
        decision.should_exit = true;
        decision.reason = ExitReason::ADVERSE_SELECTION;
        decision.confidence = adverse_score;
       
        return decision;
    }
    
    
    // 4. PROFIT TARGET
    // Use strategy-provided targets when available (prefer them over generic profit_target)
    if (!actual_targets.empty()) {
        for (const auto& t : actual_targets) {
            // check target according to entry direction
            if (state.entry_direction == core::Direction::BUY) {
                if (current_price >= t.price) {
                    decision.should_exit = true;
                    decision.reason = ExitReason::PROFIT_TARGET;
                    // confidence: combine target probability and current signal/conf
                    decision.confidence = std::min(1.0, 0.5 + 0.5 * t.probability * current_confidence);
                    return decision;
                }
            } else {
                if (current_price <= t.price) {
                    decision.should_exit = true;
                    decision.reason = ExitReason::PROFIT_TARGET;
                    decision.confidence = std::min(1.0, 0.5 + 0.5 * t.probability * current_confidence);
                    return decision;
                }
            }
        }
        // If no explicit signal target hit, still consider generic profit target as fallback
        if (pnl_ticks >= params.profit_target_ticks) {
            decision.should_exit = true;
            decision.reason = ExitReason::PROFIT_TARGET;
            decision.confidence = 0.9;
            return decision;
        }
    } else {
        if (pnl_ticks >= params.profit_target_ticks) {
            decision.should_exit = true;
            decision.reason = ExitReason::PROFIT_TARGET;
            decision.confidence = 0.9;
            return decision;
        }
    }
    
    
    // 5. TRAILING STOP
    if (state.max_favorable_excursion > params.trailing_stop_ticks) {
        double drawdown = state.max_favorable_excursion - pnl_ticks;
        if (drawdown > params.trailing_stop_ticks) {
            decision.should_exit = true;
            decision.reason = ExitReason::TRAILING_STOP;
            decision.confidence = 0.85;
           
            return decision;
        }
    }
    
    // 6. SIGNAL REVERSAL
    bool signal_reversed = false;
    if (state.entry_direction == core::Direction::BUY) {
        signal_reversed = (current_signal_direction == core::Direction::SELL && 
                          current_confidence > params.signal_reversal_threshold);
    } else {
        signal_reversed = (current_signal_direction == core::Direction::BUY && 
                          current_confidence > params.signal_reversal_threshold);
    }
    
    if (signal_reversed && state.momentum_score < 0) {
        decision.should_exit = true;
        decision.reason = ExitReason::SIGNAL_REVERSAL;
        decision.confidence = current_confidence;
   
        return decision;
    }
    
    // 7. SPREAD WIDENING / LIQUIDITY CRISIS
    double spread_ratio = current_micro.spread_ema / state.entry_microstructure.spread_ema;
    if (spread_ratio > params.spread_emergency_multiplier) {
        decision.should_exit = true;
        decision.reason = ExitReason::LIQUIDITY_CRISIS;
        decision.confidence = 0.7;
   
        return decision;
    }
    
    // 8. TIME LIMIT
    // TTL-aware behavior: if a signal TTL is provided, and we're approaching expiration,
    // prefer to exit toward targets (or tighten trailing/time limits).
    if (actual_ttl != core::Duration::zero()) {
        auto now_ts = core::now();
        auto expiry = actual_generation_time + actual_ttl;
        // compute remaining milliseconds
        auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(expiry - now_ts).count();
        auto ttl_ms = std::chrono::duration_cast<std::chrono::milliseconds>(actual_ttl).count();
        if (ttl_ms > 0 && rem_ms <= static_cast<long long>(std::max<int64_t>(100, ttl_ms / 5))) {
            // less than 20% of TTL remaining (or <100ms): increase exit eagerness
            // If close to expiry and PnL is positive, exit to secure profit
            if (pnl_ticks > 0.0) {
                decision.should_exit = true;
                decision.reason = ExitReason::TIME_LIMIT;
                decision.confidence = std::min(1.0, 0.6 + (std::min(1.0, pnl_ticks / std::max(1.0, params.profit_target_ticks))));
                return decision;
            }
            // If we're close to target and ttl is expiring, consider exit when within 50% of profit target
            if (pnl_ticks >= params.profit_target_ticks * 0.5) {
                decision.should_exit = true;
                decision.reason = ExitReason::TIME_LIMIT;
                decision.confidence = 0.7;
                return decision;
            }
        }
    }

    if (seconds_held >= params.max_holding_time) {
        decision.should_exit = true;
        decision.reason = ExitReason::TIME_LIMIT;
        decision.confidence = 0.6;

        return decision;
    }
    
    return decision;
}

double AdaptiveExitStrategy::calculateAdverseSelectionScore(
    const TradeState& state,
    const MarketMicrostructure& current_micro) const {
    
    double score = 0.0;
    
    // 1. Immediate adverse move after entry
    if (state.max_adverse_excursion < -2 && state.max_favorable_excursion < 1) {
        score += 0.3;
    }
    
    // 2. Orderbook imbalance against position
    if (state.entry_direction == core::Direction::BUY && current_micro.orderbook_imbalance < -0.3) {
        score += 0.2;
    } else if (state.entry_direction == core::Direction::SELL && current_micro.orderbook_imbalance > 0.3) {
        score += 0.2;
    }
    
    // 3. Spread widened immediately after entry
    double spread_change = (current_micro.spread_ema - state.entry_microstructure.spread_ema) 
                           / state.entry_microstructure.spread_ema;
    if (spread_change > 0.5) {
        score += 0.2;
    }
    
    // 4. Negative momentum
    if (state.momentum_score < -0.5) {
        score += 0.3;
    }
    
    return std::min(1.0, score);
}

bool AdaptiveExitStrategy::shouldEnterPosition(
    const std::string& symbol,
    const MarketMicrostructure& micro,
    double confidence) const {
    
    // Get current regime and parameters
    std::lock_guard<std::mutex> lk(historyMutex_);
    auto it = marketHistory_.find(symbol);
    if (it == marketHistory_.end()) {
        LOG_INFO("Rejecting entry for {}: No market history available", symbol);
        //  // Debug: log exact key and map contents
        return false;  // No history, can't assess
    }
    
    auto regime = detectMarketRegime(micro, it->second);
    auto params = calculateDynamicParameters(symbol, micro, regime);
    
    // Entry filters
    
    // 1. Minimum expected edge
    // Ensure we compare like-for-like: params.expected_edge is in bps (see calculateDynamicParameters),
    // so convert current spread to bps before comparing.
    double spread_bps = 0.0;
    if (micro.mid_price > 0.0) {
        spread_bps = micro.spread_ema / micro.mid_price * 10000.0; // basis points
    } else {
        // Fallback: treat spread as price and scale by 10000 for visibility
        spread_bps = micro.spread_ema * 10000.0;
    }
    // if (params.expected_edge < spread_bps * 1.2) {
    //     LOG_INFO("Rejecting entry for {}: Expected edge too small ({:.4f} bps < {:.4f} bps (=1.2*spread))",
    //               symbol, params.expected_edge, spread_bps * 1.2);
    //     // LOG_INFO("Rejecting!! Details for {}: expected_edge(bps)={:.6f}, spread_price={} mid={} spread_bps={:.6f}",
    //     //           symbol, params.expected_edge, micro.spread_ema, micro.mid_price, spread_bps);
    //     return false;  // Edge too small vs spread cost
    // }
    //debug with reject reason
    // 2. Kelly criterion check
    if (params.kelly_fraction < 0.01) {
        LOG_INFO("Rejecting entry for {}: Kelly fraction too low ({} < 0.01)",
                  symbol, params.kelly_fraction);
        return false;  // Position size would be too small
    }
    confidence += 0.5;
    switch (regime) {
        case MarketRegimeType::STRESS:
            // In stress regime we force exit / disable new entries
            LOG_INFO("Rejecting entry for {}: Stress regime - disabling new entries", symbol);
            return false;

        case MarketRegimeType::HIGH_VOLATILITY:
            // Allow entry but require higher confidence and prefer reduced exposure (checked elsewhere)
            if (confidence < 0.76) {
                LOG_INFO("Rejecting entry for {}: Low confidence in high volatility ({} < 0.90)",
                          symbol, confidence);
                return false;
            }
            // Tight stop logic is applied at exit; here allow entry if confident
            break;

        case MarketRegimeType::LOW_LIQUIDITY:
            // Avoid entering in low liquidity due to slippage risk
            LOG_INFO("Rejecting entry for {}: Low liquidity - avoid entering due to slippage (spread_ema={} tick_size={})",
                     symbol, micro.spread_ema, micro.tick_size);
            return false;

        case MarketRegimeType::TRENDING:
            // Allow entries in trending regime but require decent confidence; longer holding allowed (handled in params)
            if (confidence < 0.65) {
                LOG_INFO("Rejecting entry for {}: Low confidence in trending regime ({} < 0.75)",
                         symbol, confidence);
                return false;
            }
            break;

        case MarketRegimeType::MEAN_REVERTING:
            // Use short-term countertrend exits; require slightly higher confidence
            if (confidence < 0.70) {
                LOG_INFO("Rejecting entry for {}: Low confidence in mean reverting regime ({} < 0.80)",
                         symbol, confidence);
                return false;
            }
            break;

        case MarketRegimeType::QUIET:
            // Quiet markets: skip trades or widen thresholds. Be conservative on entry.
            if (confidence < 0.70) {
                LOG_INFO("Rejecting entry for {}: Quiet regime - skipping low confidence trades ({} < 0.70)",
                         symbol, confidence);
                return false;
            }
            break;

        case MarketRegimeType::NORMAL:
        default:
            // Default operation: require a baseline confidence
            if (confidence < 0.6) {
                LOG_INFO("Rejecting entry for {}: Low confidence in default/normal regime ({} < 0.6)",
                         symbol, confidence);
                return false;
            }
    }
    
    // 4. Spread quality check
    const auto& spread_history = it->second.spread_samples;
    if (spread_history.size() >= 10) {
        double spread_percentile_80 = calculatePercentile(spread_history, 0.80);
        if (micro.spread_ema > spread_percentile_80) {
            LOG_TRACE("Rejecting entry for {}: Current spread worse than 80th percentile ({} > {})",
                      symbol, micro.spread_ema, spread_percentile_80);
            return false;  // Current spread worse than 80% of recent
        }
    }
    
    return true;
}

// Statistical helper functions
double AdaptiveExitStrategy::calculatePercentile(const std::deque<double>& data, double percentile) const {
    if (data.empty()) return 0.0;
    
    std::vector<double> sorted(data.begin(), data.end());
    std::sort(sorted.begin(), sorted.end());
    
    size_t index = static_cast<size_t>(percentile * (sorted.size() - 1));
    return sorted[index];
}

double AdaptiveExitStrategy::calculateStandardDeviation(const std::deque<double>& data) const {
    if (data.size() < 2) return 0.0;
    
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    double sq_sum = 0.0;
    
    for (double val : data) {
        sq_sum += (val - mean) * (val - mean);
    }
    
    return std::sqrt(sq_sum / (data.size() - 1));
}

double AdaptiveExitStrategy::computeSignalPnlBps(double entry_price, double exit_price, core::Direction direction) {
    if (entry_price <= 0.0) return 0.0;
    double pnl_price = 0.0;
    if (direction == core::Direction::BUY) {
        pnl_price = exit_price - entry_price;
    } else if (direction == core::Direction::SELL) {
        pnl_price = entry_price - exit_price;
    } else {
        // For NEUTRAL or unknown, treat as zero
        return 0.0;
    }
    double pnl_bps = (pnl_price / entry_price) * 10000.0;
    return pnl_bps;
}


double AdaptiveExitStrategy::calculateAutoCorrelation(const std::deque<double>& data, int lag) const {
    if (data.size() < lag + 2) return 0.0;
    
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    
    double numerator = 0.0;
    double denominator = 0.0;
    
    for (size_t i = lag; i < data.size(); ++i) {
        numerator += (data[i] - mean) * (data[i - lag] - mean);
    }
    
    for (double val : data) {
        denominator += (val - mean) * (val - mean);
    }
    
    if (denominator == 0) return 0.0;
    return numerator / denominator;
}

void AdaptiveExitStrategy::closePosition(const std::string& symbol, 
                                        ExitReason reason, 
                                        double final_pnl) {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    auto it = activeTrades_.find(symbol);
    if (it != activeTrades_.end()) {
        it->second.is_active = false;
        // Calculate total PnL including recent tracked PnLs and the final realized PnL
    // double total_pnl = calculateTotalPnLUnlocked(symbol, final_pnl);
        LOG_INFO("AdaptiveExitStrategy: closed position on {} due to {} with final PnL: {:.4f} ticks )",
                 symbol, exitReasonToString(reason), final_pnl);
        // NOTE: do not record tick-level or expected PnL here. Historically the
        // code pushed the "final_pnl" value (which often contains expected_pnl
        // in bps coming from evaluateExit) into historicalPnLs_. That caused
        // expected_edge estimates to collapse (tick updates) and resulted in
        // permanent rejects. Only realized trade-level PnL (in bps) should be
        // recorded via recordRealizedPnlBps() when an execution/fill confirms
        // the closed trade's realized PnL.
    }
    else {
        LOG_WARN("AdaptiveExitStrategy::closePosition called for {} but no active position found - ignoring", symbol);
    }
}

void AdaptiveExitStrategy::recordRealizedPnlBps(const std::string& symbol, double realized_pnl_bps) {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    historicalPnLs_[symbol].push_back(realized_pnl_bps);
    // cap history
    if (historicalPnLs_[symbol].size() > 100) {
        historicalPnLs_[symbol].pop_front();
    }
    LOG_INFO("AdaptiveExitStrategy: recorded realized PnL for {} = {:.4f} bps (history size={})",
             symbol, realized_pnl_bps, historicalPnLs_[symbol].size());
}

double AdaptiveExitStrategy::calculateTotalPnL(const std::string& symbol, double final_pnl) const {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    return calculateTotalPnLUnlocked(symbol, final_pnl);
}

double AdaptiveExitStrategy::calculateTotalPnLUnlocked(const std::string& symbol, double final_pnl) const {
    double total = final_pnl;
    auto it = activeTrades_.find(symbol);
    if (it != activeTrades_.end()) {
        const auto& state = it->second;
        for (double p : state.recent_pnls) {
            total += p;
        }
    }
    return total;
}

void AdaptiveExitStrategy::markPendingExit(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    auto it = activeTrades_.find(symbol);
    if (it != activeTrades_.end()) {
        it->second.pending_exit = true;
        LOG_INFO("AdaptiveExitStrategy: marked pending exit for {}", symbol);
    }
}

void AdaptiveExitStrategy::updateLatestSignal(const std::string& symbol, const core::TradingSignal& signal) {
    std::lock_guard<std::mutex> lk(latestSignalMutex_);
    latestSignalBySymbol_[symbol] = signal;
}

void AdaptiveExitStrategy::clearLatestSignal(const std::string& symbol) {
    std::lock_guard<std::mutex> lk(latestSignalMutex_);
    latestSignalBySymbol_.erase(symbol);
}

const char* AdaptiveExitStrategy::exitReasonToString(ExitReason reason) {
    switch (reason) {
        case ExitReason::PROFIT_TARGET: return "PROFIT_TARGET";
        case ExitReason::TRAILING_STOP: return "TRAILING_STOP";
        case ExitReason::STOP_LOSS: return "STOP_LOSS";
        case ExitReason::SIGNAL_REVERSAL: return "SIGNAL_REVERSAL";
        case ExitReason::TIME_LIMIT: return "TIME_LIMIT";
        case ExitReason::SPREAD_WIDENING: return "SPREAD_WIDENING";
        case ExitReason::LIQUIDITY_CRISIS: return "LIQUIDITY_CRISIS";
        case ExitReason::VOLATILITY_SPIKE: return "VOLATILITY_SPIKE";
        case ExitReason::ADVERSE_SELECTION: return "ADVERSE_SELECTION";
        default: return "UNKNOWN";
    }
}
// hasActivePosition
bool AdaptiveExitStrategy::hasActivePosition(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    auto it = activeTrades_.find(symbol);
    return (it != activeTrades_.end() && it->second.is_active);
}
// getTradeState
AdaptiveExitStrategy::TradeState AdaptiveExitStrategy::getTradeState(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(tradesMutex_);
    auto it = activeTrades_.find(symbol);
    if (it != activeTrades_.end()) {
        return it->second;
    }
    return TradeState{};
}

AdaptiveExitStrategy::DynamicParameters AdaptiveExitStrategy::getCurrentParameters(const std::string& symbol) const {
    std::lock_guard<std::mutex> regime_lock(regimeMutex_);
    auto regime_it = regimeCache_.find(symbol);
    MarketRegimeType regime = (regime_it != regimeCache_.end()) ? regime_it->second : MarketRegimeType::NORMAL;
    
    std::lock_guard<std::mutex> history_lock(historyMutex_);
    auto history_it = marketHistory_.find(symbol);
    if (history_it != marketHistory_.end()) {
        // Need current microstructure - for now return default
        MarketMicrostructure micro;
        return calculateDynamicParameters(symbol, micro, regime);
    }
    return DynamicParameters{};
}

MarketRegimeType AdaptiveExitStrategy::getCurrentMarketRegime(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(regimeMutex_);
    auto it = regimeCache_.find(symbol);
    return (it != regimeCache_.end()) ? it->second : MarketRegimeType::NORMAL;
}



} // namespace strategy
} // namespace trading
