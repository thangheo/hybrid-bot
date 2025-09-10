/**
 * @file stop_loss_manager.hpp
 * @brief Modern C++20 Stop Loss Manager with enhanced type safety and performance
 * 
 * Implements comprehensive stop-loss management with trailing stops, automatic liquidation,
 * and thread-safe operations using modern C++20 features.
 * 
 * @author Trading System Team
 * @date May 26, 2025
 */

#pragma once

#include "../risk_types.hpp"
#include "../risk_events.hpp"
#include "../risk_config.hpp"
#include "../portfolio/position_tracker.hpp"
#include "../interfaces/data_interface.hpp"
#include "../interfaces/execution_interface.hpp"
#include "execution/models/execution_report.hpp"
#include "core/market_types.hpp"
#include "core/time.hpp"

#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace trading::risk {

struct StopLossData {
    double stopPrice;
    bool isTrailing;
    double trailingDistance;
    bool active;
    double highestPrice;
    double lowestPrice;
    int adjustmentCount;
    std::chrono::system_clock::time_point lastUpdated;
};

// Strong type definitions for better type safety
struct StopPrice {
    double value{0.0};
    explicit StopPrice(double val) : value(val) {}
    operator double() const { return value; }
};

struct TrailingDistance {
    double percentage{0.0};
    explicit TrailingDistance(double pct) : percentage(pct) {}
    operator double() const { return percentage; }
};

/**
 * @concept PriceProvider
 * @brief Concept for objects that can provide price information
 */
template<typename T>
concept PriceProvider = requires(T t, const core::Symbol& symbol) {
    { t.getLastPrice(symbol) } -> std::convertible_to<std::optional<double>>;
};

/**
 * Enhanced stop loss information with atomic operations for thread safety
 */
struct StopLossInfo {
    std::atomic<double> stopPrice{0.0};
    std::atomic<bool> isTrailing{false};
    std::atomic<double> trailingDistance{0.0};
    std::atomic<bool> active{true};
    std::atomic<uint64_t> lastUpdatedNanos{0};
    
    // Trailing stop tracking
    std::atomic<double> highestPrice{0.0};  // For long positions
    std::atomic<double> lowestPrice{0.0};   // For short positions
    
    // Performance metrics
    std::atomic<uint32_t> adjustmentCount{0};
    std::atomic<double> maxDistanceFromEntry{0.0};
    
    // Convenience methods for timestamp handling
    void setLastUpdated(const core::Timestamp& timestamp) {
        lastUpdatedNanos.store(timestamp.time_since_epoch().count());
    }
    
    core::Timestamp getLastUpdated() const {
        return core::Timestamp(std::chrono::nanoseconds(lastUpdatedNanos.load()));
    }
    
    // Thread-safe update method
    void updateTrailingStop(double newPrice, bool isLongPosition) {
        if (!isTrailing.load()) return;
        
        if (isLongPosition) {
            double currentHigh = highestPrice.load();
            if (newPrice > currentHigh) {
                if (highestPrice.compare_exchange_weak(currentHigh, newPrice)) {
                    double newStop = newPrice * (1.0 - trailingDistance.load());
                    stopPrice.store(newStop);
                    adjustmentCount.fetch_add(1);
                    setLastUpdated(core::now());
                }
            }
        } else {
            double currentLow = lowestPrice.load();
            if (currentLow == 0.0 || newPrice < currentLow) {
                if (lowestPrice.compare_exchange_weak(currentLow, newPrice)) {
                    double newStop = newPrice * (1.0 + trailingDistance.load());
                    stopPrice.store(newStop);
                    adjustmentCount.fetch_add(1);
                    setLastUpdated(core::now());
                }
            }
        }
    }
    
    // Validation
    [[nodiscard]] bool isValid() const noexcept {
        return stopPrice.load() > 0.0 && 
               trailingDistance.load() >= 0.0 && 
               (isTrailing.load() ? trailingDistance.load() > 0.0 : true);
    }
};

/**
 * Enhanced stop loss event with detailed information
 */
struct EnhancedStopLossEvent {
    core::ExchangeId exchangeId;
    core::Symbol tradingPair;
    std::string strategyId;
    double positionSize{0.0};
    double entryPrice{0.0};
    double triggerPrice{0.0};
    double stopPrice{0.0};
    double lossPercentage{0.0};
    bool wasTrailing{false};
    bool liquidationInitiated{false};
    bool liquidationSuccessful{false};
    core::Timestamp timestamp{core::now()};
    std::string liquidationOrderId;
    uint32_t adjustmentCount{0};
    
