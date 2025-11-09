/* Minimal PositionSynchronizer implementation to satisfy linker during demo builds */
#include "risk/sync/position_synchronizer.hpp"
#include "risk/sync/position_publisher.hpp"
#include "risk/sync/position_subscriber.hpp"
#include <iostream>

namespace trading {
namespace risk {
namespace sync {

PositionSynchronizer::PositionSynchronizer(const PositionSyncConfig& config, std::shared_ptr<zmq::context_t> zmqContext)
    : config_(config), zmqContext_(zmqContext) {
}

PositionSynchronizer::~PositionSynchronizer() {
    shutdown();
}

bool PositionSynchronizer::initialize() {
    // Ensure idempotent initialization
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true)) {
        // Already initialized
        return true;
    }
    publisher_.store(std::make_shared<PositionPublisher>(config_, zmqContext_));
    auto subptr = std::make_shared<PositionSubscriber>(config_, zmqContext_);
    subscriber_.store(subptr);
    std::cerr << "[PositionSynchronizer] Created publisher and subscriber objects pub=" << publisher_.load().get() << " sub=" << subptr.get() << std::endl;
    return true;
}

bool PositionSynchronizer::start(std::shared_ptr<PositionTracker> positionTracker) {
    if (!initialized_) initialize();
    positionTracker_ = positionTracker;
    std::cerr << "[PositionSynchronizer] Starting components" << std::endl;
    // Start publisher first (bind) then subscriber (connect) to ensure inproc transport works
    auto pub = publisher_.load();
    if (pub) {
        pub->initialize();
        pub->connectToPositionTracker(positionTracker);
        pub->start();
    }
    auto sub = subscriber_.load();
    if (sub) {
        sub->initialize();
        sub->start();
    }
    return true;
}

void PositionSynchronizer::stop() {
    auto sub = subscriber_.load();
    if (sub) sub->stop();
}

void PositionSynchronizer::shutdown() {
    stop();
    auto sub = subscriber_.load();
    if (sub) sub->shutdown();
    auto pub = publisher_.load();
    if (pub) {
        // optionally stop publisher if needed
    }
    std::cerr << "[PositionSynchronizer] shutdown: clearing publisher/subscriber\n";
    publisher_.store(nullptr);
    subscriber_.store(nullptr);
    initialized_ = false;
}

std::shared_ptr<PositionPublisher> PositionSynchronizer::getPublisher() const { return publisher_.load(); }
std::shared_ptr<PositionSubscriber> PositionSynchronizer::getSubscriber() const { return subscriber_.load(); }

void PositionSynchronizer::registerPositionEventCallback(const PositionEventCallback& callback) {
    auto sub = subscriber_.load();
    if (sub) sub->registerEventCallback(callback);
}

bool PositionSynchronizer::hasPosition(const std::string& symbol, const std::string& exchangeId) const {
    auto sub = subscriber_.load();
    if (sub) return sub->hasPosition(symbol, exchangeId);
    return false;
}

std::optional<Position> PositionSynchronizer::getPosition(const std::string& symbol, const std::string& exchangeId) const {
    auto sub = subscriber_.load();
    if (sub) return sub->getPosition(symbol, exchangeId);
    return std::nullopt;
}

std::vector<Position> PositionSynchronizer::getAllPositions() const {
    auto sub = subscriber_.load();
    if (sub) return sub->getAllPositions();
    return {};
}

bool PositionSynchronizer::forceReconciliation() { return true; }
// Aggregate status from publisher and subscriber
PositionSyncStatus PositionSynchronizer::getOverallStatus() const {
    PositionSyncStatus s;
    bool pubHealthy = true;
    bool subHealthy = true;
    PositionSyncStatus ps, ss;
    auto pub = publisher_.load();
    auto sub = subscriber_.load();
    if (pub) ps = pub->getStatus();
    if (sub) ss = sub->getStatus();
    pubHealthy = pub ? ps.isHealthy : true;
    subHealthy = sub ? ss.isHealthy : true;
    s.isHealthy = pubHealthy && subHealthy;
    s.lastSequenceNumber = std::max(ps.lastSequenceNumber, ss.lastSequenceNumber);
    // Use subscriber processed events as the authoritative processed count
    s.processedEvents = ss.processedEvents;
    s.cacheSize = ss.cacheSize;
    s.cacheHits = ss.cacheHits;
    s.cacheMisses = ss.cacheMisses;
    s.lastError = !ss.lastError.empty() ? ss.lastError : ps.lastError;
    return s;
}

PositionSynchronizer::SyncMetrics PositionSynchronizer::getMetrics() const {
    SyncMetrics m{};
    PositionSyncStatus ps, ss;
    auto pub = publisher_.load();
    auto sub = subscriber_.load();
    if (pub) ps = pub->getStatus();
    if (sub) ss = sub->getStatus();
    m.publishedEvents = ps.processedEvents;
    m.publishFailures = ps.errorCount;
    m.receivedEvents = ss.processedEvents;
    m.processedEvents = ss.processedEvents;
    m.cacheHits = ss.cacheHits;
    m.cacheMisses = ss.cacheMisses;
    m.cacheHitRate = (m.cacheHits + m.cacheMisses) ? static_cast<double>(m.cacheHits) / (m.cacheHits + m.cacheMisses) : 0.0;
    m.sequenceGaps = 0;
    m.reconciliationCount = reconciliationCount_;
    m.lastReconciliation = lastReconciliationTime_;
    return m;
}

bool PositionSynchronizer::isHealthy() const { return true; }

// reconciliation helpers
void PositionSynchronizer::reconciliationLoop() {}
bool PositionSynchronizer::performReconciliation() { return true; }
bool PositionSynchronizer::comparePositions() { return true; }
void PositionSynchronizer::handleDiscrepancy(const std::string&, const Position&, const std::optional<Position>&) {}
void PositionSynchronizer::updateOverallHealth() {}
bool PositionSynchronizer::checkComponentsHealth() { return true; }
PositionSynchronizer::SyncMetrics PositionSynchronizer::calculateMetrics() const { return SyncMetrics{}; }
bool PositionSynchronizer::validateConfig() const { return true; }

// Factory
std::unique_ptr<PositionSynchronizer> createPositionSynchronizer(
    const PositionSyncConfig& config,
    std::shared_ptr<zmq::context_t> zmqContext) {
    return std::make_unique<PositionSynchronizer>(config, zmqContext);
}

} // namespace sync
} // namespace risk
} // namespace trading
