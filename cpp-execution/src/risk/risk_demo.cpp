/**
 * @file risk_manager_demo.cpp
 * @brief Comprehensive demo for Risk Management System
 * 
 * This demo tests all major risk management components:
 * - Pre-trade risk validation
 * - Post-trade monitoring
 * - Portfolio management
 * - Circuit breakers
 * - Position limits
 * - Emergency actions
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <random>
#include <iomanip>

// Risk management includes
#include "risk/risk_manager.hpp"
#include "risk/risk_config.hpp"
#include "risk/risk_types.hpp"

// Core includes
#include "core/market_types.hpp"
#include "core/direction.hpp"
#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"

using namespace trading;
using namespace trading::risk;
using namespace trading::core;
using namespace trading::execution;
using namespace std::chrono_literals;

class RiskManagerDemo {
public:
    RiskManagerDemo() : running_(false), gen_(std::random_device{}()) {
        setupConfiguration();
        setupTestData();
    }
    
    ~RiskManagerDemo() {
        stop();
    }
    
    bool start() {
        std::cout << "=== RISK MANAGER COMPREHENSIVE DEMO ===" << std::endl;
        std::cout << "Testing all risk management components..." << std::endl;
        
        // Initialize risk manager
        riskManager_ = std::make_unique<RiskManager>(config_);
        if (!riskManager_->initialize()) {
            std::cerr << "Failed to initialize risk manager" << std::endl;
            return false;
        }
        
        // Register for risk events
        riskManager_->registerRiskEventCallback([this](const RiskEvent& event) {
            handleRiskEvent(event);
        });
        
        running_ = true;
        
        // Start demo scenarios
        std::cout << "\nStarting risk validation scenarios..." << std::endl;
        
        runTestScenarios();
        
        return true;
    }
    
    void stop() {
        if (running_) {
            std::cout << "\nStopping risk manager demo..." << std::endl;
            running_ = false;
            if (riskManager_) {
                riskManager_.reset();
            }
        }
    }

private:
    std::unique_ptr<RiskManager> riskManager_;
    RiskConfig config_;
    std::atomic<bool> running_;
    std::mt19937 gen_;
    
    // Test counters
    std::atomic<int> signalsProcessed_{0};
    std::atomic<int> signalsApproved_{0};
    std::atomic<int> signalsRejected_{0};
    std::atomic<int> riskEventsReceived_{0};
    
    void setupConfiguration() {
        std::cout << "Setting up risk configuration..." << std::endl;
        
        // Position limits
        auto& positionConfig = const_cast<PositionLimitConfig&>(config_.getPositionLimitConfig());
        positionConfig.assetLimits["BTC"] = 1.0;     // 1 BTC max
        positionConfig.assetLimits["ETH"] = 10.0;    // 10 ETH max
        positionConfig.assetLimits["USDT"] = 100000; // 100k USDT max
        positionConfig.defaultLimit = 1000.0;
        positionConfig.enforceStrictly = true;
        
        // Exposure limits
        auto& exposureConfig = const_cast<ExposureLimitConfig&>(config_.getExposureLimitConfig());
        exposureConfig.maxTotalExposure = 50000.0;    // $50k total
        exposureConfig.maxAssetExposure = 20000.0;     // $20k per asset
        exposureConfig.maxStrategyExposure = 30000.0;  // $30k per strategy
        exposureConfig.maxExchangeExposure = 40000.0;  // $40k per exchange
        exposureConfig.volatilityMultiplier = 1.5;
        
        // Circuit breaker
        auto& circuitConfig = const_cast<CircuitBreakerConfig&>(config_.getCircuitBreakerConfig());
        circuitConfig.metricThresholds["error_rate"] = MetricThreshold(0.05, 0.10, false);
        circuitConfig.metricThresholds["drawdown_percent"] = MetricThreshold(5.0, 10.0, false);
        circuitConfig.coolingPeriod = 30s;  // Short for demo
        circuitConfig.autoResume = true;
        
        // Stop loss
        auto& stopLossConfig = const_cast<StopLossConfig&>(config_.getStopLossConfig());
        stopLossConfig.maxLossPercentage = 0.05;  // 5% max loss
        stopLossConfig.enableTrailingStop = true;
        stopLossConfig.trailingStopDistance = 0.02;  // 2%
        
        // Fat finger protection
        auto& fatFingerConfig = const_cast<FatFingerConfig&>(config_.getFatFingerConfig());
        fatFingerConfig.maxPriceDeviation = 0.05;      // 5% price deviation
        fatFingerConfig.maxOrderSize = 5.0;            // Max 5 BTC order
        fatFingerConfig.notionalValueLimit = 25000.0;  // $25k limit
        fatFingerConfig.enforceAdditionalVerification = true;
        
        // Order validation
        auto& orderConfig = const_cast<OrderValidationConfig&>(config_.getOrderValidationConfig());
        orderConfig.validateMinOrderSize = true;
        orderConfig.validatePriceRange = true;
        orderConfig.maxPriceDeviationPercent = 10.0;
        orderConfig.defaultMinOrderSize = 0.001;
        
        std::cout << "Risk configuration setup complete" << std::endl;
    }
    
    void setupTestData() {
        // Setup will be done during test execution
    }
    
    void runTestScenarios() {
        std::cout << "\n=== RUNNING RISK TEST SCENARIOS ===" << std::endl;
        
        // Scenario 1: Valid signals (should pass)
        testValidSignals();
        std::this_thread::sleep_for(2s);
        
        // Scenario 2: Position limit violations
        testPositionLimits();
        std::this_thread::sleep_for(2s);
        
        // Scenario 3: Fat finger protection
        testFatFingerProtection();
        std::this_thread::sleep_for(2s);
        
        // Scenario 4: Price validation
        testPriceValidation();
        std::this_thread::sleep_for(2s);
        
        // Scenario 5: Circuit breaker activation
        testCircuitBreaker();
        std::this_thread::sleep_for(2s);
        
        // Scenario 6: Market data updates and portfolio tracking
        testMarketDataAndPortfolio();
        std::this_thread::sleep_for(2s);
        
        // Scenario 7: Emergency actions
        testEmergencyActions();
        std::this_thread::sleep_for(2s);
        
        // Final summary
        printFinalSummary();
    }
    
    void testValidSignals() {
        std::cout << "\n--- Testing Valid Signals ---" << std::endl;
        
        std::vector<TradingSignal> validSignals = {
            createSignal("SIG001", "BTC/USDT", Direction::BUY, 0.1, 45000.0),
            createSignal("SIG002", "ETH/USDT", Direction::SELL, 0.5, 3200.0),
            createSignal("SIG003", "BTC/USDT", Direction::SELL, 0.2, 44000.0)
        };
        
        for (const auto& signal : validSignals) {
            auto result = riskManager_->validateSignal(signal);
            signalsProcessed_++;
            
            if (result.approved) {
                signalsApproved_++;
                std::cout << "✓ Signal " << signal.id << " APPROVED" << std::endl;
            } else {
                signalsRejected_++;
                std::cout << "✗ Signal " << signal.id << " REJECTED: ";
                for (const auto& violation : result.violations) {
                    std::cout << violation.description << " ";
                }
                std::cout << std::endl;
            }
        }
    }
    
    void testPositionLimits() {
        std::cout << "\n--- Testing Position Limits ---" << std::endl;
        
        // First, simulate some existing positions by processing execution reports
        simulateExecution("BTC/USDT", Direction::BUY, 0.8, 45000.0);  // 0.8 BTC position
        simulateExecution("ETH/USDT", Direction::BUY, 8.0, 3200.0);   // 8 ETH position
        
        // Now try signals that would exceed limits
        std::vector<TradingSignal> limitTestSignals = {
            createSignal("SIG004", "BTC/USDT", Direction::BUY, 0.5, 45000.0),  // Would exceed 1.0 BTC limit
            createSignal("SIG005", "ETH/USDT", Direction::BUY, 5.0, 3200.0),   // Would exceed 10.0 ETH limit
            createSignal("SIG006", "BTC/USDT", Direction::SELL, 0.3, 44000.0)  // Should be OK (reducing position)
        };
        
        for (const auto& signal : limitTestSignals) {
            auto result = riskManager_->validateSignal(signal);
            signalsProcessed_++;
            
            if (result.approved) {
                signalsApproved_++;
                std::cout << "✓ Signal " << signal.id << " APPROVED" << std::endl;
            } else {
                signalsRejected_++;
                std::cout << "✗ Signal " << signal.id << " REJECTED: ";
                for (const auto& violation : result.violations) {
                    std::cout << violation.description << " ";
                }
                std::cout << std::endl;
            }
        }
    }
    
    void testFatFingerProtection() {
        std::cout << "\n--- Testing Fat Finger Protection ---" << std::endl;
        
        std::vector<TradingSignal> fatFingerSignals = {
            createSignal("SIG007", "BTC/USDT", Direction::BUY, 10.0, 45000.0),   // Huge size
            createSignal("SIG008", "BTC/USDT", Direction::BUY, 0.1, 100000.0),   // Ridiculous price
            createSignal("SIG009", "ETH/USDT", Direction::SELL, 0.5, 1000.0),    // Very low price
            createSignal("SIG010", "BTC/USDT", Direction::BUY, 0.1, 47000.0)     // Normal signal
        };
        
        for (const auto& signal : fatFingerSignals) {
            auto result = riskManager_->validateSignal(signal);
            signalsProcessed_++;
            
            if (result.approved) {
                signalsApproved_++;
                std::cout << "✓ Signal " << signal.id << " APPROVED" << std::endl;
            } else {
                signalsRejected_++;
                std::cout << "✗ Signal " << signal.id << " REJECTED: ";
                for (const auto& violation : result.violations) {
                    std::cout << violation.description << " ";
                }
                std::cout << std::endl;
            }
        }
    }
    
    void testPriceValidation() {
        std::cout << "\n--- Testing Price Validation ---" << std::endl;
        
        // Update market data first
        updateMarketPrice("BTC/USDT", 45000.0);
        updateMarketPrice("ETH/USDT", 3200.0);
        
        std::vector<TradingSignal> priceTestSignals = {
            createSignal("SIG011", "BTC/USDT", Direction::BUY, 0.1, 49500.0),   // 10% above market
            createSignal("SIG012", "BTC/USDT", Direction::SELL, 0.1, 40500.0),  // 10% below market
            createSignal("SIG013", "BTC/USDT", Direction::BUY, 0.1, 54000.0),   // 20% above (should fail)
            createSignal("SIG014", "ETH/USDT", Direction::BUY, 1.0, 3300.0)     // Normal price
        };
        
        for (const auto& signal : priceTestSignals) {
            auto result = riskManager_->validateSignal(signal);
            signalsProcessed_++;
            
            if (result.approved) {
                signalsApproved_++;
                std::cout << "✓ Signal " << signal.id << " APPROVED" << std::endl;
            } else {
                signalsRejected_++;
                std::cout << "✗ Signal " << signal.id << " REJECTED: ";
                for (const auto& violation : result.violations) {
                    std::cout << violation.description << " ";
                }
                std::cout << std::endl;
            }
        }
    }
    
    void testCircuitBreaker() {
        std::cout << "\n--- Testing Circuit Breaker ---" << std::endl;
        
        // Simulate conditions that would trigger circuit breaker
        // This is simplified - in real system, metrics would come from monitoring
        
        std::cout << "Simulating high error rate condition..." << std::endl;
        
        // Try to trigger emergency mode through system health issues
        // In real implementation, this would be done through metrics
        std::cout << "Checking system health..." << std::endl;
        bool isHealthy = riskManager_->checkSystemHealth();
        riskManager_->setMode(isHealthy ? RiskManagerMode::NORMAL : RiskManagerMode::EMERGENCY);
        std::cout << "System health: " << (isHealthy ? "HEALTHY" : "UNHEALTHY") << std::endl;
        
        // Test mode changes
        std::cout << "Testing mode transitions..." << std::endl;
        auto currentMode = riskManager_->getMode();
        std::cout << "Current mode: " << static_cast<int>(currentMode) << std::endl;
        
        // Try setting heightened risk mode
        riskManager_->setMode(RiskManagerMode::HEIGHTENED_RISK);
        std::cout << "Switched to heightened risk mode" << std::endl;
        
        // Test signal in heightened risk mode
        auto signal = createSignal("SIG015", "BTC/USDT", Direction::BUY, 0.1, 45000.0);
        auto result = riskManager_->validateSignal(signal);
        std::cout << "Signal in heightened risk mode: " 
                  << (result.approved ? "APPROVED" : "REJECTED") << std::endl;
        
        // Return to normal mode
        riskManager_->setMode(RiskManagerMode::NORMAL);
        std::cout << "Returned to normal mode" << std::endl;
    }
    
    void testMarketDataAndPortfolio() {
        std::cout << "\n--- Testing Market Data Updates and Portfolio Tracking ---" << std::endl;
        
        // Simulate market data updates
        std::vector<MarketData> marketUpdates = {
            createMarketData("BTC/USDT", 45000.0, 46000.0, 44500.0),
            createMarketData("ETH/USDT", 3200.0, 3250.0, 3150.0),
            createMarketData("BTC/USDT", 44000.0, 44500.0, 43500.0),  // Price drop
            createMarketData("ETH/USDT", 3100.0, 3150.0, 3050.0)     // Price drop
        };
        
        for (const auto& marketData : marketUpdates) {
            std::cout << "Updating market data: " << marketData.tradingPair 
                      << " @ $" << marketData.price << std::endl;
            riskManager_->updateMarketData(marketData);
            std::this_thread::sleep_for(500ms);
        }
        
        // Get performance metrics
        auto metrics = riskManager_->getPerformanceMetrics();
        std::cout << "Current performance metrics retrieved" << std::endl;
        
        // Test position reconciliation
        std::cout << "Running position reconciliation..." << std::endl;
        bool reconciled = riskManager_->reconcilePositions(std::nullopt);
        std::cout << "Position reconciliation: " << (reconciled ? "SUCCESS" : "FAILED") << std::endl;
    }
    
    void testEmergencyActions() {
        std::cout << "\n--- Testing Emergency Actions ---" << std::endl;
        
        std::cout << "Testing emergency mode activation..." << std::endl;
        
        // Trigger emergency mode
        riskManager_->setMode(RiskManagerMode::EMERGENCY);
        
        // Try to validate signals in emergency mode (should be rejected)
        auto emergencySignal = createSignal("SIG016", "BTC/USDT", Direction::BUY, 0.1, 45000.0);
        auto result = riskManager_->validateSignal(emergencySignal);
        
        std::cout << "Signal in emergency mode: " 
                  << (result.approved ? "APPROVED" : "REJECTED") << std::endl;
        
        if (!result.approved) {
            std::cout << "Rejection reason: ";
            for (const auto& violation : result.violations) {
                std::cout << violation.description << " ";
            }
            std::cout << std::endl;
        }
        
        // Test emergency action directly
        std::cout << "Triggering manual emergency action..." << std::endl;
        bool emergencySuccess = riskManager_->emergencyAction("Manual test trigger");
        std::cout << "Emergency action result: " << (emergencySuccess ? "SUCCESS" : "FAILED") << std::endl;
        
        // Return to maintenance mode for cleanup
        riskManager_->setMode(RiskManagerMode::MAINTENANCE);
        std::cout << "Switched to maintenance mode for cleanup" << std::endl;
    }
    
    void printFinalSummary() {
        std::cout << "\n=== RISK MANAGER DEMO SUMMARY ===" << std::endl;
        std::cout << "Signals Processed: " << signalsProcessed_.load() << std::endl;
        std::cout << "Signals Approved: " << signalsApproved_.load() << std::endl;
        std::cout << "Signals Rejected: " << signalsRejected_.load() << std::endl;
        std::cout << "Risk Events: " << riskEventsReceived_.load() << std::endl;
        
        if (signalsProcessed_.load() > 0) {
            double approvalRate = (double)signalsApproved_.load() / signalsProcessed_.load() * 100.0;
            std::cout << "Approval Rate: " << std::fixed << std::setprecision(2) 
                      << approvalRate << "%" << std::endl;
        }
        
        std::cout << "\nDemo completed successfully!" << std::endl;
    }
    
    // Helper methods
    TradingSignal createSignal(const std::string& id, const std::string& pair, 
                              Direction direction, double size, double price) {
        TradingSignal signal;
        signal.id = id;
        signal.symbol = pair;
        signal.direction = direction;
        signal.strength = 0.8;
        signal.targetSize = size;
        signal.signalMetrics["expected_price"] = price;
        signal.signalMetrics["market_price"] = price * 0.999; // Slightly different
        signal.strategyId = StrategyId("demo_strategy", "1.0");
        signal.generationTime = std::chrono::system_clock::now();
        signal.ttl = 10000ms; // 10 seconds
        signal.expirationTime = signal.generationTime + signal.ttl;
        return signal;
    }
    
    void simulateExecution(const std::string& pair, Direction direction, 
                          double quantity, double price) {
        // Create and process an execution report to simulate existing positions
        ExecutionReport report;
        report.setOrderId("ORDER_" + std::to_string(gen_()));
        report.setSymbol(pair);
        report.setSide(direction == Direction::BUY ? models::OrderSide::BUY : models::OrderSide::SELL);
        report.setQuantity(quantity);
        report.setFilledQuantity(quantity);
        report.setAverageFillPrice(price);
        report.setStatus(models::OrderState::FILLED);
        
        riskManager_->processExecutionReport(report);
        std::cout << "Simulated execution: " << quantity << " " << pair 
                  << " @ $" << price << std::endl;
    }
    
    void updateMarketPrice(const std::string& pair, double price) {
        MarketData marketData = createMarketData(pair, price, price * 1.001, price * 0.999);
        riskManager_->updateMarketData(marketData);
    }
    
    MarketData createMarketData(const std::string& pair, double price, double high, double low) {
        MarketData data;
        data.tradingPair = pair;
        data.exchangeId = ExchangeId::binance();
        data.timestamp = std::chrono::system_clock::now();
        data.price = price;
        data.bid = price * 0.9995;
        data.ask = price * 1.0005;
        data.high24h = high;
        data.low24h = low;
        data.volume = 1000.0;
        return data;
    }
    
    void handleRiskEvent(const RiskEvent& event) {
        riskEventsReceived_++;
        std::cout << "\n🚨 RISK EVENT: " << event.description 
                  << " (Severity: " << event.severity << ")" << std::endl;
    }
};

int main() {
    try {
        RiskManagerDemo demo;
        
        if (!demo.start()) {
            std::cerr << "Failed to start risk manager demo" << std::endl;
            return 1;
        }
        
        std::cout << "\nPress Enter to exit..." << std::endl;
        std::cin.get();
        
        demo.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
