/**
 * Implementation of the risk validation service
 *
 * File: risk_validation_service.cpp
 * Created: 2025-04-29
 */

 #include "service/risk_validation_service.hpp"
 #include "risk_manager.hpp"
 
 #include <chrono>
 #include <future>
 #include <sstream>
#include <memory>
 
 namespace trading {
 namespace risk {
 
 RiskValidationService::RiskValidationService(const RiskConfig& config, 
                                            std::shared_ptr<RiskManager> riskManager)
     : m_riskManager(riskManager)
     , m_config(config)
     , m_validationTimeoutMs(500) // Default 500ms timeout
 {
 }
 

 bool RiskValidationService::initialize() {
     // Load validation configuration
     if (m_config.hasKey("validation.timeout_ms")) {
         m_validationTimeoutMs = m_config.getIntValue("validation.timeout_ms");
     }
     
     return true;
 }
 
 ValidationResult RiskValidationService::validateSignal(const TradingSignal& signal) {
     ValidationResult result;
     
     // Run the validation with timeout
     if (!validateSignalInternal(signal, result, m_validationTimeoutMs)) {
         // Timeout occurred, create timeout result
         result = createTimeoutResult(signal);
     }
     
     // Notify callbacks about the validation
     notifyValidation(signal, result);
     
     return result;
 }
 
 RiskParameters RiskValidationService::getRiskParameters(const ExchangeId& exchangeId, 
                                                     const TradingPair& tradingPair,
                                                     const StrategyId& strategyId) const {
     if (m_riskManager) {
         return m_riskManager->getRiskParameters(exchangeId, tradingPair, strategyId);
     }
     
     // Return empty parameters if risk manager is not available
     return RiskParameters();
 }
 
 bool RiskValidationService::updateRiskParameters(const RiskParameters& params) {
     if (m_riskManager) {
         return m_riskManager->updateRiskParameters(params);
     }
     return false;
 }
 
 RiskManagerMode RiskValidationService::getMode() const {
     if (m_riskManager) {
         return m_riskManager->getMode();
     }
     return RiskManagerMode::MAINTENANCE; // Default to maintenance mode for safety
 }
 
 bool RiskValidationService::setMode(RiskManagerMode mode) {
     if (m_riskManager) {
         m_riskManager->setMode(mode);
         return true;
     }
     return false;
 }
 
 void RiskValidationService::registerValidationCallback(
     const std::function<void(const TradingSignal&, const ValidationResult&)>& callback) {
     std::lock_guard<std::mutex> lock(m_callbackMutex);
     m_validationCallbacks.push_back(callback);
 }
 
 void RiskValidationService::setValidationTimeout(int timeoutMs) {
     if (timeoutMs > 0) {
         m_validationTimeoutMs = timeoutMs;
     }
 }
 
 int RiskValidationService::getValidationTimeout() const {
     return m_validationTimeoutMs;
 }
 
 void RiskValidationService::notifyValidation(const TradingSignal& signal, const ValidationResult& result) {
     std::lock_guard<std::mutex> lock(m_callbackMutex);
     for (const auto& callback : m_validationCallbacks) {
         callback(signal, result);
     }
 }
 
 ValidationResult RiskValidationService::createTimeoutResult(const TradingSignal& signal) {
     ValidationResult result;
     result.signalId = signal.id;
     result.approved = false;
     
     RiskViolation violation;
     violation.type = ViolationType::TIMEOUT;
     violation.description = "Risk validation timed out";
     violation.limitValue = static_cast<double>(m_validationTimeoutMs);
     violation.actualValue = 0.0; // Not applicable for timeout
     
     result.violations.push_back(violation);
     result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
     
     return result;
 }
 
 bool RiskValidationService::validateSignalInternal(const TradingSignal& signal, 
                                               ValidationResult& result, 
                                               int timeoutMs) {
     if (!m_riskManager) {
         // No risk manager available
         RiskViolation violation;
         violation.type = ViolationType::SYSTEM_ERROR;
         violation.description = "Risk manager unavailable";
         
         result.signalId = signal.id;
         result.approved = false;
         result.violations.push_back(violation);
         result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
         return true;
     }
     
    // Use std::future with timeout to implement the validation timeout
    // Use a heap-owned promise so the worker thread can safely outlive this function
    auto promisePtr = std::make_shared<std::promise<ValidationResult>>();
    auto resultFuture = promisePtr->get_future();

    // Run validation in another thread; capture signal by value and promise by shared_ptr
    std::thread validationThread([this, signal, promisePtr]() mutable {
        try {
            ValidationResult validationResult = m_riskManager->validateSignal(signal);
            try {
                promisePtr->set_value(validationResult);
            } catch (...) {
                // promise may have been satisfied already; ignore
            }
        } catch (const std::exception& e) {
            // Create error result
            ValidationResult errorResult;
            // Attempt to set a sensible signal id if available
            try {
                errorResult.signalId = signal.id;
            } catch (...) {}
            errorResult.approved = false;

            RiskViolation violation;
            violation.type = ViolationType::SYSTEM_ERROR;
            violation.description = std::string("Exception during validation: ") + e.what();
            errorResult.violations.push_back(violation);
            errorResult.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

            try {
                promisePtr->set_value(errorResult);
            } catch (...) {
                // ignore if promise already set
            }
        }
    });
     
     // Wait for the result with timeout
     auto status = resultFuture.wait_for(std::chrono::milliseconds(timeoutMs));
     
     if (status == std::future_status::ready) {
         // Got the result within timeout
         result = resultFuture.get();
         validationThread.join();
         return true;
     } else {
         // Timeout occurred
         validationThread.detach(); // Cannot wait for the thread anymore
         return false;
     }
 }
 
 } // namespace risk
 } // namespace trading