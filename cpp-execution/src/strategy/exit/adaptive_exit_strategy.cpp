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

AdaptiveExitStrategy::AdaptiveExitStrategy(double prediction_window_seconds) 
    : predictionWindow_(prediction_window_seconds) {}

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

MarketRegimeType AdaptiveExitStrategy::detectMarketRegime(
    const MarketMicrostructure& micro,
    const MarketHistory& history) const {
    // Delegate regime detection to OrderbookRegimeDetector
    trading::strategy::core_analysis::OrderbookRegimeDetector det;
    return det.detect(micro, history.volatility_samples, history.spread_samples, history.price_returns);
}

// AdaptiveExitStrategy::DynamicParameters AdaptiveExitStrategy::calculateDynamicParameters(
//     const std::string& symbol,
//     const MarketMicrostructure& current_micro,
//     const MarketRegimeType& regime) const {
    
//     DynamicParameters params;
    
//     // ===== CÔNG THỨC TÍNH TOÁN ĐỘNG =====
    
//     // 1. Base parameters từ volatility và spread
//     // Protect against invalid tick_size (divide by zero)
//     double tick = current_micro.tick_size;
//     if (tick <= 0.0) {
//         // Fallback: only warn once per symbol and use a tiny positive tick to avoid div-by-zero
//         {
//             std::lock_guard<std::mutex> lk(g_tickWarnMutex);
//             if (g_tickSizeWarnedSymbols.find(symbol) == g_tickSizeWarnedSymbols.end()) {
//                 LOG_WARN("AdaptiveExitStrategy::calculateDynamicParameters: tick_size <= 0 ({}). Using fallback tiny tick for symbol={}", tick, symbol);
//                 g_tickSizeWarnedSymbols.insert(symbol);
//             }
//         }
//         tick = 1e-8;
//     }

//     double vol_in_ticks = current_micro.volatility_1m / tick;
//     double spread_in_ticks = current_micro.spread_ema / tick;
    
//     // 2. Profit Target (dựa trên expected move trong prediction window)
//     // Formula: PT = α * σ * √t + β * spread
//     // Với α = confidence factor (1.5-2.5), β = spread cost multiplier (1.5-3)
//     double alpha = 2.0;  // Base confidence factor
//     double beta = 2.5;   // Spread cost multiplier
//     double gamma = 0.4;   // trailing fraction
//     double decay_factor = 1.0;  // holding time multiplier
    
//         // === Adjust per regime ===
//     switch (regime) {
//         case MarketRegimeType::STRESS:
//             alpha = 1.2;   // smaller PT, avoid chasing moves
//             beta  = 5.0;   // spreads explode, cost up
//             gamma = 0.2;   // very tight trailing (protect PnL)
//             decay_factor = 0.4; // close fast
//             break;

//         case MarketRegimeType::HIGH_VOLATILITY:
//             alpha = 3.0;   // bigger expected move
//             beta  = 2.5;
//             gamma = 0.35;  // let run but still tight
//             decay_factor = 0.8;  // shorter hold (fast mean reverts)
//             break;

//         case MarketRegimeType::LOW_LIQUIDITY:
//             alpha = 1.8;
//             beta  = 4.0;   // compensate for wide spreads
//             gamma = 0.3;
//             decay_factor = 0.7;  // exit earlier to avoid slippage
//             break;

//         case MarketRegimeType::TRENDING:
//             alpha = 2.5;   // expect larger moves
//             beta  = 2.0;   // spread less important
//             gamma = 0.55;  // let profits run
//             decay_factor = 1.5;  // hold longer
//             break;

//         case MarketRegimeType::MEAN_REVERTING:
//             alpha = 1.4;   // smaller PT
//             beta  = 3.0;
//             gamma = 0.25;  // take profit quickly
//             decay_factor = 0.5;
//             break;

//         case MarketRegimeType::QUIET:
//             alpha = 1.2;   // tiny move expected
//             beta  = 2.0;
//             gamma = 0.3;   // small trailing
//             decay_factor = 1.0;  // normal hold
//             break;

//         case MarketRegimeType::NORMAL:
//         default:
//             alpha = 2.0;
//             beta  = 2.5;
//             gamma = 0.4;
//             decay_factor = 1.0;
//             break;
//     }

//     // Time adjustment factor: √(prediction_window / 60)
//     double time_factor = std::sqrt(predictionWindow_ / 60.0);
    
//     params.profit_target_ticks = alpha * vol_in_ticks * time_factor + beta * spread_in_ticks;
//     params.profit_target_ticks = std::min(params.profit_target_ticks, 50.0);

