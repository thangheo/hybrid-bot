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
        // m_dataInterface = std::make_unique<MarketDataInterface>(m_config);
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
// getAllowedPositionSize
double PreTradeRiskFilter::getAllowedPositionSize(const TradingPair& tradingPair) const {
    // Query position limit checker for max position size
    double maxPosition = m_positionLimitChecker->getPositionLimit(tradingPair.base, "default_strategy");
    
    // Query exposure calculator for current exposure
    TradingSignal tempSignal;
    tempSignal.setId("temp_signal");
    tempSignal.setTradingPair(tradingPair.toString());
    tempSignal.setDirection(core::Direction::BUY);
    tempSignal.setTargetSize(0.0);
    tempSignal.setStrategyId(StrategyId("default_strategy"));
    double currentExposure = m_exposureCalculator->getCurrentExposure(tempSignal).value_or(0.0);
    
    // Get max asset exposure from config
    double maxAssetExposure = m_exposureCalculator->getExposureLimits().maxAssetExposure;
    
    // Calculate allowed size based on position and exposure limits
    double allowedByPosition = maxPosition; // Assuming no current position for simplicity
    double allowedByExposure = (maxAssetExposure > currentExposure) ? (maxAssetExposure - currentExposure) : 0.0;
    
    double allowedSize = std::min(allowedByPosition, allowedByExposure);
    
    LOG_DEBUG("Allowed position size for {}: byPosition={}, byExposure={}, final={}",
              tradingPair.toString(), allowedByPosition, allowedByExposure, allowedSize);
    
    return allowedSize;
}

ValidationResult PreTradeRiskFilter::validateSignal(const TradingSignal& signal) {
    // Start timing the validation
    auto startTime = std::chrono::high_resolution_clock::now();
    
    LOG_DEBUG("Validating signal: id={}, pair={}, direction={}, size={}",
              signal.getSignalId(), signal.getTradingPair(), 
              (signal.getDirection() == core::Direction::BUY ? "BUY" : "SELL"), 
              signal.getTargetSize());
    
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

void PreTradeRiskFilter::setMode(RiskManagerMode mode) {
    m_activeMode = mode;
    LOG_INFO("Pre-trade risk filter mode changed to: {}", static_cast<int>(mode));
}

RiskManagerMode PreTradeRiskFilter::getMode() const {
    return m_activeMode;
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
        // For OrderRequest, we need to create a temporary signal for violation details
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
        result.approved = false;
        LOG_INFO("Order {} rejected: failed exposure check", orderRequest.clientOrderId);
        for (const auto& v : exposureViolations) {
            LOG_INFO("  Exposure Violation: type={} desc='{}' limit={} actual={}'",
                     toString(v.type), v.description, v.limitValue, v.actualValue);
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