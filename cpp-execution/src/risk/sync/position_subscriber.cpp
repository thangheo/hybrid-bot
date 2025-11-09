/* PositionSubscriber implementation using cppzmq (ZMQ) */
#include "risk/sync/position_subscriber.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

namespace trading {
namespace risk {
namespace sync {

PositionSubscriber::PositionSubscriber(const PositionSyncConfig& config, std::shared_ptr<zmq::context_t> zmqContext)
    : config_(config), zmqContext_(zmqContext) {
    std::cerr << "[PositionSubscriber] ctor this=" << this << "\n";
}

PositionSubscriber::~PositionSubscriber() {
    std::cerr << "[PositionSubscriber] dtor this=" << this << "\n";
    shutdown();
}

bool PositionSubscriber::initialize() {
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true)) return true;
    try {
        subscriber_ = std::make_unique<zmq::socket_t>(*zmqContext_, zmq::socket_type::sub);
        subscriber_->set(zmq::sockopt::rcvhwm, config_.subscriberHwm);
        // Subscribe to all topics by default
        subscriber_->set(zmq::sockopt::subscribe, "");
        // Connect to endpoints provided
        for (const auto& ep : config_.subscribeEndpoints) {
            try { subscriber_->connect(ep); } catch(...) {}
        }
        for (const auto& ep : config_.subscribeEndpoints) {
            std::cerr << "[PositionSubscriber] connecting to endpoint: " << ep << std::endl;
        }
        healthy_ = true;
    } catch (const zmq::error_t& ex) {
        lastError_ = ex.what();
        initialized_ = false;
        healthy_ = false;
        return false;
    }
    return true;
}

bool PositionSubscriber::start() {
    if (!initialized_) if (!initialize()) return false;
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return true;
    workerThread_ = std::thread([this]() {
        while (running_) {
            try {
                zmq::message_t msg;
                // blocking recv with small timeout handled by poller
                zmq::pollitem_t items[] = { {static_cast<void*>(*subscriber_), 0, ZMQ_POLLIN, 0} };
                zmq::poll(items, 1, config_.batchTimeoutMs);
                if (items[0].revents & ZMQ_POLLIN) {
                    subscriber_->recv(msg, zmq::recv_flags::none);
                    std::string data(static_cast<char*>(msg.data()), msg.size());
                    // try to parse json and dispatch
                    try {
                        auto j = nlohmann::json::parse(data);
                        auto ev = PositionEvent::fromJson(j);
                        // sequence handling
                        if (ev.sequenceNumber > 0) {
                            if (ev.sequenceNumber > lastSequenceNumber_.load() + 1) {
                                ++sequenceGaps_;
                            }
                            lastSequenceNumber_.store(ev.sequenceNumber);
                        }
                        // apply to cache and notify
                        std::cerr << "[PositionSubscriber] Received event seq=" << ev.sequenceNumber << " symbol=" << ev.symbol << std::endl;
                        receivedEvents_.fetch_add(1);
                        processEvent(ev);
                    } catch (const std::exception& e) {
                        lastError_ = e.what();
                        failedEvents_.fetch_add(1);
                        healthy_ = false;
                    }
                }
            } catch (const zmq::error_t& ex) {
                lastError_ = ex.what();
                failedEvents_.fetch_add(1);
                healthy_ = false;
            }
        }
    });
    return true;
}

void PositionSubscriber::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;
    if (workerThread_.joinable()) workerThread_.join();
}

void PositionSubscriber::shutdown() {
    std::cerr << "[PositionSubscriber] shutdown this=" << this << " - setting initialized_=false\n";
    initialized_ = false;  // Mark as shutdown first to prevent new operations
    stop();
    if (subscriber_) {
        try { subscriber_->close(); } catch(...) {}
        subscriber_.reset();
    }
    std::unique_lock lock(cacheMutex_);
    positionCache_.clear();
}

void PositionSubscriber::registerEventCallback(const PositionEventCallback& callback) {
    std::lock_guard<std::mutex> lk(callbackMutex_);
    eventCallbacks_.push_back(callback);
}

bool PositionSubscriber::hasPosition(const std::string& symbol, const std::string& exchangeId) const {
    if (!initialized_.load()) return false;
    std::shared_lock lock(cacheMutex_);
    return positionCache_.find(createCacheKey(symbol, exchangeId)) != positionCache_.end();
}

std::optional<Position> PositionSubscriber::getPosition(const std::string& symbol, const std::string& exchangeId) const {
    // diagnostic: print 'this' pointer before touching members (non-dereferencing)
    std::cerr << "[PositionSubscriber] getPosition called this=" << this << " symbol=" << symbol << " exchange=" << exchangeId << "\n";
    if (!initialized_.load()) return std::nullopt;
    std::shared_lock lock(cacheMutex_);
    auto it = positionCache_.find(createCacheKey(symbol, exchangeId));
    if (it != positionCache_.end()) {
        cacheHits_.fetch_add(1);
        return it->second.position;
    }
    cacheMisses_.fetch_add(1);
    return std::nullopt;
}

