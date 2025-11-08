/**
 * @file signal_publisher.hpp
 * @brief Publisher for imbalance signals with proper risk integration
 */
#pragma once

#include "strategy/pattern/aggregation/signal_aggregator.hpp"
#include "strategy/types.hpp"
#include "common/event_bus.hpp"
#include <memory>
#include <functional>

// Forward declarations to avoid circular dependencies
namespace trading {
    namespace risk {
        class RiskManager;
        struct ValidationResult;
    }
    namespace core {
        class TradingSignal;
    }
}

namespace trading {
namespace strategy {
namespace publication {

// Forward declarations
struct SignalContext;

/**
 * @struct PublicationResult
 * @brief Result of signal publication attempt
 */
struct PublicationResult {
    bool published = false;
    bool riskValidationPassed = false;
    std::string reason;
    
    // Risk validation details (avoid incomplete type)
    std::string riskValidationDetails;
    
    // Event publishing details
    bool eventEmitted = false;
};

/**
 * @enum PublicationMode
 * @brief Different modes for signal publication
 */
enum class PublicationMode {
    DIRECT,           // Direct to EventBus (bypass risk - for testing only)
    RISK_VALIDATED,   // Through RiskManager validation (production)
    RISK_ADVISORY     // Log risk result but still publish (debugging)
};

/**
 * @class SignalPublisher
 * @brief Publishes imbalance signals with proper risk integration
 * 
 * This component handles the proper flow:
 * 1. Convert aggregated signals to TradingSignals
 * 2. Validate through RiskManager
 * 3. Publish events only if validation passes
 * 4. Provide audit trail and logging
 */
class SignalPublisher {
public:
    explicit SignalPublisher(PublicationMode mode = PublicationMode::RISK_VALIDATED);
    ~SignalPublisher() = default;
    
    /**
     * @brief Set RiskManager for validation (required for RISK_VALIDATED mode)
     */
    void setRiskManager(std::shared_ptr<trading::risk::RiskManager> riskManager);
    
    /**
     * @brief Set risk validation callback (alternative to direct RiskManager)
     * This avoids circular dependencies while still enabling risk validation
     */
    using RiskValidationCallback = std::function<bool(const core::TradingSignal&, std::string&)>;
    void setRiskValidationCallback(RiskValidationCallback callback);
    
    /**
     * @brief Publish an aggregated signal
     * @param signal The aggregated signal to publish
     * @param context Additional context (symbol, exchange, etc.)
     * @return Publication result with validation details
     */
    PublicationResult publishSignal(const aggregation::AggregatedSignal& signal,
                                   const SignalContext& context);
    
    /**
     * @brief Set publication mode
     */
    void setPublicationMode(PublicationMode mode);
    
    /**
     * @brief Get current publication mode
     */
    PublicationMode getPublicationMode() const;
    
    /**
     * @brief Configure minimum signal strength required for publication
     */
    void setMinPublicationStrength(double strength);
    
    /**
     * @brief Configure minimum confidence required for publication
     */
    void setMinPublicationConfidence(double confidence);
    
    /**
     * @brief Register callback for publication events
     */
    using PublicationCallback = std::function<void(const PublicationResult&, const TradingSignal&)>;
    void registerPublicationCallback(PublicationCallback callback);
    
private:
    PublicationMode mode_;
    std::shared_ptr<trading::risk::RiskManager> riskManager_;
    RiskValidationCallback riskValidationCallback_;
    double minPublicationStrength_ = 0.2;
    double minPublicationConfidence_ = 0.1;
    PublicationCallback callback_;
    
    // Conversion and validation
    TradingSignal convertToTradingSignal(const aggregation::AggregatedSignal& signal,
                                        const SignalContext& context) const;
    bool shouldPublish(const aggregation::AggregatedSignal& signal) const;
    PublicationResult publishDirect(const TradingSignal& signal);
    PublicationResult publishWithRiskValidation(const TradingSignal& signal);
    PublicationResult publishWithRiskAdvisory(const TradingSignal& signal);
    
    // Event creation and publishing
    void emitImbalanceEvent(const TradingSignal& signal, const aggregation::AggregatedSignal& original);
    std::string createImbalanceEventData(const TradingSignal& signal, 
                                        const aggregation::AggregatedSignal& original) const;
};

/**
 * @struct SignalContext
 * @brief Context information for signal publication
 */
struct SignalContext {
    core::ExchangeId exchangeId;
    core::Symbol symbol;
    core::Timestamp timestamp;
    int currentRegime = 0;
    double currentVolatility = 0.0;
    
    // Optional metadata
    std::string source = "ImbalanceDetectionEngine";
    std::map<std::string, double> metadata;
};

} // namespace publication
} // namespace strategy
} // namespace trading