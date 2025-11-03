// shock_detector.hpp - Shock detection based on volatility analysis
#pragma once

#include <string>
#include <chrono>
#include <deque>
#include <unordered_map>
#include <mutex>

namespace trading::strategy::engine {

struct ShockDetectorConfig {
    size_t recent_window_size = 10;           // Number of recent returns to analyze
    size_t baseline_window_size = 60;         // Number of baseline returns for median calculation
    double shock_multiplier = 5.0;            // Multiplier for shock detection (recent_vol > multiplier * baseline_vol)
    std::chrono::minutes suspend_duration{5}; // Duration to suspend trading after shock
    bool enable_shock_detection = true;       // Enable shock detection
    double min_baseline_vol = 0.0001;         // Minimum baseline volatility to avoid division by zero
};

struct ShockDetectorResult {
    bool suspend_trading = false;             // Whether to suspend trading
    std::string suspend_reason;               // Reason for suspension (if any)
    double recent_volatility = 0.0;           // Recent volatility value
    double baseline_volatility = 0.0;         // Baseline volatility value
    double volatility_ratio = 0.0;            // Ratio of recent to baseline volatility
    std::chrono::system_clock::time_point suspension_end_time; // When suspension ends
};

/**
 * @brief Shock detector that monitors volatility to suspend trading during extreme market conditions
 * 
 * This detector:
 * 1. Maintains a rolling window of price returns for each symbol
 * 2. Calculates recent volatility (std dev of recent returns)
 * 3. Compares against baseline volatility (median of longer window)
 * 4. Suspends trading when recent_vol > shock_multiplier * baseline_vol
 * 5. Maintains suspension for configured duration
 */
class ShockDetector {
public:
    explicit ShockDetector(const ShockDetectorConfig& config = ShockDetectorConfig{});
    ~ShockDetector() = default;

    // Non-copyable, non-movable
    ShockDetector(const ShockDetector&) = delete;
    ShockDetector& operator=(const ShockDetector&) = delete;

    /**
     * @brief Update price data for a symbol
     * @param symbol Trading symbol
     * @param price Current price
     * @param timestamp Price timestamp
     */
    void updatePrice(const std::string& symbol, double price, 
                    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now());

    /**
     * @brief Check if trading should be suspended for a symbol
     * @param symbol Trading symbol to check
     * @return ShockDetectorResult with decision and details
     */
    ShockDetectorResult checkTradingSuspended(const std::string& symbol) const;

    /**
     * @brief Update detector configuration
     * @param config New configuration
     */
    void updateConfig(const ShockDetectorConfig& config);

    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    const ShockDetectorConfig& getConfig() const { return config_; }

    /**
     * @brief Get detector statistics
     * @return JSON string with detector statistics
     */
    std::string getStatistics() const;

    /**
     * @brief Clear all data for a symbol
     * @param symbol Trading symbol to clear
     */
    void clearSymbolData(const std::string& symbol);

    /**
     * @brief Clear all data
     */
    void clearAllData();

private:
    struct PricePoint {
        double price;
        std::chrono::system_clock::time_point timestamp;
    };

    struct SymbolData {
        std::deque<PricePoint> price_history;
        std::deque<double> returns;
        std::chrono::system_clock::time_point last_shock_time;
        std::chrono::system_clock::time_point suspension_end_time;
        double last_price = 0.0;
        bool has_initial_price = false;
    };

    ShockDetectorConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SymbolData> symbol_data_;

    // Statistics (mutable for const methods)
    mutable size_t total_checks_ = 0;
    mutable size_t shock_detections_ = 0;
    mutable size_t suspended_checks_ = 0;
    mutable std::chrono::system_clock::time_point last_stats_reset_;

    // Helper methods
    double calculateRecentVolatility(const std::deque<double>& returns) const;
    double calculateBaselineVolatility(const std::deque<double>& returns) const;
    void updateReturns(SymbolData& data, double new_price);
    void pruneOldData(SymbolData& data) const;
};

} // namespace trading::strategy::engine