std::vector<Position> PositionSubscriber::getAllPositions() const {
    if (!initialized_.load()) return {};
    std::vector<Position> out;
    std::shared_lock lock(cacheMutex_);
    out.reserve(positionCache_.size());
    for (const auto& kv : positionCache_) out.push_back(kv.second.position);
    return out;
}

std::vector<Position> PositionSubscriber::getStrategyPositions(const std::string& strategyId) const {
    if (!initialized_.load()) return {};
    std::vector<Position> out;
    std::shared_lock lock(cacheMutex_);
    for (const auto& kv : positionCache_) {
        if (kv.second.position.getStrategyId().toString() == strategyId) out.push_back(kv.second.position);
    }
    return out;
}

std::vector<Position> PositionSubscriber::getSymbolPositions(const std::string& symbol) const {
    if (!initialized_.load()) return {};
    std::vector<Position> out;
    std::shared_lock lock(cacheMutex_);
    for (const auto& kv : positionCache_) {
        if (kv.second.position.getTradingPair().toString() == symbol) out.push_back(kv.second.position);
    }
    return out;
}

PositionSyncStatus PositionSubscriber::getStatus() const {
    PositionSyncStatus s;
    s.isHealthy = healthy_.load();
    s.lastSequenceNumber = lastSequenceNumber_.load();
    
    // CONCURRENCY FIX: Must acquire shared lock to read cache size
    {
        std::shared_lock lock(cacheMutex_);
        s.cacheSize = positionCache_.size();
    }
    
    s.cacheHits = cacheHits_.load();
    s.cacheMisses = cacheMisses_.load();
    s.pendingEvents = 0;
    s.errorCount = failedEvents_.load();
    s.processedEvents = processedEvents_.load();
    s.receivedEvents = receivedEvents_.load();
    {
        std::lock_guard<std::mutex> lk(statsMutex_);
        s.lastError = lastError_;
    }
    return s;
}

bool PositionSubscriber::isHealthy() const { return healthy_.load(); }

bool PositionSubscriber::refreshCache() { return true; }
void PositionSubscriber::clearCache() { 
    if (!initialized_.load()) return;
    std::unique_lock lock(cacheMutex_); 
    positionCache_.clear(); 
}

PositionSubscriber::CacheStats PositionSubscriber::getCacheStats() const {
    CacheStats s{};
    
    // CONCURRENCY FIX: Must acquire shared lock to read cache size  
    {
        std::shared_lock lock(cacheMutex_);
        s.totalEntries = positionCache_.size();
        s.validEntries = positionCache_.size();
    }
    
    s.expiredEntries = 0;
    s.hits = cacheHits_.load();
    s.misses = cacheMisses_.load();
    s.hitRate = (s.hits + s.misses) ? static_cast<double>(s.hits) / (s.hits + s.misses) : 0.0;
    return s;
}

// Internal helpers
void PositionSubscriber::workerLoop() {}
void PositionSubscriber::processEvent(const PositionEvent& event) { updateCache(event); notifyCallbacks(event); processedEvents_.fetch_add(1); }
void PositionSubscriber::updateCache(const PositionEvent& event) {
    auto pos = createPositionFromEvent(event);
    auto now = std::chrono::system_clock::now();
    CachedPosition cp{pos, now, now, true};
    
    // CRITICAL FIX: Must acquire exclusive lock when modifying cache
    std::unique_lock lock(cacheMutex_);
    positionCache_.insert_or_assign(createCacheKey(event.symbol, event.exchangeId), std::move(cp));
}
void PositionSubscriber::notifyCallbacks(const PositionEvent& event) {
    std::lock_guard<std::mutex> lk(callbackMutex_);
    for (auto &cb : eventCallbacks_) cb(event);
}

std::string PositionSubscriber::createCacheKey(const std::string& symbol, const std::string& exchangeId) const {
    return symbol + "@" + exchangeId;
}

void PositionSubscriber::cleanExpiredEntries() {}
Position PositionSubscriber::createPositionFromEvent(const PositionEvent& event) {
    // Construct Position using public API
    trading::core::ExchangeId exch = trading::core::ExchangeId::fromString(event.exchangeId);
    trading::core::Symbol sym = trading::core::Symbol::fromString(event.symbol);
    trading::core::StrategyId sid = trading::core::StrategyId::fromString(event.strategyId);
    Position p(exch, sym, sid, event.quantity, event.avgPrice);
    p.setCurrentPrice(event.lastPrice);
    p.setUnrealizedPnl(event.unrealizedPnl);
    p.setRealizedPnl(event.realizedPnl);
    return p;
}

bool PositionSubscriber::receiveMessage(std::string& topic, std::string& data) { return false; }
PositionEvent PositionSubscriber::deserializeEvent(const std::string& data) { return PositionEvent::fromJson(nlohmann::json::parse(data)); }

void PositionSubscriber::updateHealth() {}
bool PositionSubscriber::checkSequenceGap(uint64_t sequenceNumber) { return true; }
void PositionSubscriber::handleSequenceGap(uint64_t expected, uint64_t received) {}
bool PositionSubscriber::validateEvent(const PositionEvent& event) { return true; }
bool PositionSubscriber::isEventRecent(const PositionEvent& event) { return true; }

} // namespace sync
} // namespace risk
} // namespace trading
