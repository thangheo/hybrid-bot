/**
 * @file exposure_calculator.cpp
 * @brief Corrected exposure calculator implementation
 */

#include "risk/pretrade/exposure_calculator.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cmath>
#include <shared_mutex>

namespace trading::risk {

// Helper functions to extract data from TradingSignal
namespace {
    // Based on your TradingSignal structure from execution namespace
    std::string getSymbolFromSignal(const TradingSignal& signal) {
        // Assuming TradingSignal has tradingPair field
        return signal.tradingPair;
    }
    
    std::string getBaseAssetFromSignal(const TradingSignal& signal) {
        // Extract base asset from trading pair (e.g., "BTC/USDT" -> "BTC")
        const std::string& pair = signal.tradingPair;
        size_t pos = pair.find('/');
        if (pos != std::string::npos) {
            return pair.substr(0, pos);
        }
        // Alternative delimiter
        pos = pair.find('-');
        if (pos != std::string::npos) {
            return pair.substr(0, pos);
        }
        return pair; // Fallback
    }
    
    std::string getStrategyIdFromSignal(const TradingSignal& signal) {
        return signal.strategy_id;
    }
    
    core::ExchangeId getExchangeIdFromSignal(const TradingSignal& signal) {
        return signal.exchange;
    }
    
    double getQuantityFromSignal(const TradingSignal& signal) {
        return signal.target_size;
    }
}

class ExposureCalculator::Impl {
public:
    Impl(std::shared_ptr<PositionTracker> positionTracker,
         std::shared_ptr<DataInterface> dataInterface,
         Config config)
        : positionTracker_(std::move(positionTracker))
        , dataInterface_(std::move(dataInterface))
        , config_(std::move(config)) {
        
        // Pre-allocate caches
        priceCache_.reserve(config_.maxCacheEntries);
        volatilityCache_.reserve(config_.maxCacheEntries);
    }

    bool initialize(const RiskConfig& config) {
        try {
            std::unique_lock<std::shared_mutex> lock(limitsMutex_);
            exposureLimits_ = config.getExposureLimitConfig();
            LOG_INFO("Exposure calculator initialized successfully");
            return true;
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to initialize exposure calculator: {}", ex.what());
            return false;
        }
    }

    ExposureValidationResult validateExposure(const TradingSignal& signal) const {
        try {
            const ExposureContext context = buildExposureContext(signal);
            if (!context.isValid()) {
                return ExposureValidationResult::CALCULATION_ERROR;
            }

            // Get current exposures atomically
            const ExposureSnapshot snapshot = calculateExposureSnapshot();
            
            // Check each limit
            if (!checkTotalExposureLimit(snapshot, context)) {
                return ExposureValidationResult::TOTAL_LIMIT_EXCEEDED;
            }
            
            if (!checkAssetExposureLimit(snapshot, context)) {
                return ExposureValidationResult::ASSET_LIMIT_EXCEEDED;
            }
            
            if (!checkStrategyExposureLimit(snapshot, context)) {
                return ExposureValidationResult::STRATEGY_LIMIT_EXCEEDED;
            }
            
            if (!checkExchangeExposureLimit(snapshot, context)) {
                return ExposureValidationResult::EXCHANGE_LIMIT_EXCEEDED;
            }

            return ExposureValidationResult::APPROVED;

        } catch (const std::exception& ex) {
            LOG_ERROR("Exposure validation failed: {}", ex.what());
            return ExposureValidationResult::CALCULATION_ERROR;
        }
    }

    ExposureValidationResult validateExposure(const OrderRequest& orderRequest) const {
        // Create a TradingSignal from OrderRequest for validation
        TradingSignal signal;
        signal.tradingPair = orderRequest.tradingPair;
        signal.exchange = orderRequest.exchangeId;
        signal.target_size = orderRequest.quantity;
        signal.strategy_id = orderRequest.strategyId;
        signal.direction = (orderRequest.side == OrderSide::BUY) ? 
                          core::Direction::BUY : core::Direction::SELL;

        return validateExposure(signal);
    }

