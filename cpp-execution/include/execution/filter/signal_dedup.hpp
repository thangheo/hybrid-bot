#pragma once

#include <unordered_map>
#include <mutex>
#include "core/trading_signal.hpp"
#include "core/time.hpp"

namespace trading {
namespace execution {

using trading::core::Timestamp;
using trading::core::Direction;

struct ActiveSignal {
    std::string signalId;
    Direction direction;
    Timestamp createdAt;   // use core timestamp (system_clock)
    Timestamp expiresAt;   // expiration time from the signal
    double strength;
};

class SignalDeduplicator {
public:
    // Configuration for deduplication thresholds and behavior
    struct Config {
        std::chrono::milliseconds debounceWindow; 
        double relStrengthThreshold; // 10% relative
        double absStrengthThreshold; // absolute delta
        double flipStrengthThreshold; // opposite direction flip
        double aggressiveDelta; // fallback override
        bool includeExchangeInKey; // whether dedup key uses exchange

        Config()
            : debounceWindow(std::chrono::milliseconds(500)),
              relStrengthThreshold(0.10),
              absStrengthThreshold(0.05),
              flipStrengthThreshold(0.70),
              aggressiveDelta(0.15),
              includeExchangeInKey(false) {}
    };

    // Construct with optional configuration
    explicit SignalDeduplicator(const Config& cfg = Config());

    // Returns true if the incoming signal should be processed (not a duplicate)
    bool shouldProcessSignal(const trading::core::TradingSignal& signal);

private:
    std::unordered_map<std::string, ActiveSignal> activeSignals_; // key: symbol.toString()
    std::mutex signalMapMutex_;
    Config cfg_;
};

} // namespace execution
} // namespace trading