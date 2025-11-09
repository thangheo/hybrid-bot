/**
 * @file exposure_calculator.hpp
 * @brief Corrected exposure calculator header
 */

#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "../portfolio/position_tracker.hpp"
#include "../interfaces/data_interface.hpp"
#include "trading_signal_extensions.hpp"
#include <memory>
#include <vector>
#include <optional>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace trading::risk {

// Forward-declare PositionSynchronizer so we don't force full header includes in this public header
namespace sync { class PositionSynchronizer; }

enum class ExposureValidationResult {
    APPROVED,
    TOTAL_LIMIT_EXCEEDED,
    ASSET_LIMIT_EXCEEDED,
    STRATEGY_LIMIT_EXCEEDED,
    EXCHANGE_LIMIT_EXCEEDED,
    CALCULATION_ERROR
};

class ExposureCalculator {
public:
    struct Config {
        std::chrono::milliseconds cacheExpiryTime;
        size_t maxCacheEntries;
        bool enableOptimizations;
        
        // Default constructor with proper initialization
        Config() 
            : cacheExpiryTime(100)
            , maxCacheEntries(1000)
            , enableOptimizations(true) {}
    };

    ExposureCalculator(
        std::shared_ptr<PositionTracker> positionTracker,
        std::shared_ptr<DataInterface> dataInterface,
        Config config = Config{}
    );

    // Optional: inject PositionSynchronizer for fast cached reads
    void setPositionSynchronizer(std::shared_ptr<trading::risk::sync::PositionSynchronizer> positionSync);

    ~ExposureCalculator();
    // getCurrentExposure
    std::optional<double> getCurrentExposure(const TradingSignal& signal) const;
    // Non-copyable, movable
    ExposureCalculator(const ExposureCalculator&) = delete;
    ExposureCalculator& operator=(const ExposureCalculator&) = delete;
    ExposureCalculator(ExposureCalculator&&) = default;
    ExposureCalculator& operator=(ExposureCalculator&&) = default;

    // Initialize the calculator
    bool initialize(const RiskConfig& config);
    bool isHealthy() const;
    // Validate exposure for trading signals
    ExposureValidationResult validateExposure(const TradingSignal& signal) const;

    // Validate exposure for order requests
    ExposureValidationResult validateExposure(const OrderRequest& orderRequest) const;

    // Get detailed violation information
    std::vector<RiskViolation> getViolationDetails(const TradingSignal& signal) const;

    // Calculate exposures
    std::optional<double> calculateTotalExposure() const;
    std::optional<double> calculateAssetExposure(const std::string& assetId) const;
    std::optional<double> calculateStrategyExposure(const std::string& strategyId) const;
    std::optional<double> calculateExchangeExposure(const core::ExchangeId& exchangeId) const;

    // Update market data
    void updateMarketData(const core::MarketData& marketData);

    // Update exposure limits
    void updateExposureLimits(const ExposureLimitConfig& newLimits);
    ExposureLimitConfig getExposureLimits() const;

    // Clear caches
    void clearCaches();

private:
    // Forward declaration for PIMPL
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<trading::risk::sync::PositionSynchronizer> positionSync_;
};

} // namespace trading::risk