/**
 * Position Synchronization Demo
 * 
 * File: position_sync_demo.cpp  
 * Created: 2025-11-09
 * 
 * Demonstrates how to use position synchronization system
 */
#include "risk/sync/position_synchronizer.hpp"
#include "risk/sync/position_sync_integration.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include "risk/risk_config.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace trading::risk;
using namespace trading::risk::sync;

/**
 * Demo class showing position synchronization usage
 */
class PositionSyncDemo {
public:
    PositionSyncDemo() {
        // Initialize ZMQ context
        zmqContext_ = std::make_shared<zmq::context_t>(1);
        
        // Load configuration
        loadConfiguration();
        
        // Create position tracker
        RiskConfig riskConfig;
        positionTracker_ = std::make_shared<PositionTracker>(riskConfig, nullptr);
    }
    
    void runDemo() {
        std::cout << "\n=== Position Synchronization Demo ===" << std::endl;
        
        // 1. Setup position synchronization
        setupPositionSync();
        
        // 2. Simulate position changes
        simulatePositionChanges();
        
        // 3. Test position queries  
        testPositionQueries();
        
        // 4. Test reconciliation
        testReconciliation();
        
        // 5. Show metrics
        showMetrics();
        
        // 6. Cleanup
        cleanup();
        
        std::cout << "=== Demo Complete ===" << std::endl;
    }
    
private:
    std::shared_ptr<zmq::context_t> zmqContext_;
    std::shared_ptr<PositionTracker> positionTracker_;
    std::unique_ptr<PositionSynchronizer> positionSync_;
    PositionSyncConfig config_;
    
    void loadConfiguration() {
        // Create demo configuration
        config_.publishEndpoint = "inproc://demo_position_pub";
        config_.subscribeEndpoints = {"inproc://demo_position_pub"};
        config_.batchSize = 5;
        config_.batchTimeoutMs = 50;
        config_.cacheTimeout_ms = 10000;
        config_.reconciliationIntervalSeconds = 30;
        config_.enablePriority = true;
        
        LOG_INFO("Position sync configuration loaded");
    }
    
    void setupPositionSync() {
        std::cout << "\n--- Setting up Position Synchronization ---" << std::endl;
        
        // Create position synchronizer
        positionSync_ = PositionSyncIntegration::setupPositionSync(
            zmqContext_, positionTracker_, config_);
        
        if (!positionSync_) {
            std::cerr << "Failed to create position synchronizer!" << std::endl;
            return;
        }
        
        // Start synchronization
        if (!positionSync_->start(positionTracker_)) {
            std::cerr << "Failed to start position synchronization!" << std::endl;
            return;
        }

        // Register event callback after start so subscriber is initialized
        positionSync_->registerPositionEventCallback(
            [this](const PositionEvent& event) {
                handlePositionEvent(event);
            });
        
        std::cout << "✅ Position synchronization started successfully" << std::endl;
    }
    
