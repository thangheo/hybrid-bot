/* PositionPublisher implementation using cppzmq (ZMQ) */
#include "risk/sync/position_publisher.hpp"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

namespace trading {
namespace risk {
namespace sync {

PositionPublisher::PositionPublisher(const PositionSyncConfig& config, std::shared_ptr<zmq::context_t> zmqContext)
    : config_(config), zmqContext_(zmqContext) {
}

PositionPublisher::~PositionPublisher() {
    shutdown();
}

bool PositionPublisher::initialize() {
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true)) return true;

    try {
        // create publisher socket
        publisher_ = std::make_unique<zmq::socket_t>(*zmqContext_, zmq::socket_type::pub);
        // set HWM
        publisher_->set(zmq::sockopt::sndhwm, config_.publisherHwm);
        // bind to endpoint (publisher binds)
        publisher_->bind(config_.publishEndpoint);

        std::cerr << "[PositionPublisher] Bound to endpoint: " << config_.publishEndpoint << std::endl;

        sequenceNumber_.store(0);
        healthy_ = true;
    } catch (const zmq::error_t& e) {
        lastError_ = std::string("ZMQ publisher init failed: ") + e.what();
        healthy_ = false;
        initialized_ = false;
        return false;
    }

    return true;
}

bool PositionPublisher::start() {
    if (!initialized_) {
        if (!initialize()) return false;
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return true;

    // start worker thread
    workerThread_ = std::thread(&PositionPublisher::workerLoop, this);
    return true;
}

void PositionPublisher::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;
    queueCondition_.notify_all();
    if (workerThread_.joinable()) workerThread_.join();
}

void PositionPublisher::shutdown() {
    stop();
    if (publisher_) {
        try { publisher_->close(); } catch(...) {}
        publisher_.reset();
    }
    initialized_ = false;
}

void PositionPublisher::connectToPositionTracker(std::shared_ptr<PositionTracker> positionTracker) {
    positionTracker_ = positionTracker;
}

bool PositionPublisher::publishEvent(const PositionEvent& event) {
    if (!initialized_) {
        if (!initialize()) return false;
    }

    PositionEvent e = event; // copy so we can set sequence
    e.sequenceNumber = getNextSequenceNumber();
    if (e.eventId.empty()) {
        e.eventId = std::to_string(e.sequenceNumber);
    }

    PriorityEvent pe{e, std::chrono::system_clock::now()};
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        eventQueue_.push(std::move(pe));
        queuedEvents_.fetch_add(1);
        std::cerr << "[PositionPublisher] Enqueued event seq=" << e.sequenceNumber << " symbol=" << e.symbol << " queue_size=" << eventQueue_.size() << std::endl;
    }
    queueCondition_.notify_one();
    return true;
}

bool PositionPublisher::publishSnapshot(const PositionSnapshot& snapshot) {
    if (!initialized_) {
        if (!initialize()) return false;
    }
    try {
        auto j = snapshot.toJson();
        std::string data = j.dump();
        zmq::message_t msg(data.size());
        memcpy(msg.data(), data.data(), data.size());
        publisher_->send(msg, zmq::send_flags::none);
        publishedEvents_.fetch_add(1);
        lastPublishTime_ = std::chrono::system_clock::now();
        return true;
    } catch (const zmq::error_t& ex) {
        lastError_ = ex.what();
        healthy_ = false;
        failedEvents_.fetch_add(1);
        return false;
    }
}

PositionSyncStatus PositionPublisher::getStatus() const {
    PositionSyncStatus s;
    s.isHealthy = healthy_.load();
    s.lastSequenceNumber = sequenceNumber_.load();
    s.processedEvents = publishedEvents_.load();
    s.pendingEvents = eventQueue_.size();
    s.receivedEvents = 0; // publisher doesn't receive
    s.errorCount = failedEvents_.load();
    {
        std::lock_guard<std::mutex> lk(statsMutex_);
        s.lastError = lastError_;
    }
    return s;
}

