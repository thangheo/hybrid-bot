/**
 * Position Synchronization Integration Example
 * 
 * File: position_sync_integration.hpp
 * Created: 2025-11-09
 * 
 * This demonstrates how to integrate position synchronization into the trading system
 */
#pragma once

#include "risk/sync/position_synchronizer.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include "risk/risk_manager.hpp"

namespace trading {
namespace risk {
namespace sync {

/**
 * Integration helper class for position synchronization in trading system
 */
class PositionSyncIntegration {
public:
    /**
     * Setup position synchronization in trading system
     * @param zmqContext Shared ZMQ context
     * @param positionTracker Shared position tracker
     * @param config Optional custom configuration
     * @return Configured position synchronizer
     */
    static std::unique_ptr<PositionSynchronizer> setupPositionSync(
        std::shared_ptr<zmq::context_t> zmqContext,
        std::shared_ptr<PositionTracker> positionTracker,
        const PositionSyncConfig& config = PositionSyncConfig{});
    
    /**
     * Enhanced RiskManager that uses position synchronization
     */
    class SyncAwareRiskManager {
    public:
        SyncAwareRiskManager(
            std::shared_ptr<execution::ExecutionService> executionService,
            const RiskConfig& config,
            std::shared_ptr<DataInterface> marketDataInterface,
            std::shared_ptr<PositionSynchronizer> positionSync);

        // Position checking methods to use synchronized cache
        bool validatePositionExit(const std::string& symbol, 
                                const std::string& exchangeId,
                                double quantity);
        
        std::optional<Position> getPositionForExit(const std::string& symbol,
                                                  const std::string& exchangeId);
        
    private:
        std::shared_ptr<PositionSynchronizer> positionSync_;
        std::shared_ptr<execution::ExecutionService> executionService_;
        RiskConfig config_;
        std::shared_ptr<DataInterface> marketDataInterface_;
    };
    
    /**
     * Configuration builder for position synchronization
     */
    class ConfigBuilder {
    public:
        ConfigBuilder& setPublishEndpoint(const std::string& endpoint);
        ConfigBuilder& setBatchSize(int batchSize);
        ConfigBuilder& setBatchTimeout(int timeoutMs);
        ConfigBuilder& setCacheTimeout(int timeoutMs);
        ConfigBuilder& setReconciliationInterval(int intervalSeconds);
        ConfigBuilder& enableHighPerformance();
        ConfigBuilder& enableHighReliability();
        
        PositionSyncConfig build() const;
        
    private:
        PositionSyncConfig config_;
    };
};

} // namespace sync
} // namespace risk
} // namespace trading

// Usage example in trading_system.cpp:
/*
// In Trading_system::initialize():

// 1. Create position synchronization
auto positionSyncConfig = PositionSyncIntegration::ConfigBuilder()
    .setBatchSize(5)
    .setBatchTimeout(50)
    .setCacheTimeout(10000)  // 10 seconds
    .setReconciliationInterval(60)  // 1 minute
    .enableHighPerformance()
    .build();

auto positionSync = PositionSyncIntegration::setupPositionSync(
    zmq_context_, 
    sharedPositionTracker,
    positionSyncConfig);

// 2. Start position synchronization
if (!positionSync->start(sharedPositionTracker)) {
    LOG_ERROR("Failed to start position synchronization");
    return false;
}

// 3. Create sync-aware risk manager
riskManager_ = std::make_unique<PositionSyncIntegration::SyncAwareRiskManager>(
    executionService_, 
    riskConfig_, 
    marketDataInterface_,
    std::move(positionSync));

// 4. Register position event callbacks for monitoring
positionSync->registerPositionEventCallback([this](const PositionEvent& event) {
    LOG_INFO("Position event: {} {} {} qty={}", 
             event.symbol, 
             event.exchangeId,
             static_cast<int>(event.eventType),
             event.quantity);
             
    // Custom handling for critical events
    if (event.priority == PositionEventPriority::CRITICAL) {
        // Alert monitoring systems
        notifyPositionAlert(event);
    }
});
*/