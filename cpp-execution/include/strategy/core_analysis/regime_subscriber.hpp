// regime_subscriber.hpp (public include)
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>

namespace trading::strategy::core_analysis {

struct RegimeInfo {
    int regime = -1; // -1 means unknown/null
    std::vector<double> probs;
    std::string last_ts;
    std::string error;
    bool valid = false; // true when an entry exists
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
    void dump() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace trading::strategy::core_analysis
