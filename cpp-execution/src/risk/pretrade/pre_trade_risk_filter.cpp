/**
 * Implementation of the pre-trade risk filter
 *
 * File: pre_trade_risk_filter.cpp
 * Created: 2025-04-29
 * Updated: 2025-05-28
 */

#include "risk/pretrade/pre_trade_risk_filter.hpp"
#include "risk/interfaces/market_data_interface.hpp"
#include "common/logger.hpp"

#include <chrono>
#include <optional>

namespace trading {
namespace risk {

// Helper to convert RiskManagerMode to readable string for logs
static std::string riskModeToString(RiskManagerMode mode) {
    switch (mode) {
        case RiskManagerMode::NORMAL: return "NORMAL";
        case RiskManagerMode::HEIGHTENED_RISK: return "HEIGHTENED_RISK";
        case RiskManagerMode::EMERGENCY: return "EMERGENCY";
        case RiskManagerMode::MAINTENANCE: return "MAINTENANCE";
        default: return "UNKNOWN";
    }
}

// // static std::unique_ptr<PreTradeRiskFilter> createPreTradeFilter(const RiskConfig& config);
// std::unique_ptr<PreTradeRiskFilter> createPreTradeFilter(const RiskConfig& config) {
//     auto filter = std::make_unique<PreTradeRiskFilter>(config);
//     if (!filter->initialize()) {
//         LOG_ERROR("Failed to initialize PreTradeRiskFilter");
//         return nullptr;
//     }
//     return filter;
// }


PreTradeRiskFilter::PreTradeRiskFilter(const RiskConfig& config)
    : m_config(config)
    , m_activeMode(RiskManagerMode::NORMAL)
{
    // Components will be initialized in initialize()
}
PreTradeRiskFilter::PreTradeRiskFilter(const RiskConfig& config, std::shared_ptr<DataInterface> dataInterface)
    : m_config(config)
    , m_activeMode(RiskManagerMode::NORMAL)
    , m_dataInterface(std::move(dataInterface))
{
    // Components will be initialized in initialize()
}

//isHealthy
bool PreTradeRiskFilter::isHealthy() const {
    return m_positionLimitChecker && m_positionLimitChecker->isHealthy() &&
              m_exposureCalculator && m_exposureCalculator->isHealthy() &&
              m_orderValidator && m_orderValidator->isHealthy() &&
              m_fatFingerProtection && m_fatFingerProtection->isHealthy();
}


// PreTradeRiskFilter::PreTradeRiskFilter(const RiskConfig& config, const PortfolioManager& portfolioManager)
//     : m_config(config)
//     , m_activeMode(RiskManagerMode::NORMAL)
//     , m_positionTracker(std::make_shared<PositionTracker>(portfolioManager))
// {
//     // Components will be initialized in initialize()
// }

PreTradeRiskFilter::~PreTradeRiskFilter() {
    // Destructor implementation
}
bool PreTradeRiskFilter::initialize() {
    try {
        LOG_INFO("Initializing pre-trade risk filter...");

        // Create shared dependencies with null DataInterface for now
        // TODO: Accept DataInterface from RiskManager constructor
        m_positionTracker = std::make_shared<PositionTracker>(m_config, nullptr);
        // m_dataInterface = std::make_shared<DataInterface>();
            // m_data_interface = std::make_unique<MarketDataInterface>(m_config);
            // use singleton pattern to create data interface
            // m_data_interface = MarketDataInterface::getInstance(m_config);
            if (!m_dataInterface) {
    }
        // use singleton pattern to create data interface
        // m_dataInterface = MarketDataInterface::getInstance(m_config);
        if (!m_dataInterface) {
            LOG_ERROR("Failed to create MarketDataInterface");
            return false;
        }

        
        // Initialize position tracker and data interface
        if (!m_positionTracker->initialize()) {
            LOG_ERROR("Failed to initialize position tracker");
            return false;
        }
        
        if (!m_dataInterface->initialize()) {
            LOG_ERROR("Failed to initialize data interface");
            return false;
        }
        
        // Create and initialize each component
        m_positionLimitChecker = std::make_unique<PositionLimitChecker>(m_config, m_positionTracker);
        if (!m_positionLimitChecker->initialize()) {
            LOG_ERROR("Failed to initialize position limit checker");
            return false;
        }
        
        // Initialize exposure calculator with the new interface
        m_exposureCalculator = std::make_unique<ExposureCalculator>(m_positionTracker, m_dataInterface);
        if (!m_exposureCalculator->initialize(m_config)) {
            LOG_ERROR("Failed to initialize exposure calculator");
            return false;
        }
        
        m_orderValidator = std::make_unique<OrderValidator>(m_config, m_dataInterface);
        if (!m_orderValidator->initialize()) {
            LOG_ERROR("Failed to initialize order validator");
            return false;
        }
        
        m_fatFingerProtection = std::make_unique<FatFingerProtection>(m_dataInterface);
        if (!m_fatFingerProtection->initialize()) {
            LOG_ERROR("Failed to initialize fat finger protection");
            return false;
        }
        
        LOG_INFO("Pre-trade risk filter initialized successfully");
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to initialize pre-trade risk filter: {}", ex.what());
        return false;
    }
}

bool PreTradeRiskFilter::shutdown() {
    LOG_INFO("Shutting down PreTradeRiskFilter components...");

    if (m_positionLimitChecker) {
        m_positionLimitChecker.reset();
        LOG_INFO("PositionLimitChecker shut down");
    }
    if (m_exposureCalculator) {
        m_exposureCalculator.reset();
        LOG_INFO("ExposureCalculator shut down");
    }
    if (m_orderValidator) {
        m_orderValidator.reset();
        LOG_INFO("OrderValidator shut down");
    }
    if (m_fatFingerProtection) {
        m_fatFingerProtection.reset();
        LOG_INFO("FatFingerProtection shut down");
    }
    if (m_positionTracker) {
        m_positionTracker->shutdown();
        m_positionTracker.reset();
        LOG_INFO("PositionTracker shut down");
    }
    if (m_dataInterface) {
        m_dataInterface->shutdown();
        m_dataInterface.reset();
        LOG_INFO("DataInterface shut down");
    }

    LOG_INFO("PreTradeRiskFilter shutdown complete");
    return true;
}

// getAllowedPositionSize
double PreTradeRiskFilter::getAllowedPositionSize(const TradingPair& tradingPair) const {
    // Return allowed position size expressed as USD notional.
    // Compute: allowed = min(assetLimitUsd - currentExposureUsd, exposureLimit.maxAssetExposure - currentExposureUsd)
    // All values are USD.
    std::string baseAsset = tradingPair.base;

    double assetLimitUsd = m_positionLimitChecker->getPositionLimit(baseAsset, "default_strategy");

    // Query exposure calculator for current exposure in USD for this asset
    TradingSignal tempSignal;
    tempSignal.setId("temp_signal");
    tempSignal.setTradingPair(tradingPair.toString());
    tempSignal.setDirection(core::Direction::BUY);
    tempSignal.setTargetSize(0.0);
    tempSignal.setStrategyId(StrategyId("default_strategy"));
    double currentExposureUsd = m_exposureCalculator->getCurrentExposure(tempSignal).value_or(0.0);

    // Get max asset exposure from exposure limits (USD)
    double maxAssetExposure = m_exposureCalculator->getExposureLimits().maxAssetExposure;

    double allowedByPositionUsd = std::max(0.0, assetLimitUsd - currentExposureUsd);
    double allowedByExposureUsd = std::max(0.0, maxAssetExposure - currentExposureUsd);

    double allowedUsd = std::min(allowedByPositionUsd, allowedByExposureUsd);

    LOG_DEBUG("Allowed USD position for {}: assetLimitUsd={} currentExposureUsd={} maxAssetExposure={} finalAllowedUsd={}",
              tradingPair.toString(), assetLimitUsd, currentExposureUsd, maxAssetExposure, allowedUsd);

    return allowedUsd;
}

ValidationResult PreTradeRiskFilter::validateSignal(const TradingSignal& signal) {
    // Start timing the validation
    auto startTime = std::chrono::high_resolution_clock::now();

    LOG_DEBUG("Validating signal: id={}, pair={}, direction={}, size={}, quantity={}",
              signal.getSignalId(), signal.getTradingPair(),
              (signal.getDirection() == core::Direction::BUY ? "BUY" : "SELL"),
                signal.getTargetSize(), signal.getQuantity());

    // Check if we're in emergency mode - reject all signals
    if (m_activeMode == RiskManagerMode::EMERGENCY) {
        ValidationResult result;
        result.signalId = signal.getSignalId();
        result.approved = false;
        result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string desc = "Risk system is in emergency mode, all signals rejected";
        RiskViolation violation(
            ViolationType::OTHER,
            desc,
            0.0,
            0.0
        );
        result.violations.push_back(violation);

        LOG_WARN("Signal {} rejected (mode={}): {} pair={} dir={} size={}",
                 signal.getSignalId(), riskModeToString(m_activeMode), desc,
                 signal.getTradingPair().toString(),
                 (signal.getDirection() == core::Direction::BUY ? "BUY" : "SELL"),
                 signal.getTargetSize());
        return result;
    }
    
    // Perform validation checks with normal or heightened thresholds
    return performAllChecks(signal);
}

ValidationResult PreTradeRiskFilter::validateOrder(const OrderRequest& orderRequest) {
    // Start timing the validation
    auto startTime = std::chrono::high_resolution_clock::now();
    
    LOG_DEBUG("Validating order: id={}, pair={}, side={}, type={}, quantity={}",
              orderRequest.clientOrderId, orderRequest.tradingPair,
              (orderRequest.side == core::Direction::BUY ? "BUY" : "SELL"),
              toString(orderRequest.type), orderRequest.quantity);
    
    // Check if we're in emergency mode - reject all orders
    if (m_activeMode == RiskManagerMode::EMERGENCY) {
        ValidationResult result;
        result.signalId = orderRequest.clientOrderId;
        result.approved = false;
        result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string desc = "Risk system is in emergency mode, all orders rejected";
        RiskViolation violation(
            ViolationType::OTHER,
            desc,
            0.0,
            0.0
        );
        result.violations.push_back(violation);

        LOG_WARN("Order {} rejected (mode={}): {} pair={} side={} qty={}",
                 orderRequest.clientOrderId, riskModeToString(m_activeMode), desc,
                 orderRequest.tradingPair.toString(),
                 (orderRequest.side == core::Direction::BUY ? "BUY" : "SELL"),
                 orderRequest.quantity);
        return result;
    }
    
    // If the order is a limit order and price is zero, attempt to fill in a market price
    OrderRequest mutableOrder = orderRequest; // make a local, mutable copy
    if (mutableOrder.type == OrderType::LIMIT && mutableOrder.price == 0) {
        double marketPrice = m_dataInterface->getCurrentPrice(
            mutableOrder.tradingPair,
            std::optional<std::string>(mutableOrder.exchangeId.toString())
        );
        if (marketPrice > 0) {
            mutableOrder.price = marketPrice;
            LOG_INFO("Order {}: Limit price was zero, set to market price {}", mutableOrder.clientOrderId, marketPrice);
        } else {
            ValidationResult result;
            result.signalId = mutableOrder.clientOrderId;
            result.approved = false;
            result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            RiskViolation violation(
                ViolationType::PRICE_DEVIATION,
                "Limit price is zero and market price unavailable",
                0.0,
                mutableOrder.price
            );
            result.violations.push_back(violation);
            LOG_WARN("Order {} rejected: limit price missing and market price unavailable", mutableOrder.clientOrderId);
            return result;
        }
    }

    // Perform validation checks with normal or heightened thresholds using the (possibly modified) order
    return performAllChecks(mutableOrder);
}

bool PreTradeRiskFilter::updateRiskParameters(const RiskParameters& params) {
    try {
        LOG_INFO("Updating risk parameters for {}/{}/{}",
                params.exchangeId.toString(), params.tradingPair.toString(), params.strategyId);

        // Update position limits if provided
        if (params.maxPositionSize > 0) {
            // std::string baseAsset = getBaseAsset(params.tradingPair.toString());
            std::string baseAsset = params.tradingPair.base;
            if (!m_positionLimitChecker->updatePositionLimit(
                    baseAsset, params.strategyId, params.maxPositionSize)) {
                LOG_ERROR("Failed to update position limit");
                return false;
            }
        }
        
        // Update exposure limits if provided
        if (params.maxExposure > 0) {
            ExposureLimitConfig exposureLimits = m_exposureCalculator->getExposureLimits();
            
            // Update relevant limits based on what's provided
            std::string baseAsset = params.tradingPair.base;
            exposureLimits.maxAssetExposure = params.maxExposure;
            
            m_exposureCalculator->updateExposureLimits(exposureLimits);
        }
        
        // Update order size limits if provided
        if (params.minOrderSize > 0) {
            if (!m_orderValidator->updateMinOrderSize(
                    params.exchangeId, params.tradingPair.toString(), params.minOrderSize)) {
                LOG_ERROR("Failed to update minimum order size");
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to update risk parameters: {}", ex.what());
        return false;
    }
}

RiskParameters PreTradeRiskFilter::getRiskParameters(const core::ExchangeId& exchangeId,
                                                   const TradingPair& tradingPair,
                                                   const std::string& strategyId) const {
    RiskParameters params;
    params.exchangeId = exchangeId;
    params.tradingPair = tradingPair;
    params.strategyId = strategyId;
    
    // Get position limits
    std::string baseAsset = tradingPair.base;
    params.maxPositionSize = m_positionLimitChecker->getPositionLimit(baseAsset, strategyId);
    
    // Get exposure limits
    const ExposureLimitConfig& exposureLimits = m_exposureCalculator->getExposureLimits();
    params.maxExposure = exposureLimits.maxAssetExposure;
    
    return params;
}


ValidationResult PreTradeRiskFilter::performAllChecks(const TradingSignal& signal) {
    ValidationResult result;
    result.signalId = signal.getSignalId();
    result.approved = true;
    result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Check basic validation first
    if (!passesBasicValidation(signal, result.violations)) {
        result.approved = false;
        LOG_WARN("Signal {} rejected: failed basic validation", signal.getSignalId());
        // Detailed violations are noisy under INFO; keep them at DEBUG level
        for (const auto& v : result.violations) {
            LOG_DEBUG("  Violation: type={} desc='{}' limit={} actual={}",
                      toString(v.type), v.description, v.limitValue, v.actualValue);
        }
        return result;
    }
    
    // Check position limits
    if (!m_positionLimitChecker->checkPositionLimit(signal, result.violations)) {
        result.approved = false;
        LOG_WARN("Signal {} rejected: failed position limit check", signal.getSignalId());
        for (const auto& v : result.violations) {
            LOG_DEBUG("  Violation: type={} desc='{}' limit={} actual={}",
                      toString(v.type), v.description, v.limitValue, v.actualValue);
        }
    }
    
    // Check exposure limits
    auto exposureResult = m_exposureCalculator->validateExposure(signal);
    if (exposureResult != ExposureValidationResult::APPROVED) {
        // For BUYs, instead of outright rejection, attempt to cap the requested size
        if (signal.getDirection() == core::Direction::BUY) {
            // Compute allowed size based on current exposure and limits
            // We'll use getAllowedPositionSize which factors asset exposure and position limits
            double allowedSize = getAllowedPositionSize(signal.getTradingPair());
            // Ensure we don't increase size accidentally
            allowedSize = std::min(allowedSize, signal.getTargetSize());

            auto exposureViolations = m_exposureCalculator->getViolationDetails(signal);
            result.violations.insert(result.violations.end(), 
                                    exposureViolations.begin(), 
                                    exposureViolations.end());

            if (allowedSize > 0.0) {
                // Convert allowed USD to signal units. signal.getTargetSize() is a fraction (<=1) or absolute qty (>1)
                double currentPrice = m_positionTracker->getCachedPrice(signal.getTradingPair());
                if (currentPrice <= 0.0) {
                    result.approved = false;
                    RiskViolation v(ViolationType::PRICE_DEVIATION,
                                    "Price unavailable for capping BUY; rejecting signal",
                                    0.0, 0.0);
                    result.violations.push_back(v);
                    LOG_WARN("Signal {} rejected: price unavailable for capping", signal.getSignalId());
                } else {
                    double cappedQty = allowedSize / currentPrice; // quantity in asset units
                    double adjustedSize = signal.getTargetSize();
                    if (signal.getTargetSize() <= 1.0) {
                        // interpret targetSize as fraction of some nominal; map fraction = qty / (qty + currentPosition)
                        // We don't have the denominator here; instead, return capped quantity as absolute quantity in adjustedSize
                        adjustedSize = cappedQty;
                    } else {
                        adjustedSize = std::min(signal.getTargetSize(), cappedQty);
                    }

                    result.approved = true;
                    result.adjustedSize = adjustedSize;
                    LOG_WARN("Signal {} exposure would exceed limits; capping BUY from {} to {} (qty {})",
                             signal.getSignalId(), signal.getTargetSize(), adjustedSize, cappedQty);
                }
            } else {
                result.approved = false;
                LOG_WARN("Signal {} rejected: failed exposure check and no allowed BUY size available", signal.getSignalId());
            }

            for (const auto& v : exposureViolations) {
                LOG_DEBUG("  Exposure Violation: type={} desc='{}' limit={} actual={}",
                          toString(v.type), v.description, v.limitValue, v.actualValue);
            }
        } else {
            // Non-BUY directions remain rejected
            auto exposureViolations = m_exposureCalculator->getViolationDetails(signal);
            result.violations.insert(result.violations.end(), 
                                    exposureViolations.begin(), 
                                    exposureViolations.end());
            result.approved = false;
            LOG_WARN("Signal {} rejected: failed exposure check", signal.getSignalId());
            for (const auto& v : exposureViolations) {
                LOG_DEBUG("  Exposure Violation: type={} desc='{}' limit={} actual={}",
                          toString(v.type), v.description, v.limitValue, v.actualValue);
            }
        }
    }
    
    // Check order validation
    if (!m_orderValidator->validateOrder(signal, result.violations)) {
        result.approved = false;
        //print detailed violations at debug level
        for (const auto& v : result.violations) {
            LOG_DEBUG("  Violation: type={} desc='{}' limit={} actual={}",
                      toString(v.type), v.description, v.limitValue, v.actualValue);
        }
        // LOG_INFO("Signal {} rejected: failed order validation", signal.getSignalId());
    }
    
    // Check fat finger protection
    if (!m_fatFingerProtection->checkFatFinger(signal, result.violations)) {
        result.approved = false;
        LOG_INFO("Signal {} rejected: failed fat finger check", signal.getSignalId());
    }
    
    // If approved, adjust size if needed (e.g., due to risk constraints)
    if (result.approved) {
        result.adjustedSize = signal.getTargetSize();
        LOG_INFO("Signal {} approved", signal.getSignalId());
    }
    
    return result;
}

ValidationResult PreTradeRiskFilter::performAllChecks(const OrderRequest& orderRequest) {
    ValidationResult result;
    result.signalId = orderRequest.clientOrderId;
    result.approved = true;
    result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Check basic validation first
    if (!passesBasicValidation(orderRequest, result.violations)) {
        result.approved = false;
        LOG_INFO("Order {} rejected: failed basic validation", orderRequest.clientOrderId);
        // Log detailed violations for easier debugging
        for (const auto& v : result.violations) {
            LOG_INFO("  Violation: type={} desc='{}' limit={} actual={}",
                     toString(v.type), v.description, v.limitValue, v.actualValue);
        }
        return result;
    }
    
    // Check position limits
    if (!m_positionLimitChecker->checkPositionLimit(orderRequest, result.violations)) {
        result.approved = false;
        LOG_INFO("Order {} rejected: failed position limit check", orderRequest.clientOrderId);
    }
    
    // Check exposure limits
    auto exposureResult = m_exposureCalculator->validateExposure(orderRequest);
    if (exposureResult != ExposureValidationResult::APPROVED) {
        // For BUY orders, try to cap the quantity instead of rejecting
        if (orderRequest.side == core::Direction::BUY) {
            TradingSignal tempSignal;
            tempSignal.id = orderRequest.clientOrderId;
            tempSignal.setTradingPair(orderRequest.tradingPair.toString());
            tempSignal.direction = orderRequest.side;
            tempSignal.targetSize = orderRequest.quantity;
            tempSignal.exchangeId = orderRequest.exchangeId;

            auto exposureViolations = m_exposureCalculator->getViolationDetails(tempSignal);
            result.violations.insert(result.violations.end(), 
                                    exposureViolations.begin(), 
                                    exposureViolations.end());

            double allowedSize = getAllowedPositionSize(orderRequest.tradingPair);
            allowedSize = std::min(allowedSize, orderRequest.quantity);

            if (allowedSize > 0.0) {
                // allowedSize is USD; convert to asset qty using current price
                double currentPrice = m_positionTracker->getCachedPrice(orderRequest.tradingPair);
                if (currentPrice <= 0.0) {
                    result.approved = false;
                    RiskViolation v(ViolationType::PRICE_DEVIATION,
                                    "Price unavailable for capping BUY order; rejecting order",
                                    0.0, 0.0);
                    result.violations.push_back(v);
                    LOG_INFO("Order {} rejected: price unavailable for capping", orderRequest.clientOrderId);
                } else {
                    double cappedQty = allowedSize / currentPrice;
                    double adjustedQty = std::min(allowedSize, orderRequest.quantity);
                    adjustedQty = std::min(orderRequest.quantity, cappedQty);
                    result.approved = true;
                    result.adjustedSize = adjustedQty;
                    LOG_INFO("Order {} exposure would exceed limits; capping BUY qty from {} to {} (cappedQty={})",
                             orderRequest.clientOrderId, orderRequest.quantity, adjustedQty, cappedQty);
                }
            } else {
                result.approved = false;
                LOG_INFO("Order {} rejected: failed exposure check and no allowed BUY quantity available", orderRequest.clientOrderId);
            }

            for (const auto& v : exposureViolations) {
                LOG_INFO("  Exposure Violation: type={} desc='{}' limit={} actual={}'",
                         toString(v.type), v.description, v.limitValue, v.actualValue);
            }
        } else {
            // Non-BUY orders remain rejected
            TradingSignal tempSignal;
            tempSignal.id = orderRequest.clientOrderId;
            tempSignal.setTradingPair(orderRequest.tradingPair.toString());
            tempSignal.direction = orderRequest.side;
            tempSignal.targetSize = orderRequest.quantity;
            tempSignal.exchangeId = orderRequest.exchangeId;

            auto exposureViolations = m_exposureCalculator->getViolationDetails(tempSignal);
            result.violations.insert(result.violations.end(), 
                                    exposureViolations.begin(), 
                                    exposureViolations.end());

            bool handledByCapping = false;
            if (orderRequest.side == core::Direction::BUY) {
                double allowedSize = getAllowedPositionSize(orderRequest.tradingPair);
                if (allowedSize > 0.0 && allowedSize < orderRequest.quantity) {
                    result.approved = true;
                    result.adjustedSize = allowedSize;
                    handledByCapping = true;
                    LOG_INFO("Order {} exposure would exceed limits; capping BUY quantity from {} to {}",
                             orderRequest.clientOrderId, orderRequest.quantity, allowedSize);
                }
            } else if (orderRequest.side == core::Direction::SELL) {
                try {
                    auto posOpt = m_positionTracker->getPosition(orderRequest.exchangeId, orderRequest.tradingPair);
                    double available = 0.0;
                    if (posOpt.has_value()) {
                        double q = posOpt->getQuantity();
                        if (q > 0.0) available = std::abs(q);
                    }
                    if (available > 0.0 && available < orderRequest.quantity) {
                        result.approved = true;
                        result.adjustedSize = available;
                        handledByCapping = true;
                        LOG_INFO("Order {} would SELL more than available; capping SELL quantity from {} to {}",
                                 orderRequest.clientOrderId, orderRequest.quantity, available);
                    }
                } catch (const std::exception& ex) {
                    LOG_WARN("Failed to obtain current position for SELL capping: {}", ex.what());
                }
            }

            if (!handledByCapping) {
                result.approved = false;
                LOG_INFO("Order {} rejected: failed exposure check", orderRequest.clientOrderId);
                for (const auto& v : exposureViolations) {
                    LOG_INFO("  Exposure Violation: type={} desc='{}' limit={} actual={}'",
                             toString(v.type), v.description, v.limitValue, v.actualValue);
                }
            }
        }
    }
    
    // Check order validation
    if (!m_orderValidator->validateOrder(orderRequest, result.violations)) {
        result.approved = false;
        // Print detailed violations at info level for orders
        for (const auto& v : result.violations) {
            LOG_INFO("  Violation: type={} desc='{}' limit={} actual={}'",
                     toString(v.type), v.description, v.limitValue, v.actualValue);
        }
        // LOG_INFO("Order {} rejected: failed order validation", orderRequest.clientOrderId);
    }
    
    // Check fat finger protection
    if (!m_fatFingerProtection->checkFatFinger(orderRequest, result.violations)) {
        result.approved = false;
        LOG_INFO("Order {} rejected: failed fat finger check", orderRequest.clientOrderId);
    }
    
    // If approved, adjust size if needed (e.g., due to risk constraints)
    if (result.approved) {
        result.adjustedSize = orderRequest.quantity;
        LOG_INFO("Order {} approved", orderRequest.clientOrderId);
    }
    
    return result;
}

bool PreTradeRiskFilter::passesBasicValidation(const TradingSignal& signal, 
                                              std::vector<RiskViolation>& violations) {
    // Check for missing required fields
    if (signal.getSignalId().empty()) {
        LOG_INFO("Signal rejected: missing Signal ID");
        RiskViolation violation(
            ViolationType::OTHER,
            "Signal ID is missing",
            0.0,
            0.0
        );
        violations.push_back(violation);
        return false;
    }

    if (signal.getTradingPair().isEmpty()) {
        LOG_INFO("Signal {} rejected: missing Trading Pair", signal.getSignalId());
        RiskViolation violation(
            ViolationType::OTHER,
            "Trading pair is missing",
            0.0,
            0.0
        );
        violations.push_back(violation);
        return false;
    }
    
    // Check for invalid values
    if (signal.getTargetSize() <= 0) {
        LOG_INFO("Signal {} rejected: non-positive target size", signal.getSignalId());
        RiskViolation violation(
            ViolationType::ORDER_SIZE_LIMIT,
            "Target size must be positive",
            0.0,
            signal.getTargetSize()
        );
        violations.push_back(violation);
        return false;
    }
    
    // Check for valid direction
    if (signal.getDirection() != core::Direction::BUY && 
        signal.getDirection() != core::Direction::SELL) {
        LOG_INFO("Signal {} rejected: invalid direction", signal.getSignalId());
        RiskViolation violation(
            ViolationType::OTHER,
            "Invalid direction value",
            0.0,
            0.0
        );
        violations.push_back(violation);
        return false;
    }
    
    // All basic validations passed
    return true;
}

bool PreTradeRiskFilter::passesBasicValidation(const OrderRequest& orderRequest, 
                                              std::vector<RiskViolation>& violations) {
    // Check for missing required fields
    if (orderRequest.clientOrderId.empty()) {
        RiskViolation violation(
            ViolationType::OTHER,
            "Client order ID is missing",
            0.0,
            0.0
        );
        violations.push_back(violation);
        return false;
    }

    if (orderRequest.tradingPair.isEmpty()) {
        RiskViolation violation(
            ViolationType::OTHER,
            "Trading pair is missing",
            0.0,
            0.0
        );
        violations.push_back(violation);
        return false;
    }
    
    // Check for invalid values
    if (orderRequest.quantity <= 0) {
        RiskViolation violation(
            ViolationType::ORDER_SIZE_LIMIT,
            "Order quantity must be positive",
            0.0,
            orderRequest.quantity
        );
        violations.push_back(violation);
        return false;
    }

    // For limit orders, we do not modify the incoming OrderRequest here.
    // Price defaulting (if desired) is handled earlier in validateOrder where a mutable
    // copy of the OrderRequest can be populated from market data.
    if (orderRequest.type == OrderType::LIMIT && orderRequest.price == 0) {
        RiskViolation violation(
            ViolationType::PRICE_DEVIATION,
            "Limit price is zero",
            0.0,
            orderRequest.price
        );
        violations.push_back(violation);
        return false;
    }

    // Check price for limit orders
    if (orderRequest.type == OrderType::LIMIT && orderRequest.price <= 0) {
        RiskViolation violation(
            ViolationType::PRICE_DEVIATION,
            "Limit price must be positive",
            0.0,
            orderRequest.price
        );
        violations.push_back(violation);
        return false;
    }
    
    // Check for valid side
    if (orderRequest.side != core::Direction::BUY && 
        orderRequest.side != core::Direction::SELL) {
        RiskViolation violation(
            ViolationType::OTHER,
            "Invalid order side value",
            0.0,
            0.0
        );
        violations.push_back(violation);
        return false;
    }
    
    // All basic validations passed
    return true;
}

std::string PreTradeRiskFilter::getBaseAsset(const core::Symbol& tradingPair) const {
    auto symbol = tradingPair;
    return symbol.base;
}

} // namespace risk
} // namespace trading