//     // 3. Stop Loss (based on Value at Risk - VaR)
//     // Formula: SL = -VaR(confidence_level) = -z_score * σ * √t
//     double z_score_95 = 1.645;  // 95% confidence
//     params.stop_loss_ticks = -z_score_95 * vol_in_ticks * time_factor - spread_in_ticks;
    
//     // Regime adjustments for stop loss
//     if (regime == MarketRegimeType::HIGH_VOLATILITY) 
//         params.stop_loss_ticks *= 1.5;
//     else if (regime == MarketRegimeType::LOW_LIQUIDITY)
//         params.stop_loss_ticks *= 1.3;
//     else if (regime == MarketRegimeType::STRESS)
//         params.stop_loss_ticks *= 2.0; // maximum protection

    
//     // 4. Trailing Stop (percentage of favorable move)
//     // Formula: TS = γ * PT, với γ ∈ [0.3, 0.6]
//     // double gamma = 0.4;  // Base trailing factor
//     // if (regime == MarketRegimeType::TRENDING) {
//     //     gamma = 0.5;  // Let winners run more
//     // } else if (regime == MarketRegimeType::MEAN_REVERTING) {
//     //     gamma = 0.3;  // Tighter trailing
//     // }
//     params.trailing_stop_ticks = gamma * params.profit_target_ticks;
//     params.stop_loss_ticks = std::max(params.stop_loss_ticks, -50.0);

//     // 5. Holding Time (based on autocorrelation decay and mean reversion speed)
//     // Formula: T_max = prediction_window * decay_factor
    
//     if (regime == MarketRegimeType::TRENDING) {
//         decay_factor = 1.5;  // Hold longer in trends
//     } else if (regime == MarketRegimeType::MEAN_REVERTING) {
//         decay_factor = 0.5;  // Quick exits in mean reversion
//     }
    
//     params.max_holding_time = predictionWindow_ * decay_factor;
//     // Base min holding time on tick-to-spread ratio but ensure we don't divide by zero
//     // Recommendation B: use a conservative static floor of 1.0s so strategies
//     // with short prediction windows (e.g., 1.5s) can still respect a minimum
//     // but without an overly large 10s floor.
//     double static_floor = 1.0; // seconds
//     double ratio_based_min = static_floor;
//     if (vol_in_ticks > 1e-12) {
//         ratio_based_min = std::max(static_floor, spread_in_ticks / vol_in_ticks * 2.0);
//     }
//     double prediction_half = predictionWindow_ / 2.0;
//     params.min_holding_time = std::max(ratio_based_min, prediction_half);
    
//     // 6. Signal Reversal Threshold (based on prediction confidence decay)
//     // Higher threshold when volatility is high
//     // params.signal_reversal_threshold = 0.2 + 0.3 * (current_micro.volatility_1m / 0.001);
//     // params.signal_reversal_threshold = std::min(0.7, params.signal_reversal_threshold);
//     // In quiet regimes, be sensitive; in stress, be conservative
//     double base_rev = 0.2 + 0.3 * (current_micro.volatility_1m / 0.001);
//     base_rev = std::min(0.7, base_rev);

//     if (regime == MarketRegimeType::STRESS)
//         base_rev = 0.8; // ignore small reversals
//     else if (regime == MarketRegimeType::QUIET)
//         base_rev = 0.15; // react to small shifts

//     params.signal_reversal_threshold = base_rev;
//     // 7. Emergency multipliers
//     params.spread_emergency_multiplier = 3.0 + spread_in_ticks / 10.0;  // Dynamic based on normal spread
//     // Use a cleaner multiplier for spike detection: absolute multiplier of recent volatility
//     // Previously this was vol_in_ticks * 3.0 which can be too small for very low-volatility assets
//     // Set to a minimum multiplier and allow scaling with vol_in_ticks for very low vols.
//     double base_vol_spike_multiplier = 4.0; // raise base multiplier to reduce false positives
//     params.volatility_spike_threshold = std::max(base_vol_spike_multiplier, vol_in_ticks * 3.0);
    
//     // 8. Calculate expected edge and Kelly fraction
//     // Sử dụng historical PnL nếu có
//     auto it = historicalPnLs_.find(symbol);
//     if (it != historicalPnLs_.end() && it->second.size() >= 20) {
//         const auto& pnls = it->second;
        
//         // Win rate
//         double wins = std::count_if(pnls.begin(), pnls.end(), 
//                                    [](double pnl) { return pnl > 0; });
//         double win_rate = wins / pnls.size();
        
//         // Average win/loss
//         double avg_win = 0, avg_loss = 0;
//         int win_count = 0, loss_count = 0;
//         for (double pnl : pnls) {
//             if (pnl > 0) {
//                 avg_win += pnl;
//                 win_count++;
//             } else if (pnl < 0) {
//                 avg_loss += std::abs(pnl);
//                 loss_count++;
//             }
//         }
        
