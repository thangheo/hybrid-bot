/**
 * @file balance_reconciliation_usage_example.cpp
 * @brief Example of how to use the Balance Reconciliation module
 * 
 * This example demonstrates:
 * - Setting up the balance reconciliation system
 * - Configuring reconciliation parameters
 * - Handling reconciliation events
 * - Manual reconciliation operations
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

#include "risk/posttrade/balance_reconciliation.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include "risk/interfaces/data_interface.hpp"
#include "risk/interfaces/execution_interface.hpp"
#include "risk/risk_config.hpp"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace trading::risk;

/**
 * Example of setting up and using the Balance Reconciliation system
 */
class BalanceReconciliationExample {
public:
    void setupAndRun() {
        // 1. Create and configure the risk configuration
        auto riskConfig = createRiskConfiguration();
        
        // 2. Create necessary interfaces (these would be your actual implementations)
        auto positionTracker = std::make_shared<PositionTracker>(*riskConfig, createDataInterface());
        auto dataInterface = createDataInterface();
        auto executionInterface = createExecutionInterface();
        
        // 3. Create the balance reconciliation system
        auto balanceReconciliation = std::make_unique<BalanceReconciliation>(
            *riskConfig, positionTracker, dataInterface, executionInterface
        );
        
        // 4. Register event callbacks
        setupEventCallbacks(*balanceReconciliation);
        
        // 5. Initialize the system
        if (!balanceReconciliation->initialize()) {
            std::cerr << "Failed to initialize balance reconciliation system" << std::endl;
            return;
        }
        
        std::cout << "Balance reconciliation system initialized successfully" << std::endl;
        
        // 6. Demonstrate various operations
        demonstrateReconciliationOperations(*balanceReconciliation);
        
        // 7. Start scheduled reconciliation
        if (balanceReconciliation->startScheduledReconciliation()) {
            std::cout << "Scheduled reconciliation started" << std::endl;
        }
        
        // 8. Run for a while to see scheduled reconciliation in action
        std::this_thread::sleep_for(std::chrono::minutes(2));
        
        // 9. Graceful shutdown
        balanceReconciliation->stopScheduledReconciliation();
        std::cout << "Balance reconciliation system shut down" << std::endl;
    }

private:
    /**
     * Create a sample risk configuration with balance reconciliation settings
     */
    std::unique_ptr<RiskConfig> createRiskConfiguration() {
        auto config = std::make_unique<RiskConfig>();
        
        // Configure balance reconciliation
        BalanceReconciliationConfig reconciliationConfig;
        reconciliationConfig.reconciliationFrequency = std::chrono::minutes(5); // Every 5 minutes
        reconciliationConfig.maxDiscrepancyPercent = 2.0; // 2% tolerance
        reconciliationConfig.autoCorrectDiscrepancies = true; // Enable auto-correction
        reconciliationConfig.alertOnDiscrepancy = true; // Send alerts
        reconciliationConfig.maxConsecutiveFailures = 3; // Max failures before escalation
        reconciliationConfig.minimumPositionSize = 0.001; // Minimum position to consider
        reconciliationConfig.enableDetailedLogging = true; // Detailed logging
        reconciliationConfig.reconcileOnStartup = true; // Reconcile on startup
        reconciliationConfig.maxRetryAttempts = 3; // Retry failed operations
        reconciliationConfig.enableProgressiveBackoff = true; // Use exponential backoff
        reconciliationConfig.enableScheduledReconciliation = true; // Enable scheduling
        
        config->setBalanceReconciliationConfig(reconciliationConfig);
        
        return config;
    }
    
    /**
     * Setup callbacks for reconciliation events
     */
    void setupEventCallbacks(BalanceReconciliation& reconciliation) {
        // Register callback for reconciliation events
        reconciliation.registerReconciliationCallback(
            [this](const PositionReconciliationEvent& event) {
                handleReconciliationEvent(event);
            }
        );
    }
    
