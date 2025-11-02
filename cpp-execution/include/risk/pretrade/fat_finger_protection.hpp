/**
 * @file fat_finger_protection.hpp
 * @brief Fat finger protection for preventing abnormal orders
 * 
 * Prevents abnormally large or mispriced orders using statistical analysis.
 * 
 * @author Trading System Team
 * @date May 28, 2025
 */

#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "../interfaces/data_interface.hpp"
#include "execution/trading_signal.hpp"
#include "common/logger.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <atomic>
#include <optional>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>

namespace trading::risk {

/**
 * Statistical analyzer for order patterns
 */
class OrderPatternAnalyzer {
public:
    struct Statistics {
        double mean;
        double median;
        double stdDev;
        size_t sampleSize;
        
        Statistics() 
            : mean(0.0)
            , median(0.0)
            , stdDev(0.0)
            , sampleSize(0)
        {}
        
        bool isValid() const noexcept {
            return sampleSize > 0 && std::isfinite(mean) && std::isfinite(stdDev);
        }
    };

    explicit OrderPatternAnalyzer(size_t maxHistorySize = 100);
    
    void addSample(double value);
    Statistics getStatistics() const;
    double getAnomalyScore(double value) const;
    bool isOutlier(double value, double thresholdSigmas = 3.0) const;

private:
    mutable std::mutex mutex_;
    std::deque<double> samples_;
    size_t maxHistorySize_;
    mutable std::optional<Statistics> cachedStats_;
    mutable bool statsDirty_;

    Statistics calculateStatistics() const;
};

/**
 * Fat finger protection implementation
 */
class FatFingerProtection {
public:
    struct Config {
        double priceDeviationThreshold;      // 5%
        double sizeAnomalyThreshold;         // 3 sigma
        double notionalValueThreshold;       // $10k
        double minimumHistorySize;           // Min samples needed
        bool rejectHighNotionalOrders;
        
        Config() 
            : priceDeviationThreshold(0.05)
            , sizeAnomalyThreshold(3.0)
            , notionalValueThreshold(10000.0)
            , minimumHistorySize(10.0)
            , rejectHighNotionalOrders(false)
        {}
        
        bool isValid() const noexcept {
            return priceDeviationThreshold > 0.0 && priceDeviationThreshold < 1.0 &&
                   sizeAnomalyThreshold > 0.0 &&
                   notionalValueThreshold > 0.0 &&
                   minimumHistorySize > 0.0;
        }
    };

    // Overloaded constructor with default config
    explicit FatFingerProtection(std::shared_ptr<DataInterface> dataInterface);
    
    explicit FatFingerProtection(
        std::shared_ptr<DataInterface> dataInterface,
        const Config& config
    );

    // Backwards-compatible constructor used by older tests/code that pass a
    // RiskConfig and a DataInterface. It extracts the FatFingerConfig from the
    // provided RiskConfig.
    explicit FatFingerProtection(const RiskConfig& riskConfig, std::shared_ptr<DataInterface> dataInterface) {
        dataInterface_ = std::move(dataInterface);
        // Copy fields individually to avoid requiring an assignment operator
        // between potentially different config types returned by RiskConfig.
        auto ff = riskConfig.getFatFingerConfig();
        config_.priceDeviationThreshold = ff.priceDeviationThreshold;
        config_.sizeAnomalyThreshold = ff.sizeAnomalyThreshold;
        config_.notionalValueThreshold = ff.notionalValueThreshold;
        config_.minimumHistorySize = ff.defaultTypicalSize;
        config_.rejectHighNotionalOrders = ff.rejectHighNotionalOrders;
        totalValidations_ = 0;
        rejectedOrders_ = 0;
    }
    
    bool initialize();
    // isHealthy
    bool isHealthy() const {
        return dataInterface_ != nullptr && config_.isValid();
    }
    // Main validation methods
    bool checkFatFinger(const execution::TradingSignal& signal, 
                       std::vector<RiskViolation>& violations);
    
    bool checkFatFinger(const OrderRequest& orderRequest,
                       std::vector<RiskViolation>& violations);
    
    // Record order for learning
    void recordOrder(const OrderRequest& order);
    
    // Configuration management
    bool updateConfig(const Config& newConfig);
    const Config& getConfig() const { return config_; }
    
    // Utilities
    bool requiresAdditionalVerification(const core::Symbol& symbol,
                                      double price, double quantity) const;
    
    double getTypicalOrderSize(const core::Symbol& symbol) const;
    
    std::optional<OrderPatternAnalyzer::Statistics> 
    getOrderStatistics(const core::Symbol& symbol) const;

private:
    std::shared_ptr<DataInterface> dataInterface_;
    Config config_;
    
    // Thread-safe order history
    mutable std::mutex historyMutex_;
    std::unordered_map<std::string, std::unique_ptr<OrderPatternAnalyzer>> orderAnalyzers_;
    
    // Performance metrics
    std::atomic<uint64_t> totalValidations_;
    std::atomic<uint64_t> rejectedOrders_;

    // Helper methods
    std::string normalizeSymbol(const core::Symbol& symbol) const;
    
    bool checkPriceDeviation(const core::Symbol& symbol,
                           double price,
                           std::vector<RiskViolation>& violations);
    
    bool checkSizeAnomaly(const core::Symbol& symbol,
                         double quantity,
                         std::vector<RiskViolation>& violations);
    
    bool checkNotionalValue(const core::Symbol& symbol,
                           double price,
                           double quantity,
                           std::vector<RiskViolation>& violations);
    
    double getCurrentMarketPrice(const core::Symbol& symbol) const;
    OrderPatternAnalyzer& getOrCreateAnalyzer(const core::Symbol& symbol);
};

} // namespace trading::risk