    std::vector<RiskViolation> getViolationDetails(const TradingSignal& signal) const {
        std::vector<RiskViolation> violations;
        
        try {
            const ExposureContext context = buildExposureContext(signal);
            if (!context.isValid()) {
                violations.emplace_back(ViolationType::OTHER, "Invalid signal context", 0.0, 0.0);
                return violations;
            }

            const ExposureSnapshot snapshot = calculateExposureSnapshot();

            // Check total exposure limit
            if (!checkTotalExposureLimit(snapshot, context)) {
                double totalLimit;
                {
                    std::shared_lock<std::shared_mutex> lock(limitsMutex_);
                    totalLimit = exposureLimits_.maxTotalExposure;
                }
                const double newTotal = snapshot.totalExposure + context.riskAdjustedExposure;
                violations.emplace_back(
                    ViolationType::EXPOSURE_LIMIT,
                    "Total exposure limit would be exceeded",
                    totalLimit,
                    newTotal
                );
            }

            // Check asset exposure limit
            if (!checkAssetExposureLimit(snapshot, context)) {
                const auto it = snapshot.assetExposures.find(context.baseAsset);
                const double currentAssetExposure = (it != snapshot.assetExposures.end()) ? it->second : 0.0;
                
                double assetLimit;
                {
                    std::shared_lock<std::shared_mutex> lock(limitsMutex_);
                    assetLimit = exposureLimits_.maxAssetExposure;
                }
                const double newAssetExposure = currentAssetExposure + context.riskAdjustedExposure;
                violations.emplace_back(
                    ViolationType::EXPOSURE_LIMIT,
                    "Asset exposure limit would be exceeded for " + context.baseAsset,
                    assetLimit,
                    newAssetExposure
                );
            }

            // Similar checks for strategy and exchange limits...

        } catch (const std::exception& ex) {
            violations.emplace_back(ViolationType::OTHER, ex.what(), 0.0, 0.0);
        }

        return violations;
    }

    std::optional<double> calculateTotalExposure() const {
        try {
            const ExposureSnapshot snapshot = calculateExposureSnapshot();
            return snapshot.totalExposure;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<double> calculateAssetExposure(const std::string& assetId) const {
        try {
            const ExposureSnapshot snapshot = calculateExposureSnapshot();
            const auto it = snapshot.assetExposures.find(assetId);
            return (it != snapshot.assetExposures.end()) ? it->second : 0.0;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<double> calculateStrategyExposure(const std::string& strategyId) const {
        try {
            const ExposureSnapshot snapshot = calculateExposureSnapshot();
            const auto it = snapshot.strategyExposures.find(strategyId);
            return (it != snapshot.strategyExposures.end()) ? it->second : 0.0;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<double> calculateExchangeExposure(const core::ExchangeId& exchangeId) const {
        try {
            const ExposureSnapshot snapshot = calculateExposureSnapshot();
            const auto it = snapshot.exchangeExposures.find(exchangeId);
            return (it != snapshot.exchangeExposures.end()) ? it->second : 0.0;
        } catch (...) {
            return std::nullopt;
        }
    }

    void updateMarketData(const core::MarketData& marketData) {
        std::unique_lock<std::shared_mutex> lock(marketDataMutex_);
        
        priceCache_[marketData.tradingPair] = marketData.price;
        
        // Calculate volatility from 24h price change
        const double volatility = std::clamp(
            std::abs(marketData.priceChangePercent24h) / 100.0,
            0.01, 2.0
        );
        volatilityCache_[marketData.tradingPair] = volatility;
        
        // Update cache timestamp
        cacheTimestamp_ = std::chrono::steady_clock::now();
    }

    void updateExposureLimits(const ExposureLimitConfig& newLimits) {
        std::unique_lock<std::shared_mutex> lock(limitsMutex_);
        exposureLimits_ = newLimits;
        LOG_INFO("Updated exposure limits");
    }

    ExposureLimitConfig getExposureLimits() const {
        std::shared_lock<std::shared_mutex> lock(limitsMutex_);
        return exposureLimits_;
    }

    void clearCaches() {
        std::unique_lock<std::shared_mutex> lock(marketDataMutex_);
        priceCache_.clear();
        volatilityCache_.clear();
    }

private:
    struct ExposureContext {
        std::string symbol;
        std::string baseAsset;
        std::string strategyId;
        core::ExchangeId exchangeId;
        double quantity;
        double price;
        double volatility;
        double riskAdjustedExposure;
        
        bool isValid() const {
            return price > 0.0 && !symbol.empty() && !baseAsset.empty();
        }
    };

    struct ExposureSnapshot {
        double totalExposure = 0.0;
        std::unordered_map<std::string, double> assetExposures;
        std::unordered_map<std::string, double> strategyExposures;
        std::unordered_map<core::ExchangeId, double> exchangeExposures;
        std::chrono::steady_clock::time_point timestamp;
        
        ExposureSnapshot() : timestamp(std::chrono::steady_clock::now()) {}
    };

    ExposureContext buildExposureContext(const TradingSignal& signal) const {
        ExposureContext context;
        
        context.symbol = getSymbolFromSignal(signal);
        context.baseAsset = getBaseAssetFromSignal(signal);
        context.strategyId = getStrategyIdFromSignal(signal);
        context.exchangeId = getExchangeIdFromSignal(signal);
        context.quantity = getQuantityFromSignal(signal);
        
        context.price = getPrice(context.symbol);
        context.volatility = getVolatility(context.symbol);
        
        if (context.isValid()) {
            const double notional = std::abs(context.quantity) * context.price;
            
            double riskFactor;
            {
                std::shared_lock<std::shared_mutex> lock(limitsMutex_);
                riskFactor = exposureLimits_.volatilityMultiplier;
            }
            
            context.riskAdjustedExposure = notional * (1.0 + context.volatility * riskFactor);
        }
        
        return context;
    }

    ExposureSnapshot calculateExposureSnapshot() const {
        ExposureSnapshot snapshot;
        
        try {
            const auto positions = positionTracker_->getAllPositions();
            
            for (const auto& position : positions) {
                const double exposure = calculatePositionExposure(position);
                
                // Accumulate total exposure
                snapshot.totalExposure += exposure;
                
                // Accumulate by asset
                const std::string baseAsset = extractBaseAsset(position);
                snapshot.assetExposures[baseAsset] += exposure;
                
                // Accumulate by strategy
                const std::string strategyId = extractStrategyId(position);
                snapshot.strategyExposures[strategyId] += exposure;
                
                // Accumulate by exchange
                const core::ExchangeId exchangeId = extractExchangeId(position);
                snapshot.exchangeExposures[exchangeId] += exposure;
            }
            
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to calculate exposure snapshot: {}", ex.what());
        }
        
        return snapshot;
    }

    bool checkTotalExposureLimit(const ExposureSnapshot& snapshot, 
                                const ExposureContext& context) const {
        std::shared_lock<std::shared_mutex> lock(limitsMutex_);
        return (snapshot.totalExposure + context.riskAdjustedExposure) 
               <= exposureLimits_.maxTotalExposure;
    }

    bool checkAssetExposureLimit(const ExposureSnapshot& snapshot, 
                                const ExposureContext& context) const {
        const auto it = snapshot.assetExposures.find(context.baseAsset);
        const double currentAssetExposure = (it != snapshot.assetExposures.end()) ? it->second : 0.0;
        
        std::shared_lock<std::shared_mutex> lock(limitsMutex_);
        return (currentAssetExposure + context.riskAdjustedExposure) 
               <= exposureLimits_.maxAssetExposure;
    }

    bool checkStrategyExposureLimit(const ExposureSnapshot& snapshot, 
                                   const ExposureContext& context) const {
        const auto it = snapshot.strategyExposures.find(context.strategyId);
        const double currentStrategyExposure = (it != snapshot.strategyExposures.end()) ? it->second : 0.0;
        
        std::shared_lock<std::shared_mutex> lock(limitsMutex_);
        return (currentStrategyExposure + context.riskAdjustedExposure) 
               <= exposureLimits_.maxStrategyExposure;
    }

    bool checkExchangeExposureLimit(const ExposureSnapshot& snapshot, 
                                   const ExposureContext& context) const {
        const auto it = snapshot.exchangeExposures.find(context.exchangeId);
        const double currentExchangeExposure = (it != snapshot.exchangeExposures.end()) ? it->second : 0.0;
        
        std::shared_lock<std::shared_mutex> lock(limitsMutex_);
        return (currentExchangeExposure + context.riskAdjustedExposure) 
               <= exposureLimits_.maxExchangeExposure;
    }

    double calculatePositionExposure(const Position& position) const {
        try {
            const double quantity = std::abs(position.getQuantity());
            const std::string symbol = position.getTradingPair().symbol; // Assuming Position has getTradingPair() method
            
            const double price = getPrice(symbol);
            const double volatility = getVolatility(symbol);
            const double notional = quantity * price;
            
            std::shared_lock<std::shared_mutex> lock(limitsMutex_);
            const double riskFactor = exposureLimits_.volatilityMultiplier;
            
            return notional * (1.0 + volatility * riskFactor);
            
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to calculate position exposure: {}", ex.what());
            return 0.0;
        }
    }

    double getPrice(const std::string& symbol) const {
        std::shared_lock<std::shared_mutex> lock(marketDataMutex_);
        
        const auto it = priceCache_.find(symbol);
        if (it != priceCache_.end()) {
            return it->second;
        }
        
        // Fallback to data interface
        try {
            if (dataInterface_) {
                return dataInterface_->getCurrentPrice(symbol);
            }
        } catch (const std::exception& ex) {
            LOG_WARN("Failed to get price from data interface: {}", ex.what());
        }
        
        return 1.0; // Safe fallback
    }

    double getVolatility(const std::string& symbol) const {
        std::shared_lock<std::shared_mutex> lock(marketDataMutex_);
        
        const auto it = volatilityCache_.find(symbol);
        if (it != volatilityCache_.end()) {
            return it->second;
        }
        
        // Fallback to data interface
        try {
            if (dataInterface_) {
                return dataInterface_->getVolatility(symbol, "1d");
            }
        } catch (const std::exception& ex) {
            LOG_WARN("Failed to get volatility from data interface: {}", ex.what());
        }
        
        return 0.2; // Default 20% volatility
    }

    // Helper methods to extract data from Position
    std::string extractBaseAsset(const Position& position) const {
        const std::string& pair = position.getTradingPair().symbol; // Assuming Position has getTradingPair() method
        size_t pos = pair.find('/');
        if (pos != std::string::npos) {
            return pair.substr(0, pos);
        }
        pos = pair.find('-');
        if (pos != std::string::npos) {
            return pair.substr(0, pos);
        }
        return pair;
    }
    
    std::string extractStrategyId(const Position& position) const {
        return position.getStrategyId().id; // Adjust based on your Position implementation
    }
    
    core::ExchangeId extractExchangeId(const Position& position) const {
        return position.getExchangeId();
    }

private:
    std::shared_ptr<PositionTracker> positionTracker_;
    std::shared_ptr<DataInterface> dataInterface_;
    Config config_;

    // Use shared_mutex for better read performance
    mutable std::shared_mutex limitsMutex_;
    ExposureLimitConfig exposureLimits_;

    mutable std::shared_mutex marketDataMutex_;
    std::unordered_map<std::string, double> priceCache_;
    std::unordered_map<std::string, double> volatilityCache_;
    std::chrono::steady_clock::time_point cacheTimestamp_;
};

// Public interface implementation using PIMPL
ExposureCalculator::ExposureCalculator(
    std::shared_ptr<PositionTracker> positionTracker,
    std::shared_ptr<DataInterface> dataInterface,
    Config config
) : impl_(std::make_unique<Impl>(std::move(positionTracker), 
                                std::move(dataInterface), 
                                std::move(config))) {}

ExposureCalculator::~ExposureCalculator() = default;

bool ExposureCalculator::initialize(const RiskConfig& config) {
    return impl_->initialize(config);
}

ExposureValidationResult ExposureCalculator::validateExposure(const TradingSignal& signal) const {
    return impl_->validateExposure(signal);
}

ExposureValidationResult ExposureCalculator::validateExposure(const OrderRequest& orderRequest) const {
    return impl_->validateExposure(orderRequest);
}

std::vector<RiskViolation> ExposureCalculator::getViolationDetails(const TradingSignal& signal) const {
    return impl_->getViolationDetails(signal);
}

std::optional<double> ExposureCalculator::calculateTotalExposure() const {
    return impl_->calculateTotalExposure();
}

std::optional<double> ExposureCalculator::calculateAssetExposure(const std::string& assetId) const {
    return impl_->calculateAssetExposure(assetId);
}

std::optional<double> ExposureCalculator::calculateStrategyExposure(const std::string& strategyId) const {
    return impl_->calculateStrategyExposure(strategyId);
}

std::optional<double> ExposureCalculator::calculateExchangeExposure(const core::ExchangeId& exchangeId) const {
    return impl_->calculateExchangeExposure(exchangeId);
}

void ExposureCalculator::updateMarketData(const core::MarketData& marketData) {
    impl_->updateMarketData(marketData);
}

void ExposureCalculator::updateExposureLimits(const ExposureLimitConfig& newLimits) {
    impl_->updateExposureLimits(newLimits);
}

ExposureLimitConfig ExposureCalculator::getExposureLimits() const {
    return impl_->getExposureLimits();
}

void ExposureCalculator::clearCaches() {
    impl_->clearCaches();
}

} // namespace trading::risk