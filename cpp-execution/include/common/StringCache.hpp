#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

class StringCache {
public:
    static StringCache& instance() {
        static StringCache cache;
        return cache;
    }

    const std::string& get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }
        // Insert và return reference đến string trong map
        return cache_.emplace(key, key).first->second;
    }

    // Pre-populate common strings
    void warmup() {
        std::lock_guard<std::mutex> lock(mutex_);
        // Imbalance types
        cache_["BOOK_IMBALANCE"] = "BOOK_IMBALANCE";
        cache_["TRADE_IMBALANCE"] = "TRADE_IMBALANCE";
        cache_["QUOTE_IMBALANCE"] = "QUOTE_IMBALANCE";
        
        // Market regimes
        cache_["TRENDING"] = "TRENDING";
        cache_["RANGING"] = "RANGING";
        cache_["VOLATILE"] = "VOLATILE";
        cache_["CALM"] = "CALM";
        cache_["UNKNOWN"] = "UNKNOWN";
        
        // Metric keys
        cache_["predictedPriceMove"] = "predictedPriceMove";
        cache_["momentumCorrelation"] = "momentumCorrelation";
        cache_["predictionAligned"] = "predictionAligned";
        cache_["thresholdSensitivity"] = "thresholdSensitivity";
        cache_["marketRegime"] = "marketRegime";
        cache_["regimeName"] = "regimeName";
    }

private:
    StringCache() { warmup(); }
    std::unordered_map<std::string, std::string> cache_;
    std::mutex mutex_;
};