    void simulatePositionChanges() {
        std::cout << "\n--- Simulating Position Changes ---" << std::endl;
        
        // Simulate opening positions
        simulatePositionOpen("BTCUSDT", "binance", 1.5, 45000.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        simulatePositionOpen("ETHUSDT", "binance", 10.0, 3000.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Simulate position updates
        simulatePositionUpdate("BTCUSDT", "binance", 2.0, 45500.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Simulate position close
        simulatePositionClose("ETHUSDT", "binance");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::cout << "✅ Position changes simulated" << std::endl;
    // give some time for async publish/subscribe to process
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
    
    void testPositionQueries() {
        std::cout << "\n--- Testing Position Queries ---" << std::endl;
        
        // Test position existence check
        bool hasBTC = positionSync_->hasPosition("BTCUSDT", "binance");
        bool hasETH = positionSync_->hasPosition("ETHUSDT", "binance");
        
        std::cout << "Has BTC position: " << (hasBTC ? "YES" : "NO") << std::endl;
        std::cout << "Has ETH position: " << (hasETH ? "YES" : "NO") << std::endl;
        
        // Test position retrieval
        auto btcPosition = positionSync_->getPosition("BTCUSDT", "binance");
        if (btcPosition) {
            std::cout << "BTC Position - Qty: " << btcPosition->getQuantity()
                      << ", Avg Price: " << btcPosition->getAverageEntryPrice() << std::endl;
        }
        
        // Test getting all positions
        auto allPositions = positionSync_->getAllPositions();
        std::cout << "Total positions in cache: " << allPositions.size() << std::endl;
        
        std::cout << "✅ Position queries tested" << std::endl;
    }
    
    void testReconciliation() {
        std::cout << "\n--- Testing Reconciliation ---" << std::endl;
        
        // Force reconciliation
        bool reconcileResult = positionSync_->forceReconciliation();
        std::cout << "Force reconciliation result: " 
                  << (reconcileResult ? "SUCCESS" : "FAILED") << std::endl;
        
        std::cout << "✅ Reconciliation tested" << std::endl;
    }
    
    void showMetrics() {
        std::cout << "\n--- Position Sync Metrics ---" << std::endl;
        
        auto metrics = positionSync_->getMetrics();
        auto status = positionSync_->getOverallStatus();
        
        std::cout << "Published Events: " << metrics.publishedEvents << std::endl;
        std::cout << "Received Events: " << metrics.receivedEvents << std::endl;
        std::cout << "Cache Hit Rate: " << metrics.cacheHitRate << std::endl;
        std::cout << "Sequence Gaps: " << metrics.sequenceGaps << std::endl;
        std::cout << "Is Healthy: " << (status.isHealthy ? "YES" : "NO") << std::endl;
        std::cout << "Processed Events: " << status.processedEvents << std::endl;
        
        if (!status.lastError.empty()) {
            std::cout << "Last Error: " << status.lastError << std::endl;
        }
        
        std::cout << "✅ Metrics displayed" << std::endl;
    }
    
    void cleanup() {
        std::cout << "\n--- Cleaning Up ---" << std::endl;
        
        if (positionSync_) {
            positionSync_->stop();
            positionSync_->shutdown();
        }
        
        std::cout << "✅ Cleanup completed" << std::endl;
    }
    
    void handlePositionEvent(const PositionEvent& event) {
        std::string eventTypeName;
        switch (event.eventType) {
            case PositionEventType::POSITION_OPENED:
                eventTypeName = "OPENED";
                break;
            case PositionEventType::POSITION_UPDATED:
                eventTypeName = "UPDATED";
                break;
            case PositionEventType::POSITION_CLOSED:
                eventTypeName = "CLOSED";
                break;
            case PositionEventType::POSITION_RECONCILED:
                eventTypeName = "RECONCILED";
                break;
            default:
                eventTypeName = "UNKNOWN";
        }
        
        std::cout << "📊 Position Event: " << event.symbol << " " 
                  << eventTypeName << " qty=" << event.quantity
                  << " seq=" << event.sequenceNumber << std::endl;
    }
    
    // Helper methods to simulate position changes
    void simulatePositionOpen(const std::string& symbol, const std::string& exchange, 
                             double quantity, double price) {
        std::cout << "Simulating position open: " << symbol << " qty=" << quantity << std::endl;
        // Publish an open event if publisher is available
        if (positionSync_ && positionSync_->getPublisher()) {
            PositionEvent ev;
            ev.eventType = PositionEventType::POSITION_OPENED;
            ev.priority = PositionEventPriority::HIGH;
            ev.symbol = symbol;
            ev.exchangeId = exchange;
            ev.strategyId = "demo";
            ev.quantity = quantity;
            ev.avgPrice = price;
            ev.lastPrice = price;
            ev.timestamp = std::chrono::system_clock::now();
            positionSync_->getPublisher()->publishEvent(ev);
        }
    }
    
    void simulatePositionUpdate(const std::string& symbol, const std::string& exchange,
                               double newQuantity, double newPrice) {
        std::cout << "Simulating position update: " << symbol << " qty=" << newQuantity << std::endl;
        if (positionSync_ && positionSync_->getPublisher()) {
            PositionEvent ev;
            ev.eventType = PositionEventType::POSITION_UPDATED;
            ev.priority = PositionEventPriority::NORMAL;
            ev.symbol = symbol;
            ev.exchangeId = exchange;
            ev.strategyId = "demo";
            ev.quantity = newQuantity;
            ev.avgPrice = newPrice;
            ev.lastPrice = newPrice;
            ev.timestamp = std::chrono::system_clock::now();
            positionSync_->getPublisher()->publishEvent(ev);
        }
    }
    
    void simulatePositionClose(const std::string& symbol, const std::string& exchange) {
        std::cout << "Simulating position close: " << symbol << std::endl;
        if (positionSync_ && positionSync_->getPublisher()) {
            PositionEvent ev;
            ev.eventType = PositionEventType::POSITION_CLOSED;
            ev.priority = PositionEventPriority::HIGH;
            ev.symbol = symbol;
            ev.exchangeId = exchange;
            ev.strategyId = "demo";
            ev.quantity = 0.0;
            ev.avgPrice = 0.0;
            ev.lastPrice = 0.0;
            ev.timestamp = std::chrono::system_clock::now();
            positionSync_->getPublisher()->publishEvent(ev);
        }
    }
};

int main() {
    try {
        PositionSyncDemo demo;
        demo.runDemo();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with error: " << e.what() << std::endl;
        return 1;
    }
}