bool PositionPublisher::isHealthy() const {
    return healthy_.load();
}

uint64_t PositionPublisher::getNextSequenceNumber() {
    return sequenceNumber_.fetch_add(1) + 1;
}

void PositionPublisher::workerLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lk(queueMutex_);
        // wait until there is something to send or stop
        queueCondition_.wait_for(lk, std::chrono::milliseconds(config_.batchTimeoutMs), [this]() {
            return !eventQueue_.empty() || !running_;
        });

        if (!running_ && eventQueue_.empty()) break;

        // build batch up to batchSize
        std::vector<PositionEvent> batch;
        batch.reserve(config_.batchSize);
        while (!eventQueue_.empty() && batch.size() < static_cast<size_t>(config_.batchSize)) {
            batch.push_back(std::move(eventQueue_.top().event));
            eventQueue_.pop();
        }
        lk.unlock();

        if (batch.empty()) continue;

        // publish each event in the batch
        for (const auto& e : batch) {
            try {
                auto j = e.toJson();
                std::string data = j.dump();
                zmq::message_t msg(data.size());
                memcpy(msg.data(), data.data(), data.size());
                                publisher_->send(msg, zmq::send_flags::none);
                                publishedEvents_.fetch_add(1);
                                // decrement queued events counter for the one we just sent
                                queuedEvents_.fetch_sub(1);
                                // Debug log
                                std::cerr << "[PositionPublisher] Sent event seq=" << e.sequenceNumber << " symbol=" << e.symbol << std::endl;
                lastPublishTime_ = std::chrono::system_clock::now();
            } catch (const zmq::error_t& ex) {
                                lastError_ = ex.what();
                                healthy_ = false;
                                failedEvents_.fetch_add(1);
            }
        }
    }
}

bool PositionPublisher::publishEventInternal(const PositionEvent& event) {
    // helper used by tests or alternative codepaths
    return publishEvent(event);
}

void PositionPublisher::processBatch() {
    // no-op - batching handled in workerLoop
}

std::vector<PositionEvent> PositionPublisher::getBatch() {
    std::vector<PositionEvent> out;
    std::lock_guard<std::mutex> lk(queueMutex_);
    while (!eventQueue_.empty() && out.size() < static_cast<size_t>(config_.batchSize)) {
        out.push_back(eventQueue_.top().event);
        eventQueue_.pop();
    }
    return out;
}

void PositionPublisher::onPositionUpdate(const Position& position, const std::string& updateType) {
    // produce PositionEvent from Position and publish
    auto ev = createPositionEvent(position, PositionEventType::POSITION_UPDATED, updateType);
    publishEvent(ev);
}

PositionEvent PositionPublisher::createPositionEvent(const Position& position, PositionEventType eventType, const std::string& source) {
    PositionEvent ev;
    ev.eventType = eventType;
    ev.priority = PositionEventPriority::NORMAL;
    ev.symbol = position.getTradingPair().toString();
    ev.exchangeId = position.getExchangeId().toString();
    ev.strategyId = position.getStrategyId().toString();
    ev.quantity = position.getQuantity();
    ev.avgPrice = position.getAverageEntryPrice();
    ev.lastPrice = position.getCurrentPrice();
    ev.unrealizedPnl = position.getUnrealizedPnl();
    ev.realizedPnl = position.getRealizedPnl();
    ev.timestamp = std::chrono::system_clock::now();
    ev.source = source;
    ev.sequenceNumber = getNextSequenceNumber();
    ev.eventId = std::to_string(ev.sequenceNumber);
    return ev;
}

void PositionPublisher::updateHealth() {}
bool PositionPublisher::checkZmqHealth() { return publisher_ != nullptr; }
std::string PositionPublisher::serializeEvent(const PositionEvent& event) { return event.toJson().dump(); }
std::string PositionPublisher::serializeSnapshot(const PositionSnapshot& snapshot) { return snapshot.toJson().dump(); }

} // namespace sync
} // namespace risk
} // namespace trading
