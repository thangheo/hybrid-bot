// meta_filter.cpp - Implementation of MetaFilter
#include "strategy/engine/meta_filter.hpp"
#include "common/logger.hpp"
#include <nlohmann/json.hpp>
#include <iomanip>
#include <algorithm>

namespace trading::strategy::engine {

using json = nlohmann::json;

MetaFilter::MetaFilter(const MetaFilterConfig& config) 
    : config_(config), last_stats_reset_(std::chrono::system_clock::now()) {
    LOG_INFO("MetaFilter initialized - min_p_trend_up: {:.3f}, min_regime_confidence: {:.3f}, enable_trend_filter: {}",
             config_.min_p_trend_up, config_.min_regime_confidence, config_.enable_trend_filter);
}

MetaFilterResult MetaFilter::checkEntryAllowed(
    const std::string& symbol,
    const core_analysis::RegimeSubscriber* regime_subscriber) const {
    
    MetaFilterResult result;
    total_checks_++;

    // If filter is disabled, allow all entries
    if (!config_.enable_trend_filter) {
        result.allow_entry = true;
        result.block_reason = "";
        allowed_entries_++;
        return result;
    }

    // Check if regime subscriber is available
    if (!regime_subscriber) {
        LOG_WARN("MetaFilter: No regime subscriber available for symbol {}", symbol);
        result.allow_entry = true; // Default to allow when no data
        result.block_reason = "no_regime_data";
        allowed_entries_++;
        return result;
    }

    // Try both symbol formats: with slash (BTC/USDT) and without (BTCUSDT)
    core_analysis::RegimeInfo regime_info;
    std::string lookup_symbol = symbol;
    
    // First try the symbol as-is
    bool found_regime = regime_subscriber->get(symbol, regime_info);
    
    // If not found, try converting format
    if (!found_regime) {
        // Convert BTC/USDT -> BTCUSDT for regime lookup
        std::string converted_symbol = symbol;
        // Remove slash and dash characters
        converted_symbol.erase(std::remove(converted_symbol.begin(), converted_symbol.end(), '/'), converted_symbol.end());
        converted_symbol.erase(std::remove(converted_symbol.begin(), converted_symbol.end(), '-'), converted_symbol.end());
        
        found_regime = regime_subscriber->get(converted_symbol, regime_info);
        if (found_regime) {
            lookup_symbol = converted_symbol;
        }
    }
    
    if (!found_regime) {
        LOG_TRACE("MetaFilter: No regime info available for symbol {} (tried: {} and converted)", symbol, lookup_symbol);
        result.allow_entry = true; // Default to allow when no data
        result.block_reason = "regime_info_unavailable";
        allowed_entries_++;
        return result;
    }

    // Check if regime info is valid
    if (!regime_info.valid) {
        LOG_TRACE("MetaFilter: Invalid regime info for symbol {}", symbol);
        result.allow_entry = true; // Default to allow when invalid
        result.block_reason = "regime_info_invalid";
        allowed_entries_++;
        return result;
    }

    // Extract p_trend_up
    if (!regime_info.p_trend_up.has_value()) {
        LOG_TRACE("MetaFilter: No p_trend_up available for symbol {}", symbol);
        result.allow_entry = true; // Default to allow when no data
        result.block_reason = "p_trend_up_unavailable";
        allowed_entries_++;
        return result;
    }

    result.p_trend_up_value = regime_info.p_trend_up.value();

    // Check p_trend_up threshold
    if (result.p_trend_up_value < config_.min_p_trend_up) {
        result.allow_entry = false;
        result.block_reason = "trend_confidence_too_low";
        result.trend_confidence = result.p_trend_up_value;
        blocked_trend_low_++;
        
        LOG_INFO("MetaFilter BLOCKED: {} - p_trend_up={:.3f} < {:.3f} (trend_confidence_too_low)",
                 symbol, result.p_trend_up_value, config_.min_p_trend_up);
        return result;
    }

    // Calculate regime confidence if stay_prob is available
    if (regime_info.stay_prob.has_value()) {
        result.regime_confidence_value = result.p_trend_up_value * regime_info.stay_prob.value();
        
        // Check regime confidence threshold
        if (result.regime_confidence_value < config_.min_regime_confidence) {
            result.allow_entry = false;
            result.block_reason = "regime_confidence_too_low";
            result.trend_confidence = result.regime_confidence_value;
            blocked_regime_confidence_++;
            
            LOG_INFO("MetaFilter BLOCKED: {} - regime_confidence={:.3f} (p_trend_up={:.3f} * stay_prob={:.3f}) < {:.3f}",
                     symbol, result.regime_confidence_value, result.p_trend_up_value, 
                     regime_info.stay_prob.value(), config_.min_regime_confidence);
            return result;
        }
        
        result.trend_confidence = result.regime_confidence_value;
    } else {
        // If no stay_prob, use p_trend_up as trend confidence
        result.trend_confidence = result.p_trend_up_value;
        result.regime_confidence_value = result.p_trend_up_value;
    }

    // All checks passed - allow entry
    result.allow_entry = true;
    result.block_reason = "";
    allowed_entries_++;
    
    LOG_TRACE("MetaFilter ALLOWED: {} - p_trend_up={:.3f}, regime_confidence={:.3f}",
              symbol, result.p_trend_up_value, result.regime_confidence_value);
    
    return result;
}

MetaFilterResult MetaFilter::checkEmergencyExit(
    const std::string& symbol,
    const core_analysis::RegimeSubscriber* regime_subscriber) const {
    
    MetaFilterResult result;
    total_checks_++;

    // If emergency exit is disabled, never trigger
    if (!config_.enable_emergency_exit) {
        result.trigger_emergency_exit = false;
        return result;
    }

    // Check if regime subscriber is available
    if (!regime_subscriber) {
        LOG_TRACE("MetaFilter Emergency: No regime subscriber available for symbol {}", symbol);
        result.trigger_emergency_exit = false;
        result.emergency_reason = "no_regime_data";
        return result;
    }

    // Try both symbol formats: with slash (BTC/USDT) and without (BTCUSDT)
    core_analysis::RegimeInfo regime_info;
    std::string lookup_symbol = symbol;
    
    // First try the symbol as-is
    bool found_regime = regime_subscriber->get(symbol, regime_info);
    
    // If not found, try converting format
    if (!found_regime) {
        // Convert BTC/USDT -> BTCUSDT for regime lookup
        std::string converted_symbol = symbol;
        // Remove slash and dash characters
        converted_symbol.erase(std::remove(converted_symbol.begin(), converted_symbol.end(), '/'), converted_symbol.end());
        converted_symbol.erase(std::remove(converted_symbol.begin(), converted_symbol.end(), '-'), converted_symbol.end());
        
        found_regime = regime_subscriber->get(converted_symbol, regime_info);
        if (found_regime) {
            lookup_symbol = converted_symbol;
        }
    }
    
    if (!found_regime) {
        LOG_TRACE("MetaFilter Emergency: No regime info available for symbol {} (tried: {} and converted)", symbol, lookup_symbol);
        result.trigger_emergency_exit = false;
        result.emergency_reason = "regime_info_unavailable";
        return result;
    }

    // Check if regime info is valid
    if (!regime_info.valid) {
        LOG_TRACE("MetaFilter Emergency: Invalid regime info for symbol {}", symbol);
        result.trigger_emergency_exit = false;
        result.emergency_reason = "regime_info_invalid";
        return result;
    }

    // Extract p_trend_up
    if (!regime_info.p_trend_up.has_value()) {
        LOG_TRACE("MetaFilter Emergency: No p_trend_up available for symbol {}", symbol);
        result.trigger_emergency_exit = false;
        result.emergency_reason = "p_trend_up_unavailable";
        return result;
    }

    result.p_trend_up_value = regime_info.p_trend_up.value();

    // Check emergency p_trend_up threshold
    if (result.p_trend_up_value <= config_.emergency_p_trend_threshold) {
        result.trigger_emergency_exit = true;
        result.emergency_reason = "market_dump_detected";
        result.trend_confidence = result.p_trend_up_value;
        emergency_exits_triggered_++;
        
        LOG_TRACE("MetaFilter EMERGENCY EXIT: {} - p_trend_up={:.3f} <= {:.3f} (MARKET DUMP DETECTED!)",
                 symbol, result.p_trend_up_value, config_.emergency_p_trend_threshold);
        return result;
    }

    // Calculate regime confidence if stay_prob is available for additional check
    if (regime_info.stay_prob.has_value()) {
        result.regime_confidence_value = result.p_trend_up_value * regime_info.stay_prob.value();
        
        // Check emergency regime confidence threshold
        if (result.regime_confidence_value <= config_.emergency_regime_threshold) {
            result.trigger_emergency_exit = true;
            result.emergency_reason = "regime_collapse_detected";
            result.trend_confidence = result.regime_confidence_value;
            emergency_exits_triggered_++;
            
            LOG_WARN("MetaFilter EMERGENCY EXIT: {} - regime_confidence={:.3f} (p_trend_up={:.3f} * stay_prob={:.3f}) <= {:.3f} (REGIME COLLAPSE!)",
                     symbol, result.regime_confidence_value, result.p_trend_up_value, 
                     regime_info.stay_prob.value(), config_.emergency_regime_threshold);
            return result;
        }
        
        result.trend_confidence = result.regime_confidence_value;
    } else {
        result.trend_confidence = result.p_trend_up_value;
        result.regime_confidence_value = result.p_trend_up_value;
    }

    // No emergency exit needed
    result.trigger_emergency_exit = false;
    result.emergency_reason = "";
    
    LOG_TRACE("MetaFilter Emergency OK: {} - p_trend_up={:.3f}, regime_confidence={:.3f}",
              symbol, result.p_trend_up_value, result.regime_confidence_value);
    
    return result;
}

void MetaFilter::updateConfig(const MetaFilterConfig& config) {
    config_ = config;
    LOG_INFO("MetaFilter config updated - min_p_trend_up: {:.3f}, min_regime_confidence: {:.3f}, enable_trend_filter: {}",
             config_.min_p_trend_up, config_.min_regime_confidence, config_.enable_trend_filter);
}

std::string MetaFilter::getStatistics() const {
    json stats;
    stats["total_checks"] = total_checks_;
    stats["allowed_entries"] = allowed_entries_;
    stats["blocked_trend_low"] = blocked_trend_low_;
    stats["blocked_regime_confidence"] = blocked_regime_confidence_;
    stats["emergency_exits_triggered"] = emergency_exits_triggered_;
    
    if (total_checks_ > 0) {
        stats["allow_rate"] = static_cast<double>(allowed_entries_) / total_checks_;
        stats["block_rate_trend"] = static_cast<double>(blocked_trend_low_) / total_checks_;
        stats["block_rate_regime"] = static_cast<double>(blocked_regime_confidence_) / total_checks_;
    }
    
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_reset_);
    stats["uptime_seconds"] = duration.count();
    
    stats["config"] = {
        {"min_p_trend_up", config_.min_p_trend_up},
        {"min_regime_confidence", config_.min_regime_confidence},
        {"enable_trend_filter", config_.enable_trend_filter},
        {"enable_emergency_exit", config_.enable_emergency_exit},
        {"emergency_p_trend_threshold", config_.emergency_p_trend_threshold},
        {"emergency_regime_threshold", config_.emergency_regime_threshold}
    };
    
    return stats.dump(2);
}

} // namespace trading::strategy::engine