    /**
     * Handle reconciliation events
     */
    void handleReconciliationEvent(const PositionReconciliationEvent& event) {
        std::cout << "\n=== RECONCILIATION EVENT ===" << std::endl;
        std::cout << "Exchange: " << event.exchangeId.toString() << std::endl;
        std::cout << "Trading Pair: " << event.tradingPair.symbol << std::endl;
        std::cout << "Expected Quantity: " << event.expectedQuantity << std::endl;
        std::cout << "Actual Quantity: " << event.actualQuantity << std::endl;
        std::cout << "Discrepancy: " << (event.expectedQuantity - event.actualQuantity) << std::endl;
        std::cout << "Discrepancy %: " << event.discrepancyPercentage << "%" << std::endl;
        std::cout << "Auto Corrected: " << (event.autoCorrect ? "Yes" : "No") << std::endl;
        std::cout << "Timestamp: " << std::chrono::duration_cast<std::chrono::seconds>(
            event.timestamp.time_since_epoch()).count() << std::endl;
        std::cout << "============================\n" << std::endl;
        
        // Take additional action for critical discrepancies
        if (event.discrepancyPercentage > 5.0) {
            std::cout << "⚠️  CRITICAL DISCREPANCY DETECTED! Taking emergency action..." << std::endl;
            // You could trigger additional risk management actions here
        }
    }
    
    /**
     * Demonstrate various reconciliation operations
     */
    void demonstrateReconciliationOperations(BalanceReconciliation& reconciliation) {
        std::cout << "\n=== DEMONSTRATING RECONCILIATION OPERATIONS ===" << std::endl;
        
        // 1. Force a full reconciliation
        std::cout << "Performing full reconciliation..." << std::endl;
        auto fullResult = reconciliation.forceFullReconciliation();
        printReconciliationResult("Full Reconciliation", fullResult);
        
        // 2. Reconcile specific exchange
        std::cout << "\nReconciling Binance exchange..." << std::endl;
        auto binanceResult = reconciliation.reconcileExchangePositions(core::ExchangeId::binance());
        printReconciliationResult("Binance Reconciliation", binanceResult);
        
        // 3. Reconcile specific trading pair
        std::cout << "\nReconciling BTC/USDT on Binance..." << std::endl;
        auto pairResult = reconciliation.reconcileTradingPair(core::ExchangeId::binance(), "BTC/USDT");
        printReconciliationResult("BTC/USDT Reconciliation", pairResult);
        
        // 4. Get reconciliation statistics
        auto stats = reconciliation.getStatistics();
        printReconciliationStatistics(stats);
        
        // 5. Demonstrate manual correction
        std::cout << "\nDemonstrating manual correction..." << std::endl;
        TradingPair btcUsdt;
        btcUsdt.symbol = "BTC/USDT";
        btcUsdt.base = "BTC";
        btcUsdt.quote = "USDT";
        
        bool correctionResult = reconciliation.correctDiscrepancy(
            core::ExchangeId::binance(), btcUsdt, 1.5 // Correct quantity to 1.5 BTC
        );
        std::cout << "Manual correction " << (correctionResult ? "succeeded" : "failed") << std::endl;
        
        // 6. Update configuration dynamically
        std::cout << "\nUpdating reconciliation configuration..." << std::endl;
        auto currentConfig = reconciliation.getConfiguration();
        currentConfig.maxDiscrepancyPercent = 1.0; // Make it more strict
        currentConfig.reconciliationFrequency = std::chrono::minutes(3); // More frequent
        
        bool configUpdateResult = reconciliation.updateConfiguration(currentConfig);
        std::cout << "Configuration update " << (configUpdateResult ? "succeeded" : "failed") << std::endl;
    }
    