    // Calculated fields
    double lossAmount() const noexcept {
        return std::abs(positionSize * (triggerPrice - entryPrice));
    }
    
    bool isCriticalLoss(double threshold = 0.10) const noexcept {
        return lossPercentage >= threshold;
    }
};

/**
 * Configuration for stop loss behavior
 */
struct EnhancedStopLossConfig {
    // Basic stop loss settings
    bool enableDefaultStopLoss{true};
    double defaultStopLossPercent{0.03};        // 3% default
    bool useTrailingStopByDefault{false};
    double defaultTrailingDistance{0.02};       // 2% trailing distance
    
    // Liquidation settings
    bool autoLiquidateOnTrigger{true};
    std::chrono::seconds liquidationTimeoutSeconds{30};
    bool useMarketOrdersForLiquidation{true};
    
    // Risk thresholds
    double criticalLossThreshold{0.08};         // 8% for critical alerts
    double emergencyLossThreshold{0.15};        // 15% for emergency actions
    
    // Performance settings
    std::chrono::seconds updateIntervalSeconds{1};
    size_t maxHistorySize{1000};
    bool enableDetailedLogging{true};
    
    // Validation and safety
    double maxStopLossPercent{0.25};            // 25% maximum stop loss
    double minTrailingDistance{0.005};          // 0.5% minimum trailing
    bool requireConfirmationForLargePositions{true};
    double largePositionThreshold{10000.0};     // USD threshold
    
    [[nodiscard]] bool isValid() const noexcept {
        return defaultStopLossPercent > 0.0 &&
               defaultStopLossPercent <= maxStopLossPercent &&
               defaultTrailingDistance >= minTrailingDistance &&
               criticalLossThreshold > defaultStopLossPercent &&
               emergencyLossThreshold > criticalLossThreshold &&
               liquidationTimeoutSeconds.count() > 0 &&
               maxHistorySize > 0;
    }
};

/**
 * @class StopLossManager
 * @brief Modern C++20 stop loss manager with enhanced features
 * 
 * Provides comprehensive stop-loss management including:
 * - Automatic stop-loss creation based on positions
 * - Trailing stop functionality with atomic updates
 * - Emergency liquidation capabilities
 * - Thread-safe operations with minimal contention
 * - Comprehensive event handling and monitoring
 */
class StopLossManager {
public:
    using StopLossEventCallback = std::function<void(const EnhancedStopLossEvent&)>;
    using PositionKey = std::pair<core::ExchangeId, std::string>;
    
    /**
     * Constructor with dependency injection
     */
    StopLossManager(
        const RiskConfig& config,
        std::shared_ptr<PositionTracker> positionTracker,
        std::shared_ptr<DataInterface> dataInterface,
        std::shared_ptr<ExecutionInterface> executionInterface
    );
    
    /**
     * Destructor ensures proper cleanup
     */
    ~StopLossManager() = default;
    
    // Disable copy operations, enable move
    StopLossManager(const StopLossManager&) = delete;
    StopLossManager& operator=(const StopLossManager&) = delete;
    StopLossManager(StopLossManager&&) = default;
    StopLossManager& operator=(StopLossManager&&) = default;
    
    /**
     * Initialize the stop loss manager
     */
    [[nodiscard]] bool initialize();
    
    /**
     * Process execution report and update stop losses
     */
    void processExecutionReport(const execution::models::ExecutionReport& executionReport);
    
    /**
     * Update market data for trailing stops
     */
    void updateMarketData(const core::MarketData& marketData);
    
    /**
     * Set stop loss for a position with enhanced parameters
     */
    [[nodiscard]] bool setStopLoss(
        const core::ExchangeId& exchangeId,
        const core::Symbol& tradingPair,
        StopPrice stopPrice,
        bool isTrailing = false,
        std::optional<TrailingDistance> trailingDistance = std::nullopt
    );
    
    /**
     * Remove stop loss for a position
     */
    [[nodiscard]] bool removeStopLoss(
        const core::ExchangeId& exchangeId, 
        const core::Symbol& tradingPair
    );
    
    /**
     * Get stop loss information (thread-safe)
     */ 
    [[nodiscard]] std::optional<StopLossData> getStopLoss(
        const core::ExchangeId& exchangeId, 
        const core::Symbol& tradingPair
    ) const;