//         if (win_count > 0) avg_win /= win_count;
//         if (loss_count > 0) avg_loss /= loss_count;
        
//         // Kelly Criterion: f = (p * b - q) / b
//         // Với p = win_rate, q = 1-p, b = avg_win/avg_loss
//         if (avg_loss > 0) {
//             double b = avg_win / avg_loss;
//             double kelly = (win_rate * b - (1 - win_rate)) / b;
//             params.kelly_fraction = std::max(0.0, std::min(0.25, kelly)); // Cap at 25%
//         }
        
//         params.expected_edge = win_rate * avg_win - (1 - win_rate) * avg_loss;
        
//         // Sharpe estimate
//         double pnl_std = calculateStandardDeviation(pnls);
//         if (pnl_std > 0) {
//             params.sharpe_estimate = params.expected_edge / pnl_std * std::sqrt(252);  // Annualized
//         }
//     } else {
//         // Conservative defaults khi chưa có history
//         params.kelly_fraction = 0.02;
//         params.expected_edge = spread_in_ticks * 0.1;  // Assume 10% edge over spread
//         params.sharpe_estimate = 0.5;
//     }
    
//     return params;
// }

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
    double T_min = std::max(1.0, std::max( (spr_ticks / vol_ticks) * 2.0, pw_sec * 0.5 ));

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

        p.expected_edge = wr*avgW - (1-wr)*avgL;

        double stdp = calculateStandardDeviation(pnls);
        p.sharpe_estimate = (stdp>0) ? (p.expected_edge/stdp * std::sqrt(252.0)) : 0.5;

    } else {
        p.kelly_fraction = 0.02;
        p.expected_edge = spr_ticks * 0.1;
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
    
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    auto& state = activeTrades_[symbol];
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
        state.entry_regime = detectMarketRegime(microstructure, it->second);
    } else {
        state.entry_regime = MarketRegimeType::NORMAL;
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
    double current_confidence) {
    
    std::lock_guard<std::mutex> lock(tradesMutex_);
    
    auto it = activeTrades_.find(symbol);
    if (it == activeTrades_.end() || !it->second.is_active) {
        return ExitDecision{false};
    }
    
    auto& state = it->second;
    
    // Get current dynamic parameters
    auto regime = detectMarketRegime(current_micro, marketHistory_[symbol]);
    auto params = calculateDynamicParameters(symbol, current_micro, regime);
    
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
        decision.expected_pnl = pnl_ticks * current_micro.tick_size / state.entry_price * 10000; // to bps
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
        // Log current and aggregated PnL at exit trigger
        {
            double aggregated = calculateTotalPnLUnlocked(symbol, pnl_ticks);
            LOG_INFO("AdaptiveExitStrategy: exit triggered on {} reason={} current PnL: {:.4f} ticks (aggregated: {:.4f} ticks)",
                     symbol, exitReasonToString(decision.reason), pnl_ticks, aggregated);
        }
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
            double aggregated = calculateTotalPnLUnlocked(symbol, pnl_ticks);
            LOG_INFO("AdaptiveExitStrategy: exit triggered on {} reason={} current PnL: {:.4f} ticks (aggregated: {:.4f} ticks)",
                     symbol, exitReasonToString(decision.reason), pnl_ticks, aggregated);
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
        {
            double aggregated = calculateTotalPnLUnlocked(symbol, pnl_ticks);
            LOG_INFO("AdaptiveExitStrategy: exit triggered on {} reason={} current PnL: {:.4f} ticks (aggregated: {:.4f} ticks)",
                     symbol, exitReasonToString(decision.reason), pnl_ticks, aggregated);
        }
        return decision;
    }
    
    
    // 4. PROFIT TARGET
    if (pnl_ticks >= params.profit_target_ticks) {
        decision.should_exit = true;
        decision.reason = ExitReason::PROFIT_TARGET;
        decision.confidence = 0.9;
        {
            double aggregated = calculateTotalPnLUnlocked(symbol, pnl_ticks);
            LOG_INFO("AdaptiveExitStrategy: exit triggered on {} reason={} current PnL: {:.4f} ticks (aggregated: {:.4f} ticks)",
                     symbol, exitReasonToString(decision.reason), pnl_ticks, aggregated);
        }
        return decision;
    }
    
    
    // 5. TRAILING STOP
    if (state.max_favorable_excursion > params.trailing_stop_ticks) {
        double drawdown = state.max_favorable_excursion - pnl_ticks;
        if (drawdown > params.trailing_stop_ticks) {
            decision.should_exit = true;
            decision.reason = ExitReason::TRAILING_STOP;
            decision.confidence = 0.85;
            double aggregated = calculateTotalPnLUnlocked(symbol, pnl_ticks);
            LOG_INFO("AdaptiveExitStrategy: exit triggered on {} reason={} current PnL: {:.4f} ticks (aggregated: {:.4f} ticks)",
                     symbol, exitReasonToString(decision.reason), pnl_ticks, aggregated);
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
    double aggregated = calculateTotalPnLUnlocked(symbol, pnl_ticks);
        LOG_INFO("AdaptiveExitStrategy: exit triggered on {} reason={} current PnL: {:.4f} ticks (aggregated: {:.4f} ticks)",
                 symbol, exitReasonToString(decision.reason), pnl_ticks, aggregated);
        return decision;
    }
    
    // 7. SPREAD WIDENING / LIQUIDITY CRISIS
    double spread_ratio = current_micro.spread_ema / state.entry_microstructure.spread_ema;
    if (spread_ratio > params.spread_emergency_multiplier) {
        decision.should_exit = true;
        decision.reason = ExitReason::LIQUIDITY_CRISIS;
        decision.confidence = 0.7;
    double aggregated = calculateTotalPnLUnlocked(symbol, pnl_ticks);
        LOG_INFO("AdaptiveExitStrategy: exit triggered on {} reason={} current PnL: {:.4f} ticks (aggregated: {:.4f} ticks)",
                 symbol, exitReasonToString(decision.reason), pnl_ticks, aggregated);
        return decision;
    }
    
    // 8. TIME LIMIT
    if (seconds_held >= params.max_holding_time) {
        decision.should_exit = true;
        decision.reason = ExitReason::TIME_LIMIT;
        decision.confidence = 0.6;
    double aggregated = calculateTotalPnLUnlocked(symbol, pnl_ticks);
        LOG_INFO("AdaptiveExitStrategy: exit triggered on {} reason={} current PnL: {:.4f} ticks (aggregated: {:.4f} ticks)",
                 symbol, exitReasonToString(decision.reason), pnl_ticks, aggregated);
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
    if (params.expected_edge < micro.spread_ema * 1.2) {
        LOG_INFO("Rejecting entry for {}: Expected edge too small ({:.4f} < {:.4f})",
                  symbol, params.expected_edge, micro.spread_ema * 1.2);
        return false;  // Edge too small vs spread cost
    }
    //debug with reject reason
    // 2. Kelly criterion check
    if (params.kelly_fraction < 0.01) {
        LOG_INFO("Rejecting entry for {}: Kelly fraction too low ({} < 0.01)",
                  symbol, params.kelly_fraction);
        return false;  // Position size would be too small
    }
     
    switch (regime) {
        case MarketRegimeType::STRESS:
            // In stress regime we force exit / disable new entries
            LOG_INFO("Rejecting entry for {}: Stress regime - disabling new entries", symbol);
            return false;

        case MarketRegimeType::HIGH_VOLATILITY:
            // Allow entry but require higher confidence and prefer reduced exposure (checked elsewhere)
            if (confidence < 0.9) {
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
            if (confidence < 0.75) {
                LOG_INFO("Rejecting entry for {}: Low confidence in trending regime ({} < 0.75)",
                         symbol, confidence);
                return false;
            }
            break;

        case MarketRegimeType::MEAN_REVERTING:
            // Use short-term countertrend exits; require slightly higher confidence
            if (confidence < 0.80) {
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
            if (confidence < 0.65) {
                LOG_INFO("Rejecting entry for {}: Low confidence in default/normal regime ({} < 0.65)",
                         symbol, confidence);
                return false;
            }
    }
    
    // 4. Spread quality check
    const auto& spread_history = it->second.spread_samples;
    if (spread_history.size() >= 10) {
        double spread_percentile_80 = calculatePercentile(spread_history, 0.80);
        if (micro.spread_ema > spread_percentile_80) {
            LOG_INFO("Rejecting entry for {}: Current spread worse than 80th percentile ({} > {})",
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
    double total_pnl = calculateTotalPnLUnlocked(symbol, final_pnl);
        LOG_INFO("AdaptiveExitStrategy: closed position on {} due to {} with final PnL: {:.4f} ticks (total PnL: {:.4f} ticks)",
                 symbol, exitReasonToString(reason), final_pnl, total_pnl);
        // Update historical PnL for future Kelly calculations
        historicalPnLs_[symbol].push_back(final_pnl);
        if (historicalPnLs_[symbol].size() > 100) {
            historicalPnLs_[symbol].pop_front();
        }
    }
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

} // namespace strategy
} // namespace trading
