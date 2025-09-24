#include "execution/filter/signal_dedup.hpp"
#include "core/time.hpp"

namespace trading {
namespace execution {

using trading::core::Timestamp;
using trading::core::now;

// Deduplication policy (tunable):
// - Debounce window: ignore repeated signals for the same symbol within a short time
//   unless their strength or direction justifies processing.
// - Same direction: process if strength increased by a relative factor (e.g. +10%)
//   or absolute delta (e.g. +0.05), whichever is larger.
// - Opposite direction: process if strength exceeds an absolute threshold (e.g. 0.7)
// - Expiration: if the stored signal is expired, accept the new one.

bool SignalDeduplicator::shouldProcessSignal(const trading::core::TradingSignal& signal) {
    const Timestamp nowTs = now();
    // Build key using symbol and optionally exchange
    std::string key = signal.symbol.toString();
    if (cfg_.includeExchangeInKey) {
        key += "@" + signal.exchangeId.toString();
    }

    // Determine incoming signal expiration. Prefer explicit expirationTime if set,
    // otherwise use generationTime + ttl.
    Timestamp incomingExpires = signal.expirationTime;
    if (incomingExpires.time_since_epoch().count() == 0) {
        incomingExpires = signal.generationTime + signal.ttl;
    }

    std::lock_guard<std::mutex> lock(signalMapMutex_);

    auto it = activeSignals_.find(key);
    if (it == activeSignals_.end()) {
        // No active signal for this symbol: store and accept
        activeSignals_.emplace(key, ActiveSignal{
            signal.id,
            signal.direction,
            nowTs,
            incomingExpires,
            signal.strength
        });
        return true;
    }

    auto& existing = it->second;

    // If the existing stored signal has already expired, accept the new one.
    if (nowTs > existing.expiresAt) {
        existing = ActiveSignal{signal.id, signal.direction, nowTs, incomingExpires, signal.strength};
        return true;
    }

    // Debounce: if the new signal arrived very shortly after the existing one,
    // require a noticeable change to accept it.
    const auto debounceMs = cfg_.debounceWindow;
    bool withinDebounce = (nowTs - existing.createdAt) < debounceMs;

    // Same direction: accept if strength increased sufficiently
    if (existing.direction == signal.direction) {
        const double absDelta = signal.strength - existing.strength;
        const double relDelta = (existing.strength > 0.0) ? (absDelta / existing.strength) : 1.0;
    const double relThreshold = cfg_.relStrengthThreshold;
    const double absThreshold = cfg_.absStrengthThreshold;

        if ((!withinDebounce && (absDelta > 0.0)) || (absDelta > absThreshold) || (relDelta > relThreshold)) {
            existing = ActiveSignal{signal.id, signal.direction, nowTs, incomingExpires, signal.strength};
            return true;
        }
        // Otherwise, treat as duplicate
        return false;
    }

    // Opposite direction: accept if strength is strong enough to flip
    const double flipThreshold = cfg_.flipStrengthThreshold;
    if (signal.strength >= flipThreshold) {
        existing = ActiveSignal{signal.id, signal.direction, nowTs, incomingExpires, signal.strength};
        return true;
    }

    // If we're within the debounce window and strength is not sufficient, skip.
    if (withinDebounce) return false;

    // As a fallback, accept if incoming strength is noticeably larger than existing (aggressive override)
    if (signal.strength > existing.strength + cfg_.aggressiveDelta) {
        existing = ActiveSignal{signal.id, signal.direction, nowTs, incomingExpires, signal.strength};
        return true;
    }

    return false;
}

SignalDeduplicator::SignalDeduplicator(const Config& cfg)
    : cfg_(cfg) {}

} // namespace execution
} // namespace trading