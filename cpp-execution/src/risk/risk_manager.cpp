/**
 * Implementation of simplified execution risk manager
 */

#include "risk_manager.hpp"
#include <algorithm>
#include <chrono>

namespace trading::risk {

ExecutionRiskManager::ExecutionRiskManager(const RiskConfig& config)
    : m_config(config) {
}

bool ExecutionRiskManager::initialize() {
    try {
        //create MarketDataInterface data interface - shared for both components
        auto dataInterface = std::make_shared<MarketDataInterface>();
        if (!dataInterface->initialize()) {
            return false;
        }
        
        // Create fat finger config from risk config
        FatFingerProtection::Config fatFingerConfig;
        fatFingerConfig.priceDeviationThreshold = m_config.getFatFingerConfig().maxPriceDeviation;
        fatFingerConfig.sizeAnomalyThreshold = 3.0; // 3 sigma default
        fatFingerConfig.notionalValueThreshold = m_config.getFatFingerConfig().notionalValueLimit;
        fatFingerConfig.rejectHighNotionalOrders = m_config.getFatFingerConfig().enforceAdditionalVerification;
        
        // Initialize only execution-focused components
        m_fatFingerProtection = std::make_unique<FatFingerProtection>(dataInterface, fatFingerConfig);
        if (!m_fatFingerProtection->initialize()) {
            return false;
        }

        m_orderValidator = std::make_unique<OrderValidator>(m_config, dataInterface);
        if (!m_orderValidator->initialize()) {
            return false;
        }

        m_circuitBreaker = std::make_unique<CircuitBreakerManager>();
        if (!m_circuitBreaker->initialize()) {
            return false;
        }
        
        // RiskValidationService requires a shared_ptr to RiskManager - skip for now
        // m_validationService = std::make_unique<RiskValidationService>(m_config, shared_from_this());
        // if (!m_validationService->initialize()) {
        //     return false;
        // }
        
        // Load symbol-specific execution limits
        this->loadExecutionLimits();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize ExecutionRiskManager: " << e.what() << std::endl;
        return false;
    }
}

ExecutionRiskResult ExecutionRiskManager::validateTradingIntent(
    const TradingIntent& intent,
    const MarketIntelligence& market_intel) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    ExecutionRiskResult result;
    
    // Emergency stop check (fastest possible)
    if (m_emergencyStop.load()) {
        result.rejection_reason = "Emergency stop active";
        return result;
    }
    
    // Circuit breaker check
    if (isCircuitBreakerTriggered(intent.symbol())) {
        result.rejection_reason = "Circuit breaker triggered for " + intent.symbol();
        return result;
    }
    
    // Get execution limits for symbol
    auto limits = getExecutionLimits(intent.symbol());
    
    // Fast execution-level validations only
    
    // 1. Order size validation
    if (intent.target_quantity() > limits.max_order_size) {
        result.rejection_reason = "Order size exceeds limit";
        result.adjusted_quantity = limits.max_order_size;
        // Could still approve with adjustment
    }
    
    // 2. Market impact validation
    if (intent.max_market_impact() > limits.max_market_impact) {
        result.rejection_reason = "Market impact limit exceeded";
        result.max_price_impact = limits.max_market_impact;
    }
    
    // 3. Price deviation check (fat finger protection)
    if (m_fatFingerProtection) {
        auto fat_finger_result = this->validateTradingIntent(intent, market_intel);

        if (!fat_finger_result.approved) {
            result.rejection_reason = fat_finger_result.rejection_reason;
            return result;
        }
    }
    
    // 4. Liquidity-based adjustments
    if (market_intel.liquidity_ratio() < 0.3) {
        // Reduce size in thin markets
        double liquidity_factor = market_intel.liquidity_ratio() / 0.3;
        if (result.adjusted_quantity.has_value()) {
            result.adjusted_quantity = result.adjusted_quantity.value() * liquidity_factor;
        } else {
            result.adjusted_quantity = intent.target_quantity() * liquidity_factor;
        }
        
        // Extend execution time in illiquid markets
        result.max_execution_time_ms = std::max(
            intent.max_execution_time_ms() * 2,
            60000  // Minimum 60 seconds
        );
    }
    
