/**
 * @file balance_reconciliation.cpp
 * @brief Implementation of balance reconciliation functionality
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

#include "risk/posttrade/balance_reconciliation.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace trading {
namespace risk {

BalanceReconciliation::BalanceReconciliation(
    const RiskConfig& config,
    std::shared_ptr<PositionTracker> positionTracker,
    std::shared_ptr<DataInterface> dataInterface,
    std::shared_ptr<ExecutionInterface> executionInterface)
    : m_positionTracker(std::move(positionTracker)),
      m_dataInterface(std::move(dataInterface)),
      m_executionInterface(std::move(executionInterface)),
      m_config(config.getBalanceReconciliationConfig()) {
    
    if (!m_positionTracker) {
        throw std::invalid_argument("PositionTracker cannot be null");
    }
    if (!m_dataInterface) {
        throw std::invalid_argument("DataInterface cannot be null");
    }
    if (!m_executionInterface) {
        throw std::invalid_argument("ExecutionInterface cannot be null");
    }
}

BalanceReconciliation::~BalanceReconciliation() {
    shutdown();
}
// isHealthy
bool BalanceReconciliation::isHealthy() const {
    return m_dataInterface && m_dataInterface->isConnected();
}

bool BalanceReconciliation::initialize() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    
    // Initialize statistics
    m_statistics = ReconciliationStatistics{};
    
    // Initialize last reconciliation result
    m_lastReconciliation = ReconciliationResult{};
    m_lastReconciliation.success = true;
    
    // Start scheduled reconciliation if enabled
    if (m_config.reconciliationFrequency.count() > 0) {
        return startScheduledReconciliation();
    }
    
    return true;
}

bool BalanceReconciliation::shutdown() {
    // Stop scheduled reconciliation
    stopScheduledReconciliation();
    
    // Wait for background thread to finish
    if (m_scheduledThread && m_scheduledThread->joinable()) {
        m_scheduledThread->join();
    }
    
    return true;
}

ReconciliationResult BalanceReconciliation::reconcileAllPositions() {
    ReconciliationResult result;
    
    try {
        // Get all positions from position tracker to determine which exchanges to check
        auto allPositions = m_positionTracker->getAllPositions();
        std::set<core::ExchangeId> exchanges;
        
        // Extract unique exchanges from positions
        for (const auto& position : allPositions) {
            exchanges.insert(position.getExchangeId());
        }
        
        for (const auto& exchangeId : exchanges) {
            auto exchangeResult = reconcileExchangePositions(exchangeId);
            
            // Aggregate results
            result.matchedPositions += exchangeResult.matchedPositions;
            result.mismatchedPositions += exchangeResult.mismatchedPositions;
            
            // Merge discrepancies
            result.discrepancies.insert(
                result.discrepancies.end(),
                exchangeResult.discrepancies.begin(),
                exchangeResult.discrepancies.end()
            );
        }
        
        result.success = (result.mismatchedPositions == 0);
    }
    catch (const std::exception& e) {
        result.success = false;
        // TODO: Log error appropriately
    }
    
    // Update state and statistics
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastReconciliation = result;
        updateStatistics(result);
    }
    
    return result;
}

ReconciliationResult BalanceReconciliation::reconcileExchangePositions(
    const core::ExchangeId& exchangeId) {
    
    ReconciliationResult result;
    
    try {
        // Get expected positions from our tracker
        auto expectedPositions = m_positionTracker->getExchangePositions(exchangeId);
        
        // Get actual positions from exchange
        std::map<std::string, double> actualPositions;
        if (!fetchActualPositions(exchangeId, actualPositions)) {
            result.success = false;
            return result;
        }
        
        // Compare each expected position
        for (const auto& position : expectedPositions) {
            TradingPair tradingPair = position.getTradingPair();
            double expectedQuantity = position.getQuantity();
            
            // Skip positions that are effectively zero
            if (std::abs(expectedQuantity) < 1e-8) {
                continue;
            }
            
            // Find corresponding actual position
            auto actualIt = actualPositions.find(tradingPair.toString());
            double actualQuantity = (actualIt != actualPositions.end()) ? actualIt->second : 0.0;
            
            double discrepancy = expectedQuantity - actualQuantity;
            
            if (exceedsTolerance(discrepancy, expectedQuantity)) {
                // Create reconciliation event for this discrepancy
                auto event = createReconciliationEvent(
                    exchangeId, tradingPair.toString(), expectedQuantity, actualQuantity, discrepancy,
                    shouldAutoCorrect(calculateDiscrepancyPercent(expectedQuantity, actualQuantity))
                );
                
                // Apply auto-correction if enabled
                if (event.autoCorrect) {
                    applyCorrection(exchangeId, TradingPair{tradingPair}, actualQuantity);
                }
                
                result.discrepancies.push_back(event);
                result.mismatchedPositions++;
                
                // Notify callbacks
                notifyCallbacks(event);
            } else {
                result.matchedPositions++;
            }
            
            // Remove from actual positions map to track unaccounted positions
            if (actualIt != actualPositions.end()) {
                actualPositions.erase(actualIt);
            }
        }
        
        // Check for positions that exist in exchange but not in our tracker
        for (const auto& [tradingPair, actualQuantity] : actualPositions) {
            if (std::abs(actualQuantity) < 1e-8) {
                continue; // Skip zero positions
            }
            
            double expectedQuantity = 0.0;
            double discrepancy = expectedQuantity - actualQuantity;
            
            auto event = createReconciliationEvent(
                exchangeId, tradingPair, expectedQuantity, actualQuantity, discrepancy,
                shouldAutoCorrect(100.0) // 100% discrepancy for untracked positions
            );
            
            if (event.autoCorrect) {
                applyCorrection(exchangeId, TradingPair{tradingPair}, actualQuantity);
            }
            
            result.discrepancies.push_back(event);
            result.mismatchedPositions++;
            
            notifyCallbacks(event);
        }
        
        result.success = (result.mismatchedPositions == 0);
    }
    catch (const std::exception& e) {
        result.success = false;
        // TODO: Log error appropriately
    }
    
    return result;
}

ReconciliationResult BalanceReconciliation::reconcileTradingPair(
    const core::ExchangeId& exchangeId,
    const core::Symbol& tradingPair) {
    
    ReconciliationResult result;
    
    try {
        // Create TradingPair from string
        
        // Get expected position for this specific trading pair
        auto expectedPosition = m_positionTracker->getPosition(exchangeId, tradingPair);
        double expectedQuantity = expectedPosition ? expectedPosition->getQuantity() : 0.0;
        
        // Get actual position from exchange
        std::map<std::string, double> actualPositions;
        if (!fetchActualPositions(exchangeId, actualPositions)) {
            result.success = false;
            return result;
        }
        
        auto actualIt = actualPositions.find(tradingPair.toString());
        double actualQuantity = (actualIt != actualPositions.end()) ? actualIt->second : 0.0;
        
        double discrepancy = expectedQuantity - actualQuantity;
        
        if (exceedsTolerance(discrepancy, expectedQuantity)) {
            auto event = createReconciliationEvent(
                exchangeId, tradingPair, expectedQuantity, actualQuantity, discrepancy,
                shouldAutoCorrect(calculateDiscrepancyPercent(expectedQuantity, actualQuantity))
            );
            
            if (event.autoCorrect) {
                applyCorrection(exchangeId, tradingPair, actualQuantity);
            }
            
            result.discrepancies.push_back(event);
            result.mismatchedPositions = 1;
            result.matchedPositions = 0;
            
            notifyCallbacks(event);
        } else {
            result.matchedPositions = 1;
            result.mismatchedPositions = 0;
        }
        
        result.success = (result.mismatchedPositions == 0);
    }
    catch (const std::exception& e) {
        result.success = false;
    }
    
    return result;
}

bool BalanceReconciliation::startScheduledReconciliation() {
    if (m_isScheduledActive.load()) {
        return false; // Already active
    }
    
    if (m_config.reconciliationFrequency.count() <= 0) {
        return false; // Invalid frequency
    }
    
    m_shouldStop.store(false);
    m_isScheduledActive.store(true);
    
    // Calculate first reconciliation time
    m_nextScheduledTime = calculateNextReconciliationTime();
    
    // Start background thread
    m_scheduledThread = std::make_unique<std::thread>(
        &BalanceReconciliation::scheduledReconciliationLoop, this
    );
    
    return true;
}

bool BalanceReconciliation::stopScheduledReconciliation() {
    if (!m_isScheduledActive.load()) {
        return false; // Not active
    }
    
    m_shouldStop.store(true);
    m_isScheduledActive.store(false);
    
    return true;
}

bool BalanceReconciliation::isScheduledReconciliationActive() const {
    return m_isScheduledActive.load();
}

bool BalanceReconciliation::updateConfiguration(const BalanceReconciliationConfig& newConfig) {
    // Validate new configuration
    if (newConfig.reconciliationFrequency.count() < 0) {
        return false;
    }
    
    if (newConfig.maxDiscrepancyPercent < 0.0 || newConfig.maxDiscrepancyPercent > 100.0) {
        return false;
    }
    
    bool wasScheduledActive = m_isScheduledActive.load();
    
    // Update configuration
    m_config = newConfig;
    
    // Restart scheduled reconciliation if it was active and frequency changed
    if (wasScheduledActive) {
        stopScheduledReconciliation();
        if (newConfig.reconciliationFrequency.count() > 0) {
            startScheduledReconciliation();
        }
    }
    
    return true;
}

BalanceReconciliationConfig BalanceReconciliation::getConfiguration() const {
    return m_config;
}

void BalanceReconciliation::registerReconciliationCallback(
    const std::function<void(const PositionReconciliationEvent&)>& callback) {
    
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_callbacks.push_back(callback);
}

ReconciliationResult BalanceReconciliation::getLastReconciliationResult() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_lastReconciliation;
}

ReconciliationStatistics BalanceReconciliation::getStatistics() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_statistics;
}

bool BalanceReconciliation::correctDiscrepancy(
    const core::ExchangeId& exchangeId,
    const TradingPair& tradingPair,
    double correctQuantity) {
    
    return applyCorrection(exchangeId, tradingPair, correctQuantity);
}

ReconciliationResult BalanceReconciliation::forceFullReconciliation() {
    return reconcileAllPositions();
}

// Private helper methods

PositionReconciliationEvent BalanceReconciliation::createReconciliationEvent(
    const core::ExchangeId& exchangeId,
    const core::Symbol& tradingPair,
    double expectedQuantity,
    double actualQuantity,
    double discrepancy,
    bool autoCorrect) {
    
    // Create TradingPair structure
        
    PositionReconciliationEvent event(
        std::chrono::system_clock::now(),
        exchangeId,
        tradingPair,
        expectedQuantity,
        actualQuantity,
        calculateDiscrepancyPercent(expectedQuantity, actualQuantity),
        autoCorrect,
        "Position reconciliation discrepancy detected for " + tradingPair.toString()
    );
    
    return event;
}

bool BalanceReconciliation::fetchActualPositions(
    const core::ExchangeId& exchangeId,
    std::map<std::string, double>& actualPositions) {
    
    int attempts = 0;
    std::exception lastException;
    
    while (attempts < m_config.maxRetryAttempts) {
        try {
            // Try to get account balances from execution interface first
            // (as it's more likely to have real position data)
            if (m_executionInterface) {
                auto balances = m_executionInterface->getAccountBalances(exchangeId);
                for (const auto& [asset, balance] : balances) {
                    if (std::abs(balance) > m_config.minimumPositionSize) {
                        actualPositions[asset] = balance;
                    }
                }
                return true;
            }
            
            // Fallback to data interface
            if (m_dataInterface) {
                auto balances = m_dataInterface->getExchangeBalances(exchangeId);
                for (const auto& [asset, balance] : balances) {
                    if (std::abs(balance) > m_config.minimumPositionSize) {
                        actualPositions[asset] = balance;
                    }
                }
                return true;
            }
            
            return false;
        }
        catch (const std::exception& e) {
            lastException = e;
            attempts++;
            
            if (attempts < m_config.maxRetryAttempts) {
                // Wait before retrying with exponential backoff
                auto delay = m_config.getRetryDelay(attempts);
                std::this_thread::sleep_for(delay);
            }
        }
    }
    
    // All attempts failed
    // TODO: Log the final error appropriately
    return false;
}

bool BalanceReconciliation::shouldAutoCorrect(double discrepancyPercent) const {
    return m_config.autoCorrectDiscrepancies && 
           discrepancyPercent <= m_config.maxDiscrepancyPercent;
}

bool BalanceReconciliation::applyCorrection(
    const core::ExchangeId& exchangeId,
    const TradingPair& tradingPair,
    double actualQuantity) {
    
    try {
        // Update position in tracker to match actual quantity
        return m_positionTracker->updatePosition(exchangeId, tradingPair, actualQuantity);
    }
    catch (const std::exception& e) {
        // TODO: Log error appropriately
        return false;
    }
}

void BalanceReconciliation::notifyCallbacks(const PositionReconciliationEvent& event) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    
    for (const auto& callback : m_callbacks) {
        try {
            callback(event);
        }
        catch (const std::exception& e) {
            // TODO: Log callback error
        }
    }
}

double BalanceReconciliation::calculateDiscrepancyPercent(double expected, double actual) const {
    if (std::abs(expected) < 1e-8) {
        return std::abs(actual) > 1e-8 ? 100.0 : 0.0;
    }
    
    return std::abs((expected - actual) / expected) * 100.0;
}

bool BalanceReconciliation::exceedsTolerance(double discrepancy, double expected) const {
    double discrepancyPercent = calculateDiscrepancyPercent(expected, expected - discrepancy);
    return discrepancyPercent > m_config.maxDiscrepancyPercent;
}

void BalanceReconciliation::updateStatistics(const ReconciliationResult& result) {
    m_statistics.totalReconciliations++;
    m_statistics.lastReconciliationTime = result.timestamp;
    
    if (result.success) {
        m_statistics.successfulReconciliations++;
        m_statistics.consecutiveFailures = 0;
    } else {
        m_statistics.failedReconciliations++;
        m_statistics.consecutiveFailures++;
    }
    
    // Update discrepancy statistics
    m_statistics.totalDiscrepanciesFound += static_cast<int>(result.discrepancies.size());
    
    if (!result.discrepancies.empty()) {
        double totalDiscrepancy = 0.0;
        for (const auto& discrepancy : result.discrepancies) {
            totalDiscrepancy += std::abs(discrepancy.discrepancyPercentage);
        }
        
        // Update running average
        m_statistics.averageDiscrepancyValue = 
            (m_statistics.averageDiscrepancyValue * (m_statistics.totalReconciliations - 1) + 
             totalDiscrepancy / result.discrepancies.size()) / m_statistics.totalReconciliations;
    }
}

void BalanceReconciliation::scheduledReconciliationLoop() {
    while (!m_shouldStop.load()) {
        auto now = std::chrono::system_clock::now();
        
        if (now >= m_nextScheduledTime) {
            // Perform reconciliation
            reconcileAllPositions();
            
            // Calculate next reconciliation time
            m_nextScheduledTime = calculateNextReconciliationTime();
        }
        
        // Sleep for a short interval to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

std::chrono::system_clock::time_point BalanceReconciliation::calculateNextReconciliationTime() const {
    return std::chrono::system_clock::now() + m_config.reconciliationFrequency;
}

} // namespace risk
} // namespace trading