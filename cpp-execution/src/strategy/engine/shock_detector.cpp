// shock_detector.cpp - Implementation of ShockDetector
#include "strategy/engine/shock_detector.hpp"
#include "common/logger.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace trading::strategy::engine {

using json = nlohmann::json;

ShockDetector::ShockDetector(const ShockDetectorConfig& config) 
    : config_(config), last_stats_reset_(std::chrono::system_clock::now()) {
    LOG_INFO("ShockDetector initialized - recent_window: {}, baseline_window: {}, shock_multiplier: {:.2f}, suspend_duration: {}min, enabled: {}",
             config_.recent_window_size, config_.baseline_window_size, config_.shock_multiplier, 
             config_.suspend_duration.count(), config_.enable_shock_detection);
}

void ShockDetector::updatePrice(const std::string& symbol, double price, 
                               std::chrono::system_clock::time_point timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& data = symbol_data_[symbol];
    
    // Add price point to history
    data.price_history.push_back({price, timestamp});
    
    // Update returns if we have a previous price
    if (data.has_initial_price) {
        updateReturns(data, price);
    } else {
        data.has_initial_price = true;
    }
    
    data.last_price = price;
    
    // Prune old data to maintain window sizes
    pruneOldData(data);
}

ShockDetectorResult ShockDetector::checkTradingSuspended(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ShockDetectorResult result;
    total_checks_++;
    
    // If shock detection is disabled, never suspend
    if (!config_.enable_shock_detection) {
        result.suspend_trading = false;
        return result;
    }
    
    auto it = symbol_data_.find(symbol);
    if (it == symbol_data_.end()) {
        // No data for symbol - don't suspend
        result.suspend_trading = false;
        result.suspend_reason = "no_price_data";
        return result;
    }
    
    const auto& data = it->second;
    auto now = std::chrono::system_clock::now();
    
    // Check if we're currently under suspension
    if (data.suspension_end_time > now) {
        result.suspend_trading = true;
        result.suspend_reason = "shock_detected";
        result.suspension_end_time = data.suspension_end_time;
        suspended_checks_++;
        
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            data.suspension_end_time - now);
        LOG_DEBUG("ShockDetector: {} still suspended for {}s", symbol, remaining.count());
        return result;
    }
    
    // Need enough data for analysis
    if (data.returns.size() < config_.recent_window_size) {
        result.suspend_trading = false;
        result.suspend_reason = "insufficient_data";
        return result;
    }
    
    // Calculate recent volatility (std dev of recent returns)
    result.recent_volatility = calculateRecentVolatility(data.returns);
    
    // Calculate baseline volatility (median of all returns)
    result.baseline_volatility = calculateBaselineVolatility(data.returns);
    
    // Avoid division by zero
    if (result.baseline_volatility < config_.min_baseline_vol) {
        result.suspend_trading = false;
        result.suspend_reason = "low_baseline_volatility";
        return result;
    }
    
    // Calculate volatility ratio
    result.volatility_ratio = result.recent_volatility / result.baseline_volatility;
    
    // Check for shock condition
    if (result.volatility_ratio > config_.shock_multiplier) {
        result.suspend_trading = true;
        result.suspend_reason = "shock_detected";
        result.suspension_end_time = now + config_.suspend_duration;
        shock_detections_++;
        
        // Update the symbol's suspension time (need to modify const data)
        auto& mutable_data = const_cast<SymbolData&>(data);
        mutable_data.last_shock_time = now;
        mutable_data.suspension_end_time = result.suspension_end_time;
        
        LOG_WARN("ShockDetector SHOCK DETECTED: {} - recent_vol={:.6f}, baseline_vol={:.6f}, ratio={:.2f} > {:.2f}, suspending for {}min",
                 symbol, result.recent_volatility, result.baseline_volatility, 
                 result.volatility_ratio, config_.shock_multiplier, config_.suspend_duration.count());
        
        return result;
    }
    
    // No shock detected
    result.suspend_trading = false;
    result.suspend_reason = "";
    
    LOG_DEBUG("ShockDetector: {} volatility OK - recent_vol={:.6f}, baseline_vol={:.6f}, ratio={:.2f}",
              symbol, result.recent_volatility, result.baseline_volatility, result.volatility_ratio);
    
    return result;
}

void ShockDetector::updateConfig(const ShockDetectorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    LOG_INFO("ShockDetector config updated - recent_window: {}, baseline_window: {}, shock_multiplier: {:.2f}, suspend_duration: {}min, enabled: {}",
             config_.recent_window_size, config_.baseline_window_size, config_.shock_multiplier, 
             config_.suspend_duration.count(), config_.enable_shock_detection);
}