    // 5. Volatility-based adjustments
    if (market_intel.realized_volatility_1m() > 0.03) {
        // High volatility - be more conservative
        result.max_price_impact = std::min(
            limits.max_market_impact * 0.5,
            intent.max_market_impact()
        );
    }
    
    // Final approval decision
    if (result.rejection_reason.empty()) {
        result.approved = true;
    } else if (result.adjusted_quantity.has_value() || 
               result.max_price_impact.has_value() ||
               result.max_execution_time_ms.has_value()) {
        // Approve with modifications
        result.approved = true;
        result.rejection_reason = "Approved with risk adjustments";
    }
    
    // Record validation time
    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time);
    
    return result;
}

ExecutionRiskResult ExecutionRiskManager::validateOrder(
    const OrderRequest& order,
    const MarketIntelligence& market_intel) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    ExecutionRiskResult result;
    
    // Ultra-fast pre-execution checks
    
    // Emergency stop
    if (m_emergencyStop.load()) {
        result.rejection_reason = "Emergency stop";
        return result;
    }
    
    // Order format validation
    if (m_orderValidator) {
        std::vector<RiskViolation> violations;
        auto validation_result = m_orderValidator->validateOrder(order, violations);
        if (!validation_result) {
            result.rejection_reason = "Order validation failed";
            if (!violations.empty()) {
                result.rejection_reason += ": " + violations.front().description;
            }
            return result;
        }
    }
    
    // Real-time market impact check
    {
        std::lock_guard<std::mutex> lock(m_impactMutex);
        auto it = m_impactTrackers.find(order.tradingPair);
        if (it != m_impactTrackers.end()) {
            double current_impact_rate = it->second.getAverageImpact();
            auto limits = getExecutionLimits(order.tradingPair);
            
            if (current_impact_rate > limits.max_market_impact) {
                result.rejection_reason = "Exceeds real-time market impact threshold";
                return result;
            }
        }
    }
    
    result.approved = true;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time);
    
    return result;
}

void ExecutionRiskManager::updateMarketImpact(const std::string& symbol,
                                            double executed_quantity,
                                            double price_impact) {
    std::lock_guard<std::mutex> lock(m_impactMutex);
    m_impactTrackers[symbol].update(executed_quantity, price_impact);
    
    // Check if we should trigger circuit breaker
    auto& tracker = m_impactTrackers[symbol];
    auto limits = getExecutionLimits(symbol);
    
    if (tracker.getAverageImpact() > limits.cb_slippage_threshold) {
        if (m_circuitBreaker) {
            m_circuitBreaker->triggerForSymbol(symbol, "High market impact detected");
        }
    }
}

bool ExecutionRiskManager::isCircuitBreakerTriggered(const std::string& symbol) const {
    if (!m_circuitBreaker) return false;
    return m_circuitBreaker->isTriggeredForSymbol(symbol);
}

ExecutionLimits ExecutionRiskManager::getExecutionLimits(const std::string& symbol) const {
    auto it = m_symbolLimits.find(symbol);
    if (it != m_symbolLimits.end()) {
        return it->second;
    }
    
    // Return default limits if symbol not found
    return ExecutionLimits{};
}

void ExecutionRiskManager::loadExecutionLimits() {
    // Load execution limits from configuration - using default values since config method doesn't exist
    
    // Set default limits with reasonable values
    ExecutionLimits default_limits;
    default_limits.max_order_size = 1000000.0;  // 1M default
    default_limits.max_market_impact = 0.01;    // 1% max impact
    default_limits.max_price_deviation = 0.05;  // 5% price deviation
    default_limits.max_participation_rate = 0.1; // 10% participation rate
    default_limits.max_orders_per_second = 10;   // 10 orders/sec
    default_limits.allow_market_orders = true;
    default_limits.cb_slippage_threshold = 0.02; // 2% circuit breaker threshold
    default_limits.cb_rejection_rate_threshold = 0.5; // 50% rejection rate
    default_limits.cb_max_consecutive_rejections = 5;  // 5 consecutive rejections
    
    // TODO: Implement proper config loading once getExecutionLimitsConfig() method is available
    // For now, use default limits for all symbols
    
    // Store default limits for symbols not explicitly configured
    m_defaultLimits = default_limits;
}

} // namespace trading::risk