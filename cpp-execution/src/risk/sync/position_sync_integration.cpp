#include "risk/sync/position_sync_integration.hpp"
#include "risk/sync/position_synchronizer.hpp"
#include <memory>

namespace trading {
namespace risk {
namespace sync {

std::unique_ptr<PositionSynchronizer> PositionSyncIntegration::setupPositionSync(
    std::shared_ptr<zmq::context_t> zmqContext,
    std::shared_ptr<PositionTracker> positionTracker,
    const PositionSyncConfig& config) {
    auto sync = createPositionSynchronizer(config, zmqContext);
    // Note: initialization/start should be done by caller
    return sync;
}

// SyncAwareRiskManager minimal implementations
PositionSyncIntegration::SyncAwareRiskManager::SyncAwareRiskManager(
    std::shared_ptr<execution::ExecutionService> executionService,
    const RiskConfig& config,
    std::shared_ptr<DataInterface> marketDataInterface,
    std::shared_ptr<PositionSynchronizer> positionSync)
    : executionService_(executionService), config_(config), marketDataInterface_(marketDataInterface), positionSync_(positionSync) {
}

bool PositionSyncIntegration::SyncAwareRiskManager::validatePositionExit(const std::string& symbol,
                                                                        const std::string& exchangeId,
                                                                        double quantity) {
    if (positionSync_) {
        if (positionSync_->hasPosition(symbol, exchangeId)) {
            auto p = positionSync_->getPosition(symbol, exchangeId);
            if (p) return std::abs(p->getQuantity()) >= std::abs(quantity);
        }
    }
    return false;
}

std::optional<Position> PositionSyncIntegration::SyncAwareRiskManager::getPositionForExit(const std::string& symbol,
                                                                                          const std::string& exchangeId) {
    if (positionSync_) return positionSync_->getPosition(symbol, exchangeId);
    return std::nullopt;
}

PositionSyncConfig PositionSyncIntegration::ConfigBuilder::build() const { return config_; }
PositionSyncIntegration::ConfigBuilder& PositionSyncIntegration::ConfigBuilder::setPublishEndpoint(const std::string& endpoint) { config_.publishEndpoint = endpoint; return *this; }
PositionSyncIntegration::ConfigBuilder& PositionSyncIntegration::ConfigBuilder::setBatchSize(int batchSize) { config_.batchSize = batchSize; return *this; }
PositionSyncIntegration::ConfigBuilder& PositionSyncIntegration::ConfigBuilder::setBatchTimeout(int timeoutMs) { config_.batchTimeoutMs = timeoutMs; return *this; }
PositionSyncIntegration::ConfigBuilder& PositionSyncIntegration::ConfigBuilder::setCacheTimeout(int timeoutMs) { config_.cacheTimeout_ms = timeoutMs; return *this; }
PositionSyncIntegration::ConfigBuilder& PositionSyncIntegration::ConfigBuilder::setReconciliationInterval(int intervalSeconds) { config_.reconciliationIntervalSeconds = intervalSeconds; return *this; }
PositionSyncIntegration::ConfigBuilder& PositionSyncIntegration::ConfigBuilder::enableHighPerformance() { return *this; }
PositionSyncIntegration::ConfigBuilder& PositionSyncIntegration::ConfigBuilder::enableHighReliability() { return *this; }

} // namespace sync
} // namespace risk
} // namespace trading
