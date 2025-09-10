/**
 * Implementation of the pre-trade risk filter
 *
 * File: pre_trade_risk_filter.cpp
 * Created: 2025-04-29
 * Updated: 2025-05-28
 */

#include "risk/pretrade/pre_trade_risk_filter.hpp"
#include "common/logger.hpp"

#include <chrono>

namespace trading {
namespace risk {

PreTradeRiskFilter::PreTradeRiskFilter(const RiskConfig& config)
    : m_config(config)
    , m_activeMode(RiskManagerMode::NORMAL)
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
        auto dataInterface = createDataInterface(); 
        if (!dataInterface) {
            LOG_ERROR("Failed to create data interface");
            return false;
        }

        // Create shared dependencies
        m_positionTracker = std::make_shared<PositionTracker>(m_config, nullptr);
        // m_dataInterface = std::make_shared<DataInterface>();
        m_dataInterface = std::shared_ptr<DataInterface>(dataInterface.release());

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
        
        RiskViolation violation(
            ViolationType::OTHER,
            "Risk system is in emergency mode, all signals rejected",
            0.0,
            0.0
        );
        result.violations.push_back(violation);
        
        LOG_WARN("Signal {} rejected: risk system in emergency mode", signal.getSignalId());
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
        
        RiskViolation violation(
            ViolationType::OTHER,
            "Risk system is in emergency mode, all orders rejected",
            0.0,
            0.0
        );
        result.violations.push_back(violation);
        
        LOG_WARN("Order {} rejected: risk system in emergency mode", orderRequest.clientOrderId);
        return result;
    }
    
    // Perform validation checks with normal or heightened thresholds
    return performAllChecks(orderRequest);
}

bool PreTradeRiskFilter::updateRiskParameters(const RiskParameters& params) {
    try {
        LOG_INFO("Updating risk parameters for {}/{}/{}",
                params.exchangeId.toString(), params.tradingPair.toString(), params.strategyId);

        // Update position limits if provided
        if (params.maxPositionSize > 0) {
            std::string baseAsset = getBaseAsset(params.tradingPair.toString());
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
            std::string baseAsset = getBaseAsset(params.tradingPair.toString());
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
        LOG_INFO("Signal {} rejected: failed basic validation", signal.getSignalId());
        return result;
    }
    
    // Check position limits
    if (!m_positionLimitChecker->checkPositionLimit(signal, result.violations)) {
        result.approved = false;
        LOG_INFO("Signal {} rejected: failed position limit check", signal.getSignalId());
    }
    
    // Check exposure limits
    auto exposureResult = m_exposureCalculator->validateExposure(signal);
    if (exposureResult != ExposureValidationResult::APPROVED) {
        auto exposureViolations = m_exposureCalculator->getViolationDetails(signal);
        result.violations.insert(result.violations.end(), 
                                exposureViolations.begin(), 
                                exposureViolations.end());
        result.approved = false;
        LOG_INFO("Signal {} rejected: failed exposure check", signal.getSignalId());
    }
    
    // Check order validation
    if (!m_orderValidator->validateOrder(signal, result.violations)) {
        result.approved = false;
        LOG_INFO("Signal {} rejected: failed order validation", signal.getSignalId());
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
    }
    
    // Check order validation
    if (!m_orderValidator->validateOrder(orderRequest, result.violations)) {
        result.approved = false;
        LOG_INFO("Order {} rejected: failed order validation", orderRequest.clientOrderId);
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

// std::string PreTradeRiskFilter::getBaseAsset(const std::string& tradingPairStr) const {
//     // Extract base asset from trading pair (e.g., "BTC/USDT" -> "BTC")
//     auto symbol = core::Symbol::fromString(tradingPairStr);
//     return symbol.base;
// }

} // namespace risk
} // namespace trading