    /**
     * Print reconciliation result details
     */
    void printReconciliationResult(const std::string& operationName, const ReconciliationResult& result) {
        std::cout << "\n--- " << operationName << " Results ---" << std::endl;
        std::cout << "Success: " << (result.success ? "Yes" : "No") << std::endl;
        std::cout << "Matched Positions: " << result.matchedPositions << std::endl;
        std::cout << "Mismatched Positions: " << result.mismatchedPositions << std::endl;
        std::cout << "Total Discrepancies: " << result.discrepancies.size() << std::endl;
        
        if (!result.discrepancies.empty()) {
            std::cout << "Discrepancies:" << std::endl;
            for (size_t i = 0; i < result.discrepancies.size() && i < 5; ++i) {
                const auto& disc = result.discrepancies[i];
                std::cout << "  - " << disc.exchangeId.toString() << ":" << disc.tradingPair.symbol 
                         << " (" << disc.discrepancyPercentage << "% discrepancy)" << std::endl;
            }
            if (result.discrepancies.size() > 5) {
                std::cout << "  ... and " << (result.discrepancies.size() - 5) << " more" << std::endl;
            }
        }
        std::cout << "------------------------------" << std::endl;
    }
    
    /**
     * Print reconciliation statistics
     */
    void printReconciliationStatistics(const ReconciliationStatistics& stats) {
        std::cout << "\n--- Reconciliation Statistics ---" << std::endl;
        std::cout << "Total Reconciliations: " << stats.totalReconciliations << std::endl;
        std::cout << "Successful: " << stats.successfulReconciliations << std::endl;
        std::cout << "Failed: " << stats.failedReconciliations << std::endl;
        std::cout << "Success Rate: " << (stats.getSuccessRate() * 100.0) << "%" << std::endl;
        std::cout << "Consecutive Failures: " << stats.consecutiveFailures << std::endl;
        std::cout << "Total Discrepancies Found: " << stats.totalDiscrepanciesFound << std::endl;
        std::cout << "Average Discrepancy: " << stats.averageDiscrepancyValue << "%" << std::endl;
        std::cout << "--------------------------------" << std::endl;
    }
};

/**
 * Example of integrating balance reconciliation with risk management workflow
 */
class RiskManagementIntegrationExample {
public:
    void demonstrateIntegration() {
        std::cout << "\n=== RISK MANAGEMENT INTEGRATION EXAMPLE ===" << std::endl;
        
        // This example shows how balance reconciliation fits into the broader
        // risk management workflow
        
        // 1. Position tracking detects a significant position change
        std::cout << "1. Position tracker detects large position change..." << std::endl;
        
        // 2. Trigger immediate reconciliation to verify the change
        std::cout << "2. Triggering immediate reconciliation..." << std::endl;
        
        // 3. If discrepancy found, escalate based on severity
        std::cout << "3. Analyzing discrepancy severity..." << std::endl;
        
        // 4. Take appropriate risk management action
        std::cout << "4. Taking risk management action..." << std::endl;
        
        // Example workflow:
        simulateRiskWorkflow();
    }

private:
    void simulateRiskWorkflow() {
        // Simulate a workflow where balance reconciliation is integrated
        // with other risk management components
        
        std::cout << "\nSimulated Risk Management Workflow:" << std::endl;
        std::cout << "  Step 1: Order execution reported" << std::endl;
        std::cout << "  Step 2: Position tracker updated" << std::endl;
        std::cout << "  Step 3: Automatic reconciliation triggered" << std::endl;
        std::cout << "  Step 4: Discrepancy detected (3.2%)" << std::endl;
        std::cout << "  Step 5: Exceeds threshold (2.0%), escalating..." << std::endl;
        std::cout << "  Step 6: Circuit breaker evaluating risk..." << std::endl;
        std::cout << "  Step 7: Risk acceptable, continuing trading" << std::endl;
        std::cout << "  Step 8: Scheduling follow-up reconciliation" << std::endl;
        
        std::cout << "\nWorkflow completed successfully!" << std::endl;
    }
};

/**
 * Main function demonstrating the balance reconciliation system
 */
int main() {
    try {
        std::cout << "=== BALANCE RECONCILIATION SYSTEM DEMO ===" << std::endl;
        
        // Run the main example
        BalanceReconciliationExample example;
        example.setupAndRun();
        
        // Run the integration example
        RiskManagementIntegrationExample integrationExample;
        integrationExample.demonstrateIntegration();
        
        std::cout << "\n=== DEMO COMPLETED SUCCESSFULLY ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error running balance reconciliation demo: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}