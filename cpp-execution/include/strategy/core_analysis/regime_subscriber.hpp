// regime_subscriber.hpp (public include)
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>

namespace trading::strategy::core_analysis {

struct RegimeInfo {
    int regime = -1; // -1 means unknown/null
    std::vector<double> probs;
    std::string last_ts;
    std::string error;
    bool valid = false; // true when an entry exists
    // Optional diagnostics published by regime_publisher.py
    std::string recommended_action; // e.g. "extend", "recommend_exit", "none"
    std::optional<double> short_alpha;
    std::optional<double> p_trend_up;
    std::optional<double> stay_prob;
    std::optional<double> mixture_half_life_min;
    std::optional<bool> micro_ok;
    std::string recommendation_reason;
    std::optional<int> suggested_time_cap_min;
    
    // Hedging information from regime_publisher
    struct HedgingInfo {
        std::string action;      // "open_short", "close", etc.
        bool success = false;
        std::string reason;
        bool is_hedged = false;
        std::string timestamp;
    };
    std::optional<HedgingInfo> hedging;
};

class RegimeSubscriber {
public:
    RegimeSubscriber(const std::string& addr = "tcp://localhost:5555",
                    const std::string& topic = "regimes_update");
    ~RegimeSubscriber();

    RegimeSubscriber(const RegimeSubscriber&) = delete;
    RegimeSubscriber& operator=(const RegimeSubscriber&) = delete;

    void start();
    void stop();
    bool running() const;

    bool get(const std::string& symbol, RegimeInfo& out) const;
    
    // Check if cached data is fresh (within max_age_seconds)
    bool isFresh(const std::string& symbol, int max_age_seconds = 300) const;
    
    // Clean up old cache entries (older than max_age_seconds)
    void cleanupOldEntries(int max_age_seconds = 3600);
    
    void dump() const;
// interactive_loop
    
private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace trading::strategy::core_analysis
