// meta_filter.hpp - Meta filter for regime-based trading decisions
#pragma once

#include <string>
#include <optional>
#include <chrono>
#include "strategy/core_analysis/regime_subscriber.hpp"

namespace trading::strategy::engine {

struct MetaFilterConfig {
    double min_p_trend_up = 0.3;           // Minimum p_trend_up threshold
    double min_regime_confidence = 0.2;    // Minimum regime_confidence threshold
    bool enable_trend_filter = true;       // Enable trend confidence filtering
};

struct MetaFilterResult {
    bool allow_entry = true;               // Whether to allow entry signals
    std::string block_reason;              // Reason for blocking (if any)
    double trend_confidence = 0.0;         // Calculated trend confidence
    double p_trend_up_value = 0.0;         // Current p_trend_up value
    double regime_confidence_value = 0.0;  // Calculated regime confidence
};

/**
 * @brief Meta filter that uses regime information to block entry signals
 * 
 * This filter prevents entry signals when:
 * 1. p_trend_up < min_p_trend_up (default 0.3)
 * 2. regime_confidence = p_trend_up * stay_prob < min_regime_confidence (default 0.2)
 * 
 * The filter is designed to prevent trading during periods of high uncertainty
 * or when the market regime indicates low probability of trend continuation.
 */
class MetaFilter {
public:
    explicit MetaFilter(const MetaFilterConfig& config = MetaFilterConfig{});
    ~MetaFilter() = default;

    // Non-copyable, non-movable
    MetaFilter(const MetaFilter&) = delete;
    MetaFilter& operator=(const MetaFilter&) = delete;

    /**
     * @brief Check if entry signal should be allowed based on regime information
     * @param symbol Trading symbol to check
     * @param regime_subscriber Regime subscriber to get regime data from
     * @return MetaFilterResult with decision and details
     */
    MetaFilterResult checkEntryAllowed(
        const std::string& symbol,
        const core_analysis::RegimeSubscriber* regime_subscriber) const;

    /**
     * @brief Update filter configuration
     * @param config New configuration
     */
    void updateConfig(const MetaFilterConfig& config);

    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    const MetaFilterConfig& getConfig() const { return config_; }

    /**
     * @brief Get filter statistics
     * @return JSON string with filter statistics
     */
    std::string getStatistics() const;

private:
    MetaFilterConfig config_;
    
    // Statistics (mutable for const methods)
    mutable size_t total_checks_ = 0;
    mutable size_t blocked_trend_low_ = 0;
    mutable size_t blocked_regime_confidence_ = 0;
    mutable size_t allowed_entries_ = 0;
    mutable std::chrono::system_clock::time_point last_stats_reset_;
};

} // namespace trading::strategy::engine