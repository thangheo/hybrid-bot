#include "strategy/exit/adaptive_exit_strategy.hpp" 
#include <numeric>
#include <algorithm>
#include <cmath>
#include <common/logger.hpp>
namespace trading {
namespace strategy {

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

     // Debug: log exact key and map contents
    LOG_INFO("updateMarketData (this={}): Looking for key '{}' in marketHistory_ (size={})", 
             static_cast<const void*>(this), symbol, marketHistory_.size());

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

    // Diagnostic: log first sample arrival and current history sizes
    if (was_empty) {
        try {
            LOG_INFO("AdaptiveExitStrategy: first market data for {} (vol={}, spread={}, return={})", symbol, volatility_1m, spread_ema, price_return);
        } catch (...) {}
    } else {
        try {
            LOG_DEBUG("AdaptiveExitStrategy: updated marketHistory[{}] sizes vol={} spread={} returns={}",
                      symbol,
                      history.volatility_samples.size(),
                      history.spread_samples.size(),
                      history.price_returns.size());
        } catch (...) {}
    }
}

AdaptiveExitStrategy::MarketRegime AdaptiveExitStrategy::detectMarketRegime(
    const MarketMicrostructure& micro,
    const MarketHistory& history) const {
    
    if (history.volatility_samples.size() < 10) {
        return MarketRegime::NORMAL;
    }
    
    // Calculate regime indicators
    double vol_percentile_75 = calculatePercentile(history.volatility_samples, 0.75);
    double vol_percentile_25 = calculatePercentile(history.volatility_samples, 0.25);
    double spread_percentile_90 = calculatePercentile(history.spread_samples, 0.90);
    
    double returns_autocorr = calculateAutoCorrelation(history.price_returns, 1);
    double vol_ratio = micro.volatility_1m / vol_percentile_75;
    double spread_ratio = micro.spread_ema / spread_percentile_90;
    
    // Regime detection logic
    if (micro.spread_ema > spread_percentile_90 * 2.0) {
        return MarketRegime::LOW_LIQUIDITY;
    }
    
    if (micro.volatility_1m > vol_percentile_75 * 1.5) {
        return MarketRegime::HIGH_VOLATILITY;
    }
    
    // Trending: positive autocorrelation and directional orderbook imbalance
    if (returns_autocorr > 0.3 && std::abs(micro.orderbook_imbalance) > 0.3) {
        return MarketRegime::TRENDING;
    }
    
    // Mean reverting: negative autocorrelation
    if (returns_autocorr < -0.2) {
        return MarketRegime::MEAN_REVERTING;
    }
    
    return MarketRegime::NORMAL;
}

AdaptiveExitStrategy::DynamicParameters AdaptiveExitStrategy::calculateDynamicParameters(
    const std::string& symbol,
    const MarketMicrostructure& current_micro,
    const MarketRegime& regime) const {
    
    DynamicParameters params;
    
    // ===== CÔNG THỨC TÍNH TOÁN ĐỘNG =====
    
    // 1. Base parameters từ volatility và spread
    double vol_in_ticks = current_micro.volatility_1m / current_micro.tick_size;
    double spread_in_ticks = current_micro.spread_ema / current_micro.tick_size;
    
    // 2. Profit Target (dựa trên expected move trong prediction window)
    // Formula: PT = α * σ * √t + β * spread
    // Với α = confidence factor (1.5-2.5), β = spread cost multiplier (1.5-3)
    double alpha = 2.0;  // Base confidence factor
    double beta = 2.5;   // Spread cost multiplier
    
    // Điều chỉnh theo regime
    switch (regime) {
        case MarketRegime::TRENDING:
            alpha = 2.5;  // Expect larger moves
            beta = 2.0;   // Can tolerate tighter spread cost
            break;
        case MarketRegime::MEAN_REVERTING:
            alpha = 1.5;  // Smaller moves expected
            beta = 3.0;   // Need more buffer for spread
            break;
        case MarketRegime::HIGH_VOLATILITY:
            alpha = 3.0;  // Large moves but risky
            beta = 2.5;
            break;
        case MarketRegime::LOW_LIQUIDITY:
            alpha = 1.8;
            beta = 4.0;  // Wide spreads need compensation
            break;
        default:
            break;
    }
    
    // Time adjustment factor: √(prediction_window / 60)
    double time_factor = std::sqrt(predictionWindow_ / 60.0);
    
    params.profit_target_ticks = alpha * vol_in_ticks * time_factor + beta * spread_in_ticks;
    
    // 3. Stop Loss (based on Value at Risk - VaR)
    // Formula: SL = -VaR(confidence_level) = -z_score * σ * √t
    double z_score_95 = 1.645;  // 95% confidence
    params.stop_loss_ticks = -z_score_95 * vol_in_ticks * time_factor - spread_in_ticks;
    
    // Regime adjustments for stop loss
    if (regime == MarketRegime::HIGH_VOLATILITY) {
        params.stop_loss_ticks *= 1.5;  // Wider stop in volatile markets
    } else if (regime == MarketRegime::LOW_LIQUIDITY) {
        params.stop_loss_ticks *= 1.3;  // Account for slippage
    }
    
    // 4. Trailing Stop (percentage of favorable move)
    // Formula: TS = γ * PT, với γ ∈ [0.3, 0.6]
    double gamma = 0.4;  // Base trailing factor
    if (regime == MarketRegime::TRENDING) {
        gamma = 0.5;  // Let winners run more
    } else if (regime == MarketRegime::MEAN_REVERTING) {
        gamma = 0.3;  // Tighter trailing
    }
    params.trailing_stop_ticks = gamma * params.profit_target_ticks;
    
    // 5. Holding Time (based on autocorrelation decay and mean reversion speed)
    // Formula: T_max = prediction_window * decay_factor
    double decay_factor = 1.0;
    
    if (regime == MarketRegime::TRENDING) {
        decay_factor = 1.5;  // Hold longer in trends
    } else if (regime == MarketRegime::MEAN_REVERTING) {
        decay_factor = 0.5;  // Quick exits in mean reversion
    }
    
    params.max_holding_time = predictionWindow_ * decay_factor;
    params.min_holding_time = std::max(1.0, spread_in_ticks / vol_in_ticks * 2.0); // Based on tick-to-spread ratio
    
    // 6. Signal Reversal Threshold (based on prediction confidence decay)
    // Higher threshold when volatility is high
    params.signal_reversal_threshold = 0.2 + 0.3 * (current_micro.volatility_1m / 0.001);
    params.signal_reversal_threshold = std::min(0.7, params.signal_reversal_threshold);
    
    // 7. Emergency multipliers
    params.spread_emergency_multiplier = 3.0 + spread_in_ticks / 10.0;  // Dynamic based on normal spread
    params.volatility_spike_threshold = vol_in_ticks * 3.0;
    
    // 8. Calculate expected edge and Kelly fraction
    // Sử dụng historical PnL nếu có
    auto it = historicalPnLs_.find(symbol);
    if (it != historicalPnLs_.end() && it->second.size() >= 20) {
        const auto& pnls = it->second;
        
        // Win rate
        double wins = std::count_if(pnls.begin(), pnls.end(), 
                                   [](double pnl) { return pnl > 0; });
        double win_rate = wins / pnls.size();
        
        // Average win/loss
        double avg_win = 0, avg_loss = 0;
        int win_count = 0, loss_count = 0;
        for (double pnl : pnls) {
            if (pnl > 0) {
                avg_win += pnl;
                win_count++;
            } else if (pnl < 0) {
                avg_loss += std::abs(pnl);
                loss_count++;
            }
        }
        
        if (win_count > 0) avg_win /= win_count;
        if (loss_count > 0) avg_loss /= loss_count;
        
        // Kelly Criterion: f = (p * b - q) / b
        // Với p = win_rate, q = 1-p, b = avg_win/avg_loss
        if (avg_loss > 0) {
            double b = avg_win / avg_loss;
            double kelly = (win_rate * b - (1 - win_rate)) / b;
            params.kelly_fraction = std::max(0.0, std::min(0.25, kelly)); // Cap at 25%
        }
        
        params.expected_edge = win_rate * avg_win - (1 - win_rate) * avg_loss;
        
        // Sharpe estimate
        double pnl_std = calculateStandardDeviation(pnls);
        if (pnl_std > 0) {
            params.sharpe_estimate = params.expected_edge / pnl_std * std::sqrt(252);  // Annualized
        }
    } else {
        // Conservative defaults khi chưa có history
        params.kelly_fraction = 0.02;
        params.expected_edge = spread_in_ticks * 0.1;  // Assume 10% edge over spread
        params.sharpe_estimate = 0.5;
    }
    
    return params;
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
    // LOG_INFO("AdaptiveExitStrategy: started tracking position on {} at price {:.6f} dir={} conf={:.3f}",
    //          symbol, entry_price,
    //          (direction == core::Direction::BUY ? "BUY" : "SELL"),
    //          confidence);
    // Detect entry regime
    auto it = marketHistory_.find(symbol);
    if (it != marketHistory_.end()) {
        state.entry_regime = detectMarketRegime(microstructure, it->second);
    } else {
        state.entry_regime = MarketRegime::NORMAL;
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
    double pnl_ticks;
    if (state.entry_direction == core::Direction::BUY) {
        pnl_ticks = (current_price - state.entry_price) / current_micro.tick_size;
    } else {
        pnl_ticks = (state.entry_price - current_price) / current_micro.tick_size;
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
    decision.expected_pnl = pnl_ticks * current_micro.tick_size / state.entry_price * 10000; // to bps
    
    // ===== EXIT CONDITIONS (Priority Order) =====
    
    // 1. STOP LOSS (immediate)
    if (pnl_ticks <= params.stop_loss_ticks) {
        decision.should_exit = true;
        decision.reason = ExitReason::STOP_LOSS;
        decision.confidence = 1.0;
        return decision;
    }
    
    // 2. VOLATILITY SPIKE (protect capital)
    if (current_micro.volatility_1m > state.entry_microstructure.volatility_1m * 2.5) {
        if (pnl_ticks < params.profit_target_ticks * 0.5) {
            decision.should_exit = true;
            decision.reason = ExitReason::VOLATILITY_SPIKE;
            decision.confidence = 0.8;
            return decision;
        }
    }
    
    // 3. ADVERSE SELECTION DETECTION
    double adverse_score = calculateAdverseSelectionScore(state, current_micro);
    if (adverse_score > 0.7 && pnl_ticks < 0) {
        decision.should_exit = true;
        decision.reason = ExitReason::ADVERSE_SELECTION;
        decision.confidence = adverse_score;
        return decision;
    }
    
    // Skip remaining checks if minimum hold time not met
    if (seconds_held < params.min_holding_time) {
        return decision;
    }
    
    // 4. PROFIT TARGET
    if (pnl_ticks >= params.profit_target_ticks) {
        decision.should_exit = true;
        decision.reason = ExitReason::PROFIT_TARGET;
        decision.confidence = 0.9;
        return decision;
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
    LOG_INFO("shouldEnterPosition (this={}): looking for '{}' in marketHistory_ (size={})", static_cast<const void*>(this), symbol, marketHistory_.size());
    if (it == marketHistory_.end()) {
        // LOG_INFO("Rejecting entry for {}: No market history available", symbol);
        //  // Debug: log exact key and map contents
        LOG_INFO("shouldEnterPosition: Looking for key '{}' in marketHistory_ (size={})", 
                symbol, marketHistory_.size());
        return false;  // No history, can't assess
    }
    
    auto regime = detectMarketRegime(micro, it->second);
    auto params = calculateDynamicParameters(symbol, micro, regime);
    
    // Entry filters
    
    // 1. Minimum expected edge
    if (params.expected_edge < micro.spread_ema * 1.5) {
        return false;  // Edge too small vs spread cost
    }
    //debug with reject reason
    // 2. Kelly criterion check
    if (params.kelly_fraction < 0.01) {
        LOG_INFO("Rejecting entry for {}: Kelly fraction too low ({} < 0.01)",
                  symbol, params.kelly_fraction);
        return false;  // Position size would be too small
    }
    
    // 3. Regime-specific filters
    switch (regime) {
        case MarketRegime::LOW_LIQUIDITY:
            if (micro.spread_ema > micro.tick_size * 10) {
                LOG_INFO("Rejecting entry for {}: Low liquidity (spread_ema={} > tick_size*10)",
                          symbol, micro.spread_ema);
                return false;  // Spread too wide
            }
            break;
        case MarketRegime::HIGH_VOLATILITY:
            if (confidence < 0.95) {
                LOG_INFO("Rejecting entry for {}: Low confidence in high volatility ({} < 0.95)",
                          symbol, confidence);
                return false;  // Need high confidence in volatile markets
            }
            break;
        default:
            if (confidence < 0.85) {
                LOG_INFO("Rejecting entry for {}: Low confidence in default regime ({} < 0.85)",
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
        
        // Update historical PnL for future Kelly calculations
        historicalPnLs_[symbol].push_back(final_pnl);
        if (historicalPnLs_[symbol].size() > 100) {
            historicalPnLs_[symbol].pop_front();
        }
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