std::string ShockDetector::getStatistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    json stats;
    stats["total_checks"] = total_checks_;
    stats["shock_detections"] = shock_detections_;
    stats["suspended_checks"] = suspended_checks_;
    stats["symbols_tracked"] = symbol_data_.size();
    
    if (total_checks_ > 0) {
        stats["shock_rate"] = static_cast<double>(shock_detections_) / total_checks_;
        stats["suspension_rate"] = static_cast<double>(suspended_checks_) / total_checks_;
    }
    
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_reset_);
    stats["uptime_seconds"] = duration.count();
    
    // Symbol-specific stats
    json symbol_stats = json::array();
    for (const auto& [symbol, data] : symbol_data_) {
        json sym_stat;
        sym_stat["symbol"] = symbol;
        sym_stat["price_points"] = data.price_history.size();
        sym_stat["returns"] = data.returns.size();
        sym_stat["last_price"] = data.last_price;
        
        if (data.suspension_end_time > now) {
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                data.suspension_end_time - now);
            sym_stat["suspended_remaining_seconds"] = remaining.count();
        }
        
        if (!data.returns.empty()) {
            sym_stat["recent_volatility"] = calculateRecentVolatility(data.returns);
            sym_stat["baseline_volatility"] = calculateBaselineVolatility(data.returns);
        }
        
        symbol_stats.push_back(sym_stat);
    }
    stats["symbols"] = symbol_stats;
    
    stats["config"] = {
        {"recent_window_size", config_.recent_window_size},
        {"baseline_window_size", config_.baseline_window_size},
        {"shock_multiplier", config_.shock_multiplier},
        {"suspend_duration_minutes", config_.suspend_duration.count()},
        {"enable_shock_detection", config_.enable_shock_detection},
        {"min_baseline_vol", config_.min_baseline_vol}
    };
    
    return stats.dump(2);
}

void ShockDetector::clearSymbolData(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    symbol_data_.erase(symbol);
    LOG_INFO("ShockDetector: Cleared data for symbol {}", symbol);
}

void ShockDetector::clearAllData() {
    std::lock_guard<std::mutex> lock(mutex_);
    symbol_data_.clear();
    total_checks_ = 0;
    shock_detections_ = 0;
    suspended_checks_ = 0;
    last_stats_reset_ = std::chrono::system_clock::now();
    LOG_INFO("ShockDetector: Cleared all data");
}

double ShockDetector::calculateRecentVolatility(const std::deque<double>& returns) const {
    if (returns.size() < config_.recent_window_size) {
        return 0.0;
    }
    
    // Take the most recent N returns
    size_t start_idx = returns.size() - config_.recent_window_size;
    std::vector<double> recent_returns(returns.begin() + start_idx, returns.end());
    
    // Calculate mean
    double mean = std::accumulate(recent_returns.begin(), recent_returns.end(), 0.0) / recent_returns.size();
    
    // Calculate variance
    double variance = 0.0;
    for (double ret : recent_returns) {
        double diff = ret - mean;
        variance += diff * diff;
    }
    variance /= recent_returns.size();
    
    // Return standard deviation
    return std::sqrt(variance);
}

double ShockDetector::calculateBaselineVolatility(const std::deque<double>& returns) const {
    if (returns.empty()) {
        return 0.0;
    }
    
    // Use up to baseline_window_size returns for median calculation
    size_t window_size = std::min(returns.size(), config_.baseline_window_size);
    size_t start_idx = returns.size() - window_size;
    
    std::vector<double> baseline_returns(returns.begin() + start_idx, returns.end());
    
    // Calculate absolute returns for median volatility
    std::vector<double> abs_returns;
    abs_returns.reserve(baseline_returns.size());
    for (double ret : baseline_returns) {
        abs_returns.push_back(std::abs(ret));
    }
    
    // Calculate median of absolute returns
    std::sort(abs_returns.begin(), abs_returns.end());
    size_t mid = abs_returns.size() / 2;
    
    if (abs_returns.size() % 2 == 0) {
        return (abs_returns[mid - 1] + abs_returns[mid]) / 2.0;
    } else {
        return abs_returns[mid];
    }
}

void ShockDetector::updateReturns(SymbolData& data, double new_price) {
    if (data.last_price > 0.0 && new_price > 0.0) {
        double return_val = (new_price - data.last_price) / data.last_price;
        data.returns.push_back(return_val);
    }
}

void ShockDetector::pruneOldData(SymbolData& data) const {
    // Keep at most baseline_window_size + some buffer for price history
    size_t max_history = config_.baseline_window_size + 20;
    while (data.price_history.size() > max_history) {
        data.price_history.pop_front();
    }
    
    // Keep at most baseline_window_size returns
    while (data.returns.size() > config_.baseline_window_size) {
        data.returns.pop_front();
    }
}

} // namespace trading::strategy::engine