    /**
     * Check all stop losses and return triggered events
     */
    [[nodiscard]] std::vector<EnhancedStopLossEvent> checkStopLosses();
    
    /**
     * Liquidate position immediately
     */
    [[nodiscard]] std::optional<std::string> liquidatePosition(
        const core::ExchangeId& exchangeId, 
        const core::Symbol& tradingPair
    );
    
    /**
     * Update configuration with validation
     */
    [[nodiscard]] bool updateConfiguration(const EnhancedStopLossConfig& config);
    
    /**
     * Get current configuration
     */
    [[nodiscard]] EnhancedStopLossConfig getConfiguration() const;
    
    /**
     * Register callback for stop loss events
     */
    void registerStopLossCallback(const StopLossEventCallback& callback);
    
    /**
     * Get comprehensive statistics
     */
    [[nodiscard]] std::map<std::string, double> getStatistics() const;
    
    /**
     * Get all active stop losses (thread-safe copy)
     */
    [[nodiscard]] std::map<PositionKey, StopLossData> getAllStopLosses() const;
    
    /**
     * Force recalculation of all trailing stops
     */
    void recalculateTrailingStops();
    
    /**
     * Health check and diagnostics
     */
    [[nodiscard]] bool isHealthy() const noexcept;
    [[nodiscard]] std::map<std::string, std::string> getDiagnostics() const;

private:
    // Dependencies
    std::shared_ptr<PositionTracker> positionTracker_;
    std::shared_ptr<DataInterface> dataInterface_;
    std::shared_ptr<ExecutionInterface> executionInterface_;
    
    // Configuration with thread safety
    mutable std::shared_mutex configMutex_;
    EnhancedStopLossConfig config_;
    
    // Stop loss storage with fine-grained locking
    mutable std::shared_mutex stopLossMutex_;
    std::unordered_map<std::string, std::unique_ptr<StopLossInfo>> stopLosses_;
    
    // Market data cache for performance
    mutable std::shared_mutex marketDataMutex_;
    std::unordered_map<std::string, double> priceCache_;
    
    // Event callbacks
    // mutable std::mutex callbackMutex_;
    mutable std::shared_mutex callbackMutex_;
    std::vector<StopLossEventCallback> callbacks_;
    
    // Performance tracking
    std::atomic<uint64_t> totalChecks_{0};
    std::atomic<uint64_t> totalTriggers_{0};
    std::atomic<uint64_t> totalLiquidations_{0};
    std::atomic<uint64_t> successfulLiquidations_{0};
    
    // Private helper methods
    [[nodiscard]] std::string createPositionKey(
        const core::ExchangeId& exchangeId, 
        const core::Symbol& tradingPair
    ) const;
    
    [[nodiscard]] std::optional<double> getCurrentPrice(
        const core::Symbol& tradingPair
    ) const;
    
    [[nodiscard]] bool isStopLossTriggered(
        const StopLossInfo& stopLoss, 
        double currentPrice, 
        double positionSize
    ) const noexcept;
    
    [[nodiscard]] EnhancedStopLossEvent createStopLossEvent(
        const core::ExchangeId& exchangeId,
        const core::Symbol& tradingPair,
        const StopLossInfo& stopLoss,
        double positionSize,
        double entryPrice,
        double currentPrice
    ) const;
    
    void updateTrailingStops(const core::Symbol& symbol, double price);
    void notifyCallbacks(const EnhancedStopLossEvent& event);
    
    [[nodiscard]] bool shouldCreateDefaultStopLoss(
        const core::ExchangeId& exchangeId,
        const core::Symbol& tradingPair,
        double positionSize
    ) const;
    
    [[nodiscard]] double calculateDefaultStopPrice(
        double entryPrice, 
        double positionSize
    ) const noexcept;
    
    [[nodiscard]] bool validateStopLossParameters(
        StopPrice stopPrice,
        bool isTrailing,
        std::optional<TrailingDistance> trailingDistance
    ) const noexcept;
    
    // Template helper for safe execution
    template<typename Func>
    requires std::invocable<Func>
    auto safeExecute(Func&& func) const noexcept -> std::optional<decltype(func())> {
        try {
            return std::forward<Func>(func)();
        } catch (...) {
            return std::nullopt;
        }
    }
    
    // Range-based filtering for active stop losses
    auto getActiveStopLosses() const {
        std::shared_lock lock(stopLossMutex_);
        return stopLosses_ | std::views::filter([](const auto& pair) {
            return pair.second && pair.second->active.load();
        });
    }
};

} // namespace trading::risk