/**
 * @file fat_finger_protection.cpp
 * @brief Fat finger protection implementation
 * 
 * Simple, reliable implementation using standard C++17 features.
 * 
 * @author Trading System Team  
 * @date May 28, 2025
 */

#include "risk/pretrade/fat_finger_protection.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace trading::risk {

// OrderPatternAnalyzer Implementation

OrderPatternAnalyzer::OrderPatternAnalyzer(size_t maxHistorySize) 
    : maxHistorySize_(maxHistorySize), statsDirty_(true) {
}

void OrderPatternAnalyzer::addSample(double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    samples_.push_back(value);
    if (samples_.size() > maxHistorySize_) {
        samples_.pop_front();
    }
    
    statsDirty_ = true;
    cachedStats_.reset();
}

OrderPatternAnalyzer::Statistics OrderPatternAnalyzer::getStatistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!statsDirty_ && cachedStats_) {
        return *cachedStats_;
    }
    
    cachedStats_ = calculateStatistics();
    statsDirty_ = false;
    
    return *cachedStats_;
}

double OrderPatternAnalyzer::getAnomalyScore(double value) const {
    const auto stats = getStatistics();
    
    if (!stats.isValid() || stats.stdDev == 0.0) {
        return 0.0;
    }
    
    return std::abs((value - stats.mean) / stats.stdDev);
}

bool OrderPatternAnalyzer::isOutlier(double value, double thresholdSigmas) const {
    return getAnomalyScore(value) > thresholdSigmas;
}

OrderPatternAnalyzer::Statistics OrderPatternAnalyzer::calculateStatistics() const {
    if (samples_.empty()) {
        return Statistics{};
    }
    
    Statistics stats;
    stats.sampleSize = samples_.size();
    
    // Convert to vector for algorithms
    std::vector<double> sortedSamples(samples_.begin(), samples_.end());
    std::sort(sortedSamples.begin(), sortedSamples.end());
    
    // Calculate mean
    stats.mean = std::accumulate(sortedSamples.begin(), sortedSamples.end(), 0.0) / sortedSamples.size();
    
    // Calculate median
    const size_t n = sortedSamples.size();
    if (n % 2 == 0) {
        stats.median = (sortedSamples[n/2 - 1] + sortedSamples[n/2]) / 2.0;
    } else {
        stats.median = sortedSamples[n/2];
    }
    
    // Calculate standard deviation
    double variance = 0.0;
    for (double sample : sortedSamples) {
        double diff = sample - stats.mean;
        variance += diff * diff;
    }
    variance /= (sortedSamples.size() - 1);
    stats.stdDev = std::sqrt(variance);
    
    return stats;
}

// FatFingerProtection Implementation

FatFingerProtection::FatFingerProtection(std::shared_ptr<DataInterface> dataInterface)
    : dataInterface_(std::move(dataInterface))
    , config_(Config())
    , totalValidations_(0)
    , rejectedOrders_(0)
{
}

FatFingerProtection::FatFingerProtection(
    std::shared_ptr<DataInterface> dataInterface,
    const Config& config
) 
    : dataInterface_(std::move(dataInterface))
    , config_(config)
    , totalValidations_(0)
    , rejectedOrders_(0)
{
    if (!config_.isValid()) {
        LOG_WARN("Invalid fat finger protection config provided, using defaults");
        config_ = Config();
    }
}

bool FatFingerProtection::initialize() {
    if (!dataInterface_) {
        LOG_ERROR("DataInterface is null");
        return false;
    }
    
    try {
        if (!dataInterface_->initialize()) {
            LOG_ERROR("Failed to initialize DataInterface");
            return false;
        }
        
        LOG_INFO("Fat finger protection initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Initialization failed: {}", e.what());
        return false;
    }
}

bool FatFingerProtection::checkFatFinger(const execution::TradingSignal& signal,
                                        std::vector<RiskViolation>& violations) {
    ++totalValidations_;
    
    // For signals, we only check size (no price available yet)
    // Prefer explicit resolved quantity if set on the signal. If not set, fall back
    // to targetSize (which may be fractional) and treat it as a relative size.
    double sigQty = signal.getQuantity();
    if (sigQty > 0.0) {
        return checkSizeAnomaly(signal.getTradingPair(), sigQty, violations);
    }
    return checkSizeAnomaly(signal.getTradingPair(), signal.getTargetSize(), violations);
}

bool FatFingerProtection::checkFatFinger(const OrderRequest& orderRequest,
                                        std::vector<RiskViolation>& violations) {
    ++totalValidations_;
    
    bool priceOk = true;
    bool sizeOk = true;
    bool notionalOk = true;
    
    // For limit orders, check price deviation
    if (orderRequest.type != OrderType::MARKET) {
        priceOk = checkPriceDeviation(orderRequest.tradingPair, orderRequest.price, violations);
    }
    
    // Check size anomaly
    sizeOk = checkSizeAnomaly(orderRequest.tradingPair, orderRequest.quantity, violations);
    
    // Check notional value
    double price = (orderRequest.type != OrderType::MARKET) ?
                  orderRequest.price : getCurrentMarketPrice(orderRequest.tradingPair);
    notionalOk = checkNotionalValue(orderRequest.tradingPair, price, orderRequest.quantity, violations);
    
    return priceOk && sizeOk && notionalOk;
}

void FatFingerProtection::recordOrder(const OrderRequest& order) {
    try {
        auto& analyzer = getOrCreateAnalyzer(order.tradingPair);
        analyzer.addSample(order.quantity);
        
        LOG_DEBUG("Recorded order for {}: size={}", order.tradingPair, order.quantity);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to record order for {}: {}", order.tradingPair, e.what());
    }
}

bool FatFingerProtection::updateConfig(const Config& newConfig) {
    if (!newConfig.isValid()) {
        LOG_ERROR("Attempted to set invalid fat finger protection config");
        return false;
    }
    
    config_ = newConfig;
    LOG_INFO("Updated fat finger protection config");
    return true;
}

bool FatFingerProtection::requiresAdditionalVerification(const core::Symbol& symbol,
                                                       double price,
                                                       double quantity) const {
    const double notionalValue = price * std::abs(quantity);
    return notionalValue > config_.notionalValueThreshold;
}

double FatFingerProtection::getTypicalOrderSize(const core::Symbol& symbol) const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    auto it = orderAnalyzers_.find(normalizeSymbol(symbol));
    if (it != orderAnalyzers_.end()) {
        auto stats = it->second->getStatistics();
        if (stats.isValid()) {
            return stats.median;
        }
    }
    
    return 1.0; // Default value if no history
}

std::optional<OrderPatternAnalyzer::Statistics> 
FatFingerProtection::getOrderStatistics(const core::Symbol& symbol) const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    auto it = orderAnalyzers_.find(normalizeSymbol(symbol));
    if (it != orderAnalyzers_.end()) {
        return it->second->getStatistics();
    }    
    return std::nullopt;
}

std::string FatFingerProtection::normalizeSymbol(const core::Symbol& symbol) const {
    std::string result = symbol.toString();
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

bool FatFingerProtection::checkPriceDeviation(const core::Symbol& symbol,
                                             double price,
                                             std::vector<RiskViolation>& violations) {
    try {
        double marketPrice = getCurrentMarketPrice(symbol);
        double deviation = std::abs((price - marketPrice) / marketPrice);
        
        if (deviation > config_.priceDeviationThreshold) {
            ++rejectedOrders_;
            RiskViolation violation;
            violation.type = ViolationType::PRICE_DEVIATION;
            violation.description = "Price deviates significantly from market price";
            violation.limitValue = config_.priceDeviationThreshold * 100;
            violation.actualValue = deviation * 100;
            violations.push_back(violation);
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get market price for {}: {}", symbol, e.what());
        
        RiskViolation violation;
        violation.type = ViolationType::OTHER;
        violation.description = "Could not verify price due to market data unavailability";
        violation.limitValue = 0;
        violation.actualValue = 0;
        violations.push_back(violation);
        return false;
    }
}

bool FatFingerProtection::checkSizeAnomaly(const core::Symbol& symbol,
                                          double quantity,
                                          std::vector<RiskViolation>& violations) {
    if (quantity <= 0.0) {
        ++rejectedOrders_;
        RiskViolation violation;
        violation.type = ViolationType::ORDER_SIZE_LIMIT;
        violation.description = "Invalid order size";
        violation.actualValue = quantity;
        violations.push_back(violation);
        return false;
    }
    
    const std::string normalizedSymbol = normalizeSymbol(symbol);
    
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    auto it = orderAnalyzers_.find(normalizedSymbol);
    if (it == orderAnalyzers_.end()) {
        LOG_DEBUG("No order history for {} - allowing order", symbol);
        return true;
    }
    
    const auto& analyzer = *it->second;
    const auto stats = analyzer.getStatistics();
    
    if (!stats.isValid() || stats.sampleSize < config_.minimumHistorySize) {
        LOG_DEBUG("Insufficient history for {} (samples: {}) - allowing order", 
                 symbol, stats.sampleSize);
        return true;
    }
    
    const double anomalyScore = analyzer.getAnomalyScore(quantity);
    
    if (anomalyScore > config_.sizeAnomalyThreshold) {
        ++rejectedOrders_;
        RiskViolation violation;
        violation.type = ViolationType::ORDER_SIZE_LIMIT;
        violation.description = "Order size significantly larger than typical";
        violation.limitValue = config_.sizeAnomalyThreshold;
        violation.actualValue = anomalyScore;
        violations.push_back(violation);
        return false;
    }
    
    return true;
}

bool FatFingerProtection::checkNotionalValue(const core::Symbol& symbol,
                                            double price,
                                            double quantity,
                                            std::vector<RiskViolation>& violations) {
    const double notionalValue = price * std::abs(quantity);
    
    if (notionalValue > config_.notionalValueThreshold) {
        if (config_.rejectHighNotionalOrders) {
            ++rejectedOrders_;
            RiskViolation violation;
            violation.type = ViolationType::OTHER;
            violation.description = "Order notional value exceeds maximum allowed";
            violation.limitValue = config_.notionalValueThreshold;
            violation.actualValue = notionalValue;
            violations.push_back(violation);
            return false;
        } else {
            LOG_WARN("High notional value order for {}: ${:.2f} (limit: ${:.2f}) - "
                    "requires additional verification",
                    symbol, notionalValue, config_.notionalValueThreshold);
        }
    }
    
    return true;
}

double FatFingerProtection::getCurrentMarketPrice(const core::Symbol& symbol) const {
    return dataInterface_->getCurrentPrice(symbol);
}

OrderPatternAnalyzer& FatFingerProtection::getOrCreateAnalyzer(const core::Symbol& symbol) {
    const std::string normalizedSymbol = normalizeSymbol(symbol);
    
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    auto it = orderAnalyzers_.find(normalizedSymbol);
    if (it == orderAnalyzers_.end()) {
        auto analyzer = std::make_unique<OrderPatternAnalyzer>();
        it = orderAnalyzers_.emplace(normalizedSymbol, std::move(analyzer)).first;
    }
    
    return *it->second;
}

} // namespace trading::risk