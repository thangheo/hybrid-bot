/**
 * @file imbalance_detection_engine.cpp
 * @brief Implementation of the imbalance detection engine
 * 
 * @details
 * Implements algorithms for detecting significant order book imbalances
 * that may predict short-term price movements.
 * 
 * Created on: May 5, 2025
 */

 #include "strategy/pattern/imbalance_detection_engine.hpp"
 #include "strategy/types/profit_aware_labeling.hpp"
#include "strategy/exit/adaptive_exit_strategy.hpp"
 #include <common/event_bus.hpp>
 #include <common/trading_events.hpp>
 #include <algorithm>
 #include <cmath>
 #include <iostream>
 #include <chrono>
#include <future>
#include <functional>
#include <thread>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <random>
 #include <sstream>
#include <filesystem>
#include <common/logger.hpp>
#include "perception/bayesian_trading_adapter.hpp"
// #include "core_analysis/regime_subscriber.hpp"
#include "strategy/types/correlation_types.hpp" // for marketRegimeToString
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>

 using namespace trading::core;
 using namespace std::chrono_literals;

namespace trading {
namespace strategy {
// Local tick-size loader: read symbol-level and exchange-level tick sizes
namespace {
    static std::unordered_map<std::string, double> s_symbolTickSizeLocal;
    static std::unordered_map<std::string, double> s_exchangeTickSizeLocal;
    static std::once_flag s_tickInitFlag;

    static std::string normalizeSymbolKey(std::string k) {
        k.erase(std::remove(k.begin(), k.end(), '/'), k.end());
        for (auto &c: k) c = static_cast<char>(std::toupper(c));
        return k;
    }

    static void initTickSizesLocal() {
        try {
            const std::string symPath = "config/symbol_tick_sizes.json";
            if (std::filesystem::exists(symPath)) {
                std::ifstream ifs(symPath);
                if (ifs.is_open()) {
                    nlohmann::json j; ifs >> j;
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        try {
                            double t = it.value().get<double>();
                            std::string k = normalizeSymbolKey(it.key());
                            s_symbolTickSizeLocal[k] = t;
                        } catch (...) {}
                    }
                }
            }
        } catch (...) {}

        try {
            const std::string exchPath = "config/exchange_specific_params.json";
            if (std::filesystem::exists(exchPath)) {
                std::ifstream ifs(exchPath);
                if (ifs.is_open()) {
                    nlohmann::json j; ifs >> j;
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        try {
                            if (it.value().contains("numeric") && it.value()["numeric"].contains("tick_size")) {
                                double t = it.value()["numeric"]["tick_size"].get<double>();
                                s_exchangeTickSizeLocal[it.key()] = t;
                            }
                        } catch (...) {}
                    }
                }
            }
        } catch (...) {}
    }

    static double getConfiguredTickSize(const core::ExchangeId &exch, const core::Symbol &sym) {
        std::call_once(s_tickInitFlag, initTickSizesLocal);
        std::string k = normalizeSymbolKey(sym.toString());
        auto it = s_symbolTickSizeLocal.find(k);
        if (it != s_symbolTickSizeLocal.end() && it->second > 0.0) return it->second;
        auto it2 = s_exchangeTickSizeLocal.find(exch.toString());
        if (it2 != s_exchangeTickSizeLocal.end() && it2->second > 0.0) return it2->second;
        return 0.01; // default fallback
    }
}
// Thread-safe statistical adapter to compute dynamic thresholds per regime
class StatisticalThresholdAdapter {
public:
    struct DynamicThresholds {
        double buyThreshold = 0.55;
        // make SELL threshold less negative by default to increase sensitivity
        double sellThreshold = -0.35;
        double minProfitableReturn = 0.003; // default trading cost
    };

private:
    struct ThresholdStats {
        std::deque<double> recentReturns;
        std::deque<double> recentVolatilities;
        size_t maxHistory = 1000;

        double percentile(const std::deque<double>& data, double pct) const {
            if (data.empty()) return 0.0;
            std::vector<double> sorted(data.begin(), data.end());
            std::sort(sorted.begin(), sorted.end());
            
            // FIX: Correct percentile calculation with interpolation
            // For pct=0.5 (median) with 10 elements, we want index between 4 and 5, not 5
            double exactIdx = pct * (sorted.size() - 1);
            size_t lowerIdx = static_cast<size_t>(std::floor(exactIdx));
            size_t upperIdx = static_cast<size_t>(std::ceil(exactIdx));
            
            if (lowerIdx == upperIdx || upperIdx >= sorted.size()) {
                return sorted[lowerIdx];
            }
            
            // Linear interpolation between two nearest values
            double fraction = exactIdx - lowerIdx;
            return sorted[lowerIdx] * (1.0 - fraction) + sorted[upperIdx] * fraction;
        }
    };

    std::map<int, ThresholdStats> regimeStats_;
    mutable std::mutex mu_;

public:
    DynamicThresholds calculateThresholds(int regime, double currentVol, double minProfitableOverride = -1.0) {
        std::lock_guard<std::mutex> lk(mu_);
        auto &stats = regimeStats_[regime];
        
        DynamicThresholds out;
        
        // FIX: Return default thresholds if insufficient data
        if (stats.recentReturns.size() < 10) {
            LOG_TRACE("calculateThresholds: Insufficient data ({}), using defaults", 
                     stats.recentReturns.size());
            return out; // Returns default values
        }
        
        const double P75 = 0.60; 
        const double P25 = 0.40;
        const double MIN_ABS_PCT = 0.30;
        const double VOL_ADJUST_MULT = 0.15;
        
        double p75_return = stats.percentile(stats.recentReturns, P75);
        double p25_return = stats.percentile(stats.recentReturns, P25);
        double median_vol = stats.percentile(stats.recentVolatilities, 0.50);
        
        // FIX: Validate percentile results
        if (!std::isfinite(p75_return) || !std::isfinite(p25_return) || !std::isfinite(median_vol)) {
            LOG_ERROR("calculateThresholds: Invalid percentiles p75={}, p25={}, medVol={}", 
                     p75_return, p25_return, median_vol);
            return out; // Return defaults
        }
        
        // FIX: Clamp percentile values to reasonable ranges
        p75_return = std::clamp(p75_return, -10.0, 10.0);
        p25_return = std::clamp(p25_return, -10.0, 10.0);
        
        double volRef = std::max(median_vol, 1e-6);
        double volAdjust = std::clamp(currentVol / volRef, 0.5, 2.0);
        
        const double MIN_Z_MAG = 0.20;
        
        double scaledBuyThreshold = p75_return * (1.0 + VOL_ADJUST_MULT * (volAdjust - 1.0));
        double scaledSellThreshold = p25_return * (1.0 + VOL_ADJUST_MULT * (volAdjust - 1.0));
        
        // These clamps should now actually work since input values are bounded
        out.buyThreshold = std::clamp(std::max(MIN_Z_MAG, scaledBuyThreshold), 0.1, 3.0);
        out.sellThreshold = std::clamp(std::min(-MIN_Z_MAG, scaledSellThreshold), -3.0, -0.1);

        // Calculate min profitable return
        std::deque<double> absvals;
        for (double r : stats.recentReturns) {
            absvals.push_back(std::abs(r));
        }
        
        if (!absvals.empty()) {
            double minAbsNorm = stats.percentile(absvals, MIN_ABS_PCT);
            double computed = std::max(0.0005, minAbsNorm * volRef) + 0.003;
            // If caller provided an override (>=0), ensure minProfitableReturn is at least that
            if (minProfitableOverride >= 0.0) {
                out.minProfitableReturn = std::max(computed, minProfitableOverride);
            } else {
                out.minProfitableReturn = computed;
            }
        }
        
        // FIX: Final validation before returning
        if (!std::isfinite(out.buyThreshold) || !std::isfinite(out.sellThreshold) || 
            !std::isfinite(out.minProfitableReturn)) {
            LOG_ERROR("calculateThresholds: Invalid output thresholds, using defaults");
            out.buyThreshold = 0.55;
            out.sellThreshold = -0.35;
            out.minProfitableReturn = 0.003;
        }
        
        return out;
    }

    void updateStats(int regime, double return_, double volatility) {
        std::lock_guard<std::mutex> lk(mu_);
        auto &s = regimeStats_[regime];
        
        // FIX: Check if existing data is corrupted and reset if needed
        if (!s.recentReturns.empty()) {
            double maxAbs = 0.0;
            for (double r : s.recentReturns) {
                maxAbs = std::max(maxAbs, std::abs(r));
            }
            
            // If we have extreme values, reset the statistics
            if (maxAbs > 100.0) {
                LOG_ERROR("Extreme values detected in regime {} (max={}), resetting statistics", 
                         regime, maxAbs);
                s.recentReturns.clear();
                s.recentVolatilities.clear();
            }
        }
        
        // FIX: Validate inputs before processing
        if (!std::isfinite(return_) || !std::isfinite(volatility)) {
            LOG_WARN("updateStats: Invalid input return={}, vol={}", return_, volatility);
            return;
        }
        
        // FIX: Clamp volatility to reasonable range before using  
        // Vol between 0.0001% and 100% - prevents division by tiny numbers
        double volRef = std::clamp(volatility, 1e-6, 1.0);
        
        // Store normalized return (z-like): log-return divided by volatility  
        double norm = return_ / volRef;
        
        // FIX: Clamp normalized return to prevent accumulation of extreme values
        // Z-scores beyond ±20 are practically impossible in real markets
        norm = std::clamp(norm, -20.0, 20.0);
        
        s.recentReturns.push_back(norm);
        s.recentVolatilities.push_back(volRef); // Store clamped volatility
        
        while (s.recentReturns.size() > s.maxHistory) s.recentReturns.pop_front();
        while (s.recentVolatilities.size() > s.maxHistory) s.recentVolatilities.pop_front();
    }
    
    // FIX: Add reset mechanism when extreme values are detected
    void resetRegimeStats(int regime) {
        std::lock_guard<std::mutex> lk(mu_);
        LOG_WARN("Resetting statistics for regime {} due to data corruption", regime);
        regimeStats_[regime].recentReturns.clear();
        regimeStats_[regime].recentVolatilities.clear();
    }
    
    // FIX: Add method to get data range for diagnostics
    std::pair<double, double> getDataRange(int regime) const {
        std::lock_guard<std::mutex> lk(mu_);
        const auto &stats = regimeStats_.find(regime);
        if (stats == regimeStats_.end() || stats->second.recentReturns.empty()) {
            return {0.0, 0.0};
        }
        
        const auto &returns = stats->second.recentReturns;
        double minReturn = *std::min_element(returns.begin(), returns.end());
        double maxReturn = *std::max_element(returns.begin(), returns.end());
        return {minReturn, maxReturn};
    }
    
    // FIX: Add method to get data size for diagnostics
    size_t getDataSize(int regime) const {
        std::lock_guard<std::mutex> lk(mu_);
        const auto &stats = regimeStats_.find(regime);
        if (stats == regimeStats_.end()) {
            return 0;
        }
        return stats->second.recentReturns.size();
    }
};

static StatisticalThresholdAdapter g_statThresholdAdapter;

// Global profit-aware labeling instance
static ProfitAwareLabeling g_profitLabeling;

// Shared AdaptiveExitStrategy pointer to be registered at startup via
// setGlobalAdaptiveExitStrategy(). This ensures all components use the same
// instance and share marketHistory_. Default is null until Trading_system
// registers its instance in initialize().
static std::shared_ptr<trading::strategy::AdaptiveExitStrategy> g_optimalExitPtr;

void setGlobalAdaptiveExitStrategy(const std::shared_ptr<trading::strategy::AdaptiveExitStrategy>& ptr) {
    g_optimalExitPtr = ptr;
}

std::shared_ptr<trading::strategy::AdaptiveExitStrategy> getGlobalAdaptiveExitStrategy() {
    return g_optimalExitPtr;
}

std::shared_ptr<trading::strategy::AdaptiveExitStrategy> ensureGlobalAdaptiveExitStrategy(double prediction_window_seconds) {
    // Double-checked locking to avoid races in startup ordering
    auto cur = g_optimalExitPtr;
    if (cur) return cur;
    static std::mutex g_create_mtx;
    std::lock_guard<std::mutex> lk(g_create_mtx);
    if (g_optimalExitPtr) return g_optimalExitPtr;
    try {
        auto inst = std::make_shared<trading::strategy::AdaptiveExitStrategy>(prediction_window_seconds);
        g_optimalExitPtr = inst;
        LOG_INFO("ensureGlobalAdaptiveExitStrategy: created and registered AdaptiveExitStrategy {}", static_cast<const void*>(inst.get()));
        return g_optimalExitPtr;
    } catch (const std::exception &ex) {
        LOG_WARN("ensureGlobalAdaptiveExitStrategy: failed to create AdaptiveExitStrategy: {}", ex.what());
    } catch (...) {
        LOG_WARN("ensureGlobalAdaptiveExitStrategy: unknown error creating AdaptiveExitStrategy");
    }
    return g_optimalExitPtr;
}

// Helper: build filesystem-friendly model filename for exchange+symbol
static std::string buildSymbolModelPath(const std::string &baseDir, const core::ExchangeId &exch, const core::Symbol &sym) {
    if (baseDir.empty()) return std::string();
    std::ostringstream ss;
    ss << baseDir;
    std::string base = ss.str();
    if (!base.empty() && base.back() != '/' && base.back() != '\\') ss << '/';
    // sanitize exchange and symbol by replacing any path-unfriendly chars
    auto sanitize = [](const std::string &s) {
        std::string out;
        for (char c : s) {
            if (std::isalnum((unsigned char)c) || c == '_' || c == '-' ) out.push_back(c);
            else out.push_back('_');
        }
        return out;
    };
    ss << sanitize(exch.toString()) << "_" << sanitize(sym.toString()) << ".json";
    return ss.str();
}

// Get or create a per-symbol Bayes handle, load model file if exists
trading::perception::BayesHandle trading::strategy::ImbalanceDetectionEngine::getBayesHandleForSymbol(const core::ExchangeId &exch, const core::Symbol &sym) {
    // Legacy helper kept for compatibility: if running in per-symbol (per-regime)
    // mode return the first regime handle (regime 0) if present, otherwise
    // fall back to creating a single handle as before.
    std::string key = exch.toString() + "_" + sym.toString();
    {
        std::lock_guard<std::mutex> lk(bayesHandlesMutex_);
        auto it = bayesHandles_.find(key);
        if (it != bayesHandles_.end()) {
            auto arr = it->second;
            return arr[0];
        }
    }

    // If per-symbol directory configured we prefer to create/get the array of handles
    if (!bayesModelDirectory_.empty()) {
        auto arr = getBayesHandlesForSymbol(exch, sym);
        return arr[0];
    }

    // Legacy single-file mode: create or return cached single handle
    trading::perception::BayesHandle h = nullptr;
    try {
        h = trading::perception::bayes_create();
    } catch (...) {
        return nullptr;
    }

    // Try load from single-file path if configured
    if (!bayesModelPath_.empty()) {
        try {
            if (std::filesystem::exists(bayesModelPath_)) {
                trading::perception::bayes_load(h, bayesModelPath_);
            }
        } catch (...) {}
    }

    {
        std::lock_guard<std::mutex> lk(bayesMutex_);
        // store into legacy handle slot
        if (!bayes_handle_) bayes_handle_ = h;
    }
    return h;
}

// New: return array of NUMBER_OF_REGIMES handles (one per regime). Create/load them if missing.
std::array<trading::perception::BayesHandle, ImbalanceDetectionEngine::NUMBER_OF_REGIMES> trading::strategy::ImbalanceDetectionEngine::getBayesHandlesForSymbol(const core::ExchangeId &exch, const core::Symbol &sym) {
    std::array<trading::perception::BayesHandle, ImbalanceDetectionEngine::NUMBER_OF_REGIMES> out;
    out.fill(nullptr);
    std::string key = exch.toString() + "_" + sym.toString();
    {
        std::lock_guard<std::mutex> lk(bayesHandlesMutex_);
        auto it = bayesHandles_.find(key);
        if (it != bayesHandles_.end()) return it->second;
    }

    // Initialize all handles as null - create them lazily when needed
    for (int i = 0; i < ImbalanceDetectionEngine::NUMBER_OF_REGIMES; ++i) {
        out[i] = nullptr;
    }

    // Try load per-regime files if directory configured
    if (!bayesModelDirectory_.empty()) {
        try {
            // create base path and append regime index
            std::ostringstream ss;
            ss << bayesModelDirectory_;
            std::string base = ss.str();
            if (!base.empty() && base.back() != '/' && base.back() != '\\') ss << '/';
            // sanitized base name
            auto sanitize = [](const std::string &s) {
                std::string out;
                for (char c : s) {
                    if (std::isalnum((unsigned char)c) || c == '_' || c == '-') out.push_back(c);
                    else out.push_back('_');
                }
                return out;
            };
            std::string name = sanitize(exch.toString()) + "_" + sanitize(sym.toString());
            for (int i = 0; i < ImbalanceDetectionEngine::NUMBER_OF_REGIMES; ++i) {
                std::ostringstream fn;
                std::string regimeName = marketRegimeToString(static_cast<MarketRegimeType>(i));
                fn << bayesModelDirectory_ << "/" << name << "_" << regimeName << ".json";
                std::string path = fn.str();
                // If a saved per-regime model exists, create a handle and attempt to load it.
                if (!path.empty() && std::filesystem::exists(path)) {
                    try {
                        auto h = trading::perception::bayes_create();
                        if (h) {
                            if (trading::perception::bayes_load(h, path)) {
                                out[i] = h;
                            } else {
                                try { trading::perception::bayes_destroy(h); } catch(...) {}
                            }
                        }
                    } catch (...) {}
                }
            }
        } catch (...) {}
    }

    {
        std::lock_guard<std::mutex> lk(bayesHandlesMutex_);
        bayesHandles_[key] = out;
    }
    return out;
}

bool trading::strategy::ImbalanceDetectionEngine::saveBayesForSymbol(trading::perception::BayesHandle h, const core::ExchangeId &exch, const core::Symbol &sym) {
    if (!h) return false;
    if (bayesModelDirectory_.empty()) return false;
    try {
        std::string path = buildSymbolModelPath(bayesModelDirectory_, exch, sym);
        if (path.empty()) return false;
        try { std::filesystem::create_directories(std::filesystem::path(path).parent_path()); } catch (...) {}
        return trading::perception::bayes_save(h, path);
    } catch (...) {
        return false;
    }
}


// Internal simple thread-pool used to run detectors without creating a new
// OS thread for each call. Kept internal to this translation unit.
namespace {
class SimpleThreadPool {
public:
    explicit SimpleThreadPool(size_t threads = std::max(1u, std::thread::hardware_concurrency())) {
        stop_.store(false);
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lk(mutex_);
                        cv_.wait(lk, [this] { return stop_.load() || !tasks_.empty(); });
                        if (stop_.load() && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    try {
                        task();
                    } catch (...) {
                        // swallow - detector exceptions are handled by their packaged_task
                    }
                }
            });
        }
    }

    ~SimpleThreadPool() {
        stop();
    }

    void stop() {
        bool expected = false;
        if (!stop_.compare_exchange_strong(expected, true)) return;
        cv_.notify_all();
        for (auto &t : workers_) if (t.joinable()) t.join();
    }

    template<typename F>
    auto submit(F f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto taskPtr = std::make_shared<std::packaged_task<R()>>(std::move(f));
        std::future<R> fut = taskPtr->get_future();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            tasks_.emplace([taskPtr]() { (*taskPtr)(); });
        }
        cv_.notify_one();
        return fut;
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
};

SimpleThreadPool &getDetectorThreadPool() {
    static SimpleThreadPool pool(std::max(1u, std::thread::hardware_concurrency()));
    return pool;
}
} // namespace
// Helper utilities for feature extraction and reference selection
namespace {
// Compute microprice from an OrderBookMetrics snapshot. Returns 0.0 if cannot compute.
static double computeMicroPrice(const OrderBookMetrics &m) {
    try {
        double bidP = m.bidPrice;
        double askP = m.askPrice;
        double bidV = (m.bidVolumesByLevel.empty() ? m.totalBidVolume : m.bidVolumesByLevel.front());
        double askV = (m.askVolumesByLevel.empty() ? m.totalAskVolume : m.askVolumesByLevel.front());
        double denom = bidV + askV;
        if (denom > 0.0) return (bidP * askV + askP * bidV) / denom;
    } catch (...) {}
    return 0.0;
}

// Choose a reference value and type from available snapshot metrics.
// Returns pair(referenceValue, referenceType) where type: 0=mid,1=vwap,2=sma,3=micro
static std::pair<double,int> selectReference(const OrderBookMetrics &m, double vwap_snap, double sma_snap) {
    double micro = computeMicroPrice(m);
    double refVal = 0.0;
    int refType = 0;
    if (vwap_snap > 0.0) { refVal = vwap_snap; refType = 1; }
    else if (sma_snap > 0.0) { refVal = sma_snap; refType = 2; }
    else if (micro > 0.0) { refVal = micro; refType = 3; }
    else { refVal = m.midPrice; refType = 0; }
    return {refVal, refType};
}

// Build feature vector from detectors results and raw metrics.
// Accept engine pointer so the helper can call member utilities (e.g. calculateImbalanceStrength).
static std::vector<double> buildFeatures(const OrderBookMetrics &m, const std::vector<DetectorResult> &results) {
    std::vector<double> features;
    features.reserve(results.size() + 16);
    for (const auto &r : results) {
        double v = 0.0;
        if (std::isfinite(r.strength) && r.strength > 0.0) {
            v = r.strength;
        } else if (std::isfinite(r.imbalanceValue)) {
            // inline the small calculateImbalanceStrength logic to avoid depending on class private member
            double ratio = r.imbalanceValue;
            double threshold = 1.0;
            double strength;
            if (ratio > threshold) strength = (ratio - threshold) / threshold;
            else if (ratio < 1.0 / threshold) strength = (1.0/threshold - ratio) / (1.0/threshold);
            else strength = 0.0;
            v = std::min(1.0, std::max(0.0, strength));
        }
        features.push_back(v);
    }

    auto safe = [](double v, double def = 0.0) { return std::isfinite(v) ? v : def; };
    double bar = safe(m.bidAskVolumeRatio, 1.0);
    {
        double ratio = bar; double threshold = 1.0; double strength;
        if (ratio > threshold) strength = (ratio - threshold) / threshold;
        else if (ratio < 1.0/threshold) strength = (1.0/threshold - ratio) / (1.0/threshold);
        else strength = 0.0;
        features.push_back(std::min(1.0, std::max(0.0, strength)));
    }
    features.push_back(safe(m.priceVelocity, 0.0));
    features.push_back(std::tanh(safe(m.volumeVelocity, 0.0) / 1000.0));
    features.push_back(std::tanh(safe(m.spreadVelocity, 0.0) * 10.0));

    features.push_back(std::clamp(safe(m.liquidityPressure, 0.0), -10.0, 10.0));
    features.push_back(std::clamp(safe(m.volatilityExpectation, 0.0), 0.0, 10.0));
    features.push_back(std::clamp(safe(m.realizedVolatility, 0.0), 0.0, 100.0));
    features.push_back(std::clamp(safe(m.volatilityPerUnitTime, 0.0), 0.0, 100.0));

    double totalBid = safe(m.totalBidVolume, 0.0);
    double totalAsk = safe(m.totalAskVolume, 0.0);
    {
        double ratio = (totalBid + 1.0) / (totalAsk + 1.0);
        double threshold = 1.0; double strength;
        if (ratio > threshold) strength = (ratio - threshold) / threshold;
        else if (ratio < 1.0/threshold) strength = (1.0/threshold - ratio) / (1.0/threshold);
        else strength = 0.0;
        features.push_back(std::min(1.0, std::max(0.0, strength)));
    }

    double topBidSum = 0.0;
    double topAskSum = 0.0;
    for (size_t i = 0; i < std::min<size_t>(3, m.bidVolumesByLevel.size()); ++i) topBidSum += safe(m.bidVolumesByLevel[i], 0.0);
    for (size_t i = 0; i < std::min<size_t>(3, m.askVolumesByLevel.size()); ++i) topAskSum += safe(m.askVolumesByLevel[i], 0.0);
    {
        double ratio = (topBidSum + 1.0) / (topAskSum + 1.0);
        double threshold = 1.0; double strength;
        if (ratio > threshold) strength = (ratio - threshold) / threshold;
        else if (ratio < 1.0/threshold) strength = (1.0/threshold - ratio) / (1.0/threshold);
        else strength = 0.0;
        features.push_back(std::min(1.0, std::max(0.0, strength)));
    }

    return features;
}
} // inner namespace
 
 // Constructor
ImbalanceDetectionEngine::ImbalanceDetectionEngine() : nextCallbackId_(0) {

    //  VOLUME_IMBALANCE,              // Significant difference between bid and ask volume
    //  PRICE_LEVEL_IMBALANCE,         // Imbalance across multiple price levels
    //  LIQUIDITY_REMOVAL,             // Sudden removal of liquidity from one side
    //  ORDER_FLOW_IMBALANCE,          // Directional bias in order submissions
    //  LARGE_ORDER_APPEARANCE,        // Large orders appearing in the book
    //  LARGE_ORDER_DISAPPEARANCE,     // Large orders being removed from the book
    //  PRICE_LEVEL_CONCENTRATION,     // Concentration of volume at specific price levels
    //  BID_ASK_SPREAD_CHANGE,         // Significant change in the bid-ask spread
    //  MARKET_DEPTH_CHANGE,           // Significant change in market depth
    //  LIQUIDITY_FLOW_IMBALANCE,      // Imbalance in liquidity flow
    //  ABNORMAL_SPREAD,               // Abnormal spread between bid and ask prices
    //  LIQUIDITY_IMBALANCE,           // General liquidity imbalance
    //  TREND_CONTINUATION,            // Continuation of a price trend
    //  TREND_REVERSAL,                // Reversal of a price trend
     // Initialize imbalance state map for all imbalance types
    imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::PRICE_LEVEL_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::ABNORMAL_SPREAD] = false;
        // Add other imbalance types as needed
    imbalanceState_[ImbalanceType::LIQUIDITY_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LIQUIDITY_REMOVAL] = false;
    imbalanceState_[ImbalanceType::ORDER_FLOW_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LARGE_ORDER_APPEARANCE] = false;
    imbalanceState_[ImbalanceType::LARGE_ORDER_DISAPPEARANCE] = false;
    imbalanceState_[ImbalanceType::PRICE_LEVEL_CONCENTRATION] = false;
    imbalanceState_[ImbalanceType::BID_ASK_SPREAD_CHANGE] = false;
    imbalanceState_[ImbalanceType::MARKET_DEPTH_CHANGE] = false;
    imbalanceState_[ImbalanceType::TREND_CONTINUATION] = false;
    imbalanceState_[ImbalanceType::TREND_REVERSAL] = false;
    // Honor environment flag for quick debugging toggles
    const char* env = std::getenv("IMB_DETECT_PUBLISH_EVENTS");
    if (env) {
        std::string v(env);
        for (auto &c: v) c = static_cast<char>(std::tolower(c));
        if (v == "0" || v == "false") {
            publishEvents_.store(false);
            LOG_INFO("ImbalanceDetectionEngine: publishing disabled via IMB_DETECT_PUBLISH_EVENTS={}", v);
        } else {
            publishEvents_.store(true);
            LOG_INFO("ImbalanceDetectionEngine: publishing enabled via IMB_DETECT_PUBLISH_EVENTS={}", v);
        }
    }
 }
ImbalanceDetectionEngine::ImbalanceDetectionEngine(const ImbalanceDetectionConfig& config){
    
     config_ = config;
    // Allow quick runtime disable of publishing detected events for debugging
    // Read environment variable IMB_DETECT_PUBLISH_EVENTS. If set to "0" or "false"
    // then disable publishing by default.
    const char* env = std::getenv("IMB_DETECT_PUBLISH_EVENTS");
    if (env) {
        std::string v(env);
        for (auto &c: v) c = static_cast<char>(std::tolower(c));
        if (v == "0" || v == "false") {
            publishEvents_.store(false);
            LOG_INFO("ImbalanceDetectionEngine: publishing disabled via IMB_DETECT_PUBLISH_EVENTS={}", v);
        } else {
            publishEvents_.store(true);
            LOG_INFO("ImbalanceDetectionEngine: publishing enabled via IMB_DETECT_PUBLISH_EVENTS={}", v);
        }
    }
 }

ImbalanceDetectionEngine::~ImbalanceDetectionEngine() {
    // Stop detector workers if running
    try {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            detectorWorkerRunning_.store(false);
        }
        detectorQueueCv_.notify_all();
        for (auto &t : detectorWorkerThreads_) {
            if (t.joinable()) t.join();
        }
        detectorWorkerThreads_.clear();
    } catch (...) {
        // swallow in destructor
    }
    // Persist bayesian model if configured and destroy adapter handle
    try {
        // First, if a legacy single-file handle exists, save it while holding a minimal scope for the path
        {
            std::lock_guard<std::mutex> lk(bayesMutex_);
            if (bayes_handle_ && !bayesModelPath_.empty()) {
                try {
                    // Ensure parent directory exists
                    try {
                        std::filesystem::path fp(bayesModelPath_);
                        if (fp.has_parent_path()) std::filesystem::create_directories(fp.parent_path());
                    } catch (const std::exception &ex) {
                        LOG_WARN("ImbalanceDetectionEngine: failed to create model directory for {}: {}", bayesModelPath_, ex.what());
                    }
                } catch (...) {}
            }
        }

        // Save legacy single-file model without holding bayesMutex_
        if (bayes_handle_ && !bayesModelPath_.empty()) {
            try {
                if (trading::perception::bayes_save(bayes_handle_, bayesModelPath_)) {
                    LOG_INFO("ImbalanceDetectionEngine: saved bayes model to {}", bayesModelPath_);
                } else {
                    LOG_WARN("ImbalanceDetectionEngine: bayes_save returned false for {}", bayesModelPath_);
                }
            } catch (const std::exception &ex) {
                LOG_WARN("ImbalanceDetectionEngine: failed to save bayes model {}: {}", bayesModelPath_, ex.what());
            } catch (...) {
                LOG_WARN("ImbalanceDetectionEngine: unknown error while saving bayes model {}", bayesModelPath_);
            }
        }

        // Collect per-symbol handles and filenames while holding bayesHandlesMutex_, but do the I/O after
        std::vector<std::pair<std::string, std::array<trading::perception::BayesHandle, ImbalanceDetectionEngine::NUMBER_OF_REGIMES>>> handlesToSave;
        {
            std::lock_guard<std::mutex> lk2(bayesHandlesMutex_);
            for (auto &pr : bayesHandles_) {
                handlesToSave.emplace_back(pr.first, pr.second);
            }
            bayesHandles_.clear();
        }

        // Perform per-symbol saves and destroy handles outside of bayesHandlesMutex_
        for (auto &pr : handlesToSave) {
            const auto &symKey = pr.first;
            auto harray = pr.second; // copy of array of handles
            try {
                if (!bayesModelDirectory_.empty()) {
                            for (int i = 0; i < NUMBER_OF_REGIMES; ++i) {
                                auto h = harray[i];
                                if (!h) continue;
                                try {
                                    std::string filename = bayesModelDirectory_;
                                    if (!filename.empty() && filename.back() != '/' && filename.back() != '\\') filename += '/';
                                    std::string regimeName = marketRegimeToString(static_cast<MarketRegimeType>(i));
                                    filename += symKey + "_" + regimeName + ".json";
                                    trading::perception::bayes_save(h, filename);
                                    LOG_INFO("ImbalanceDetectionEngine: saved per-symbol bayes model {}", filename);
                                } catch (const std::exception &ex) {
                                    LOG_WARN("ImbalanceDetectionEngine: failed to save per-symbol bayes model {}: {}", symKey, ex.what());
                                } catch (...) {
                                    LOG_WARN("ImbalanceDetectionEngine: failed to save per-symbol bayes model {} (unknown error)", symKey);
                                }
                            }
                }
            } catch (...) {
                LOG_WARN("ImbalanceDetectionEngine: failed to save per-symbol bayes model for {}", symKey);
            }
            // destroy handles
            for (int i = 0; i < NUMBER_OF_REGIMES; ++i) {
                try { if (harray[i]) trading::perception::bayes_destroy(harray[i]); } catch(const std::exception &ex) { LOG_WARN("bayes_destroy failed: {}", ex.what()); } catch(...) { LOG_WARN("bayes_destroy failed with unknown error"); }
            }
        }

        if (bayes_handle_) {
            try {
                trading::perception::bayes_destroy(bayes_handle_);
            } catch (const std::exception &ex) { LOG_WARN("bayes_destroy failed for legacy handle: {}", ex.what()); } catch(...) { LOG_WARN("bayes_destroy failed for legacy handle (unknown error)"); }
            bayes_handle_ = nullptr;
            bayesInitialized_.store(false);
            LOG_TRACE("ImbalanceDetectionEngine: destroyed bayes adapter handle");
        }
    } catch (const std::exception &ex) {
        LOG_WARN("ImbalanceDetectionEngine: exception in destructor persistence: {}", ex.what());
    } catch (...) {
        LOG_WARN("ImbalanceDetectionEngine: unknown exception in destructor persistence");
    }
}

// Start detector worker(s) that dequeue metrics and run detectors
void ImbalanceDetectionEngine::startDetectorWorker(size_t threads) {
    if (detectorWorkerRunning_.load()) return;
    if (threads == 0) threads = std::max<size_t>(1, std::thread::hardware_concurrency());

    detectorWorkerRunning_.store(true);
    for (size_t i = 0; i < threads; ++i) {
        detectorWorkerThreads_.emplace_back([this] {
            while (detectorWorkerRunning_.load()) {
                OrderBookMetrics m;
                {
                    std::unique_lock<std::mutex> lk(detectorQueueMutex_);
                    detectorQueueCv_.wait(lk, [this] { return !detectorWorkerRunning_.load() || !detectorQueue_.empty(); });
                    if (!detectorWorkerRunning_.load() && detectorQueue_.empty()) break;
                    if (detectorQueue_.empty()) continue;
                    m = detectorQueue_.front();
                    detectorQueue_.pop_front();
                }

                // Run all detectors sequentially for this metric
                std::vector<DetectorResult> results;
                try {
                    // results.push_back(detectVolumeImbalance(m));
                    // results.push_back(detectPriceLevelImbalance(m));
                    // results.push_back(detectLiquidityFlowImbalance(m));
                    // results.push_back(detectAbnormalSpread(m));
                    // results.push_back(detectMarketDepthChange(m));
                    results.push_back(detectLargeOrderAppearance(m));
                    results.push_back(detectLargeOrderDisappearance(m));
                    results.push_back(detectOrderFlowImbalance(m));
                    results.push_back(detectPriceLevelConcentration(m));
                    results.push_back(detectLiquidityImbalance(m));
                    results.push_back(detectTrendContinuation(m));
                    results.push_back(detectTrendReversal(m));
                    results.push_back(detectTradeImbalance(m));
                    // results.push_back(detectCancellationImbalance(m)); 
                    // results.push_back(detectPriceVolatilityImbalance(m));
                    // results.push_back(detectHiddenLiquidityImbalance(m));
                    // results.push_back(detectCustomImbalance(m));
                } catch (...) {
                    // swallow detector errors
                }

                // Build feature vector from detectors + raw metrics (extracted via helper)
                std::vector<double> features = buildFeatures(m, results);

                // Always buffer training data. Regime is sourced exclusively from the
                // OrderBookMetrics metric "marketRegime" (set by OrderBookAnalyzer).
                
                // Buffer features for later labeling/training - always enable training
                {
                    std::lock_guard<std::mutex> lk_pending(pendingTrainingMutex_);
                    if (pendingTraining_.size() >= maxPendingTraining_) pendingTraining_.pop_front();
                    double vwap_snap = 0.0;
                    double sma_snap = 0.0;
                    try {
                        vwap_snap = m.getMetric("vwap", 0.0);
                        sma_snap = m.getMetric("sma", 0.0);
                    } catch (...) {}

                    auto ref = selectReference(m, vwap_snap, sma_snap);
                    PendingTraining pt;
                    pt.timestamp = m.timestamp;
                    pt.features = features;
                    pt.midPrice = m.midPrice;
                    pt.vwap = vwap_snap;
                    pt.sma = sma_snap;
                    pt.referenceValue = ref.first;
                    pt.referenceType = ref.second;
                    pt.exchangeId = m.exchangeId;
                    pt.symbol = m.symbol;
                    // Snapshot regime from metrics (default to 0)
                    try {
                        if (m.metrics.count("marketRegime")) {
                            double rv = m.metrics.at("marketRegime");
                            int ri = static_cast<int>(rv);
                            if (ri >= 0 && ri < ImbalanceDetectionEngine::NUMBER_OF_REGIMES) pt.regime = ri; else pt.regime = 0;
                        } else pt.regime = 0;
                    } catch (...) { pt.regime = 0; }
                    pendingTraining_.push_back(pt);
                    // Diagnostic: log captured regime for this buffered sample
                    LOG_TRACE("ImbalanceDetectionEngine: buffered training sample for {} regime={}", pt.symbol.toString(), pt.regime);

                        // Evaluate warmup gating immediately after buffering a sample.
                        if (!trainingEnabled_.load()) {
                            bool enable = false;
                            try {
                                auto now_tp = std::chrono::steady_clock::now();

                                // If configured delay is zero or negative, treat as immediate enable
                                if (config_.trainingStartDelayMs <= 0) {
                                    enable = true;
                                } else {
                                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - trainingStartTime_).count();
                                    if (elapsed >= config_.trainingStartDelayMs) enable = true;
                                }

                                // If not enabled yet, evaluate orderbook depth condition.
                                // A zero or negative minOrderbookLevels means 'no minimum' -> enable immediately.
                                if (!enable) {
                                    if (config_.minOrderbookLevels <= 0) {
                                        enable = true;
                                    } else {
                                        if (m.bidVolumesByLevel.size() >= static_cast<size_t>(config_.minOrderbookLevels) &&
                                            m.askVolumesByLevel.size() >= static_cast<size_t>(config_.minOrderbookLevels)) {
                                            enable = true;
                                        }
                                    }
                                }
                            } catch (...) {}
                            if (enable) {
                                trainingEnabled_.store(true);
                                LOG_INFO("ImbalanceDetectionEngine: training warmup complete, enabling online training (buffered={})", pendingTraining_.size());
                            }
                        }

                }

                // Predict with Bayesian model and emit event if needed
                if (bayesInitialized_.load()) {
                    try {
                        trading::perception::BayesSignal signal;
                        // Get per-regime handles and use only the active regime for prediction
                        auto handles = getBayesHandlesForSymbol(m.exchangeId, m.symbol);
                        // loading/saving per-regime Bayesian models.
                        int activeRegime = 0;
                        double maxProb = 0.0; // Start from 0 to allow any regime to be selected
                        bool hasRegimeData = false;

                        // Prefer metric-provided regime when present
                        try {
                            if (m.metrics.count("marketRegime")) {
                                double rv = m.metrics.at("marketRegime");
                                int ri = static_cast<int>(rv);
                                if (ri >= 0 && ri < ImbalanceDetectionEngine::NUMBER_OF_REGIMES) {
                                    activeRegime = ri;
                                    hasRegimeData = true;
                                    LOG_TRACE("ImbalanceDetectionEngine: using marketRegime metric={} for symbol {}", activeRegime, m.symbol.toString());
                                }
                            }
                        } catch (...) {}

                        
                        // Use only the active regime model for prediction
                        trading::perception::BayesSignal activeSignal{};
                        
                        // Lazy create handle for active regime if needed
                        if (!handles[activeRegime]) {
                            try {
                                handles[activeRegime] = trading::perception::bayes_create();
                                // Update the cached handles
                                {
                                    std::lock_guard<std::mutex> lk(bayesHandlesMutex_);
                                    bayesHandles_[m.exchangeId.toString() + "_" + m.symbol.toString()] = handles;
                                }
                                LOG_TRACE("ImbalanceDetectionEngine: lazy created bayes handle for regime {}", activeRegime);
                            } catch (...) {
                                handles[activeRegime] = nullptr;
                            }
                        }
                        
                        auto activeHandle = handles[activeRegime];
                        if (activeHandle) {
                            try {
                                activeSignal = trading::perception::bayes_predict(activeHandle, features);
                                LOG_TRACE("ImbalanceDetectionEngine: Active regime {} prediction: prob={:.3f}, dir={}, should_trade={}", 
                                         activeRegime, activeSignal.probability, 
                                         (activeSignal.direction == core::Direction::BUY ? "BUY" : 
                                          activeSignal.direction == core::Direction::SELL ? "SELL" : "NEUTRAL"),
                                         activeSignal.should_trade);
                            } catch (const std::exception &ex) {
                                LOG_WARN("bayes_predict threw exception for regime {}: {}", activeRegime, ex.what());
                            } catch (...) {
                                LOG_WARN("bayes_predict threw unknown exception for regime {}", activeRegime);
                            }
                        }
                        signal = activeSignal;
                        // Enforce minimum model updates before allowing Bayes-driven trades
                        int maxModelUpdates = 0;
                        for (int i=0;i<ImbalanceDetectionEngine::NUMBER_OF_REGIMES;i++) {
                            if (!handles[i]) continue;
                            try {
                                int mu = trading::perception::bayes_get_num_updates(handles[i]);
                                if (mu > maxModelUpdates) maxModelUpdates = mu;
                            } catch (const std::exception &ex) { LOG_WARN("bayes_get_num_updates threw: {}", ex.what()); } catch (...) { LOG_WARN("bayes_get_num_updates threw unknown"); }
                        }

                        if (maxModelUpdates < bayesMinUpdatesToTrade_) {
                            // not enough training yet — don't trade
                            LOG_TRACE("ImbalanceDetectionEngine: bayes model updates too low (max={} < min={}), skipping trade", maxModelUpdates, bayesMinUpdatesToTrade_);
                        } else if (signal.should_trade && signal.position_size > 0.0) {
                            core::Direction dir = signal.direction;
                            std::string symbol_str = m.symbol.toString();
                            // ❌ EXIT LOGIC REMOVED: Only handle ENTRY signals here
                            if (dir != core::Direction::NEUTRAL) {
                                bool authoritativeHasPosition = false;
                                double authoritativeQty = 0.0;
                                // Check authoritative position
                                try {
                                    if (positionLookup_) {
                                        auto pos = positionLookup_(m.exchangeId, m.symbol);
                                        if (pos.has_value() && std::abs(pos.value()) > 1e-8) {
                                            authoritativeHasPosition = true;
                                            authoritativeQty = pos.value();
                                        }
                                    }
                                } catch (const std::exception &e) {
                                    LOG_WARN("ImbalanceDetectionEngine: positionLookup threw: {}", e.what());
                                }
                                // Calculate spreads (use PERCENT not ratio!)
                                double current_spread_percent = (m.spread / m.midPrice) * 100.0;
                                double ema_spread_percent = (m.emaSpread / m.midPrice) * 100.0;
                                MarketMicrostructure microstructure;
                                microstructure.volatility_1m = m.realize_volatility_1m;
                                microstructure.spread_ema = m.emaSpread;
                                microstructure.spread_std = m.metrics.count("spread_std") ? m.metrics.at("spread_std") : 0.0;
                                microstructure.orderbook_imbalance = m.bidAskVolumeRatio;
                                // microstructure.trade_intensity = 
                                // microstructure.price_impact = m.estimatedPriceImpact;
                                //get tick size from con
                                microstructure.tick_size = getConfiguredTickSize(m.exchangeId, m.symbol);
                                // microstructure.volume_profile_skew = m.volumeProfileSkew;
                                microstructure.mid_price = m.midPrice;

                                // Debug: log spread units to verify whether spread_ema is raw price or percent
                                LOG_TRACE("imbalance_engine: symbol={} raw_spread={} mid={} spread_pct={}",
                                          symbol_str, m.spread, m.midPrice,
                                          (m.midPrice>0.0 ? (m.spread / m.midPrice * 100.0) : 0.0));

                                if (authoritativeHasPosition) {
                                    LOG_INFO("ENTRY_BLOCKED_BY_POSITION: {} has position qty={}", symbol_str, authoritativeQty);
                                } 
                                // else if (g_optimalExit.wasRecentlyClosed(symbol_str)) {
                                //     LOG_INFO("ENTRY_BLOCKED_BY_RECENT_CLOSE: {} cooldown", symbol_str);
                                // }
                                else {
                                        // Decide whether to allow entry. If the global AdaptiveExitStrategy
                                        // isn't registered yet we should NOT permanently block entries -
                                        // instead allow them and emit the entry signal so downstream
                                        // components (Execution/Risk/PositionTracker) can create an
                                        // authoritative position record. Once the global pointer is
                                        // registered later, it will be used for tracking.
                                        auto opt = trading::strategy::getGlobalAdaptiveExitStrategy();
                                        bool allowEntry = true;
                                        if (opt) {
                                            if (!opt->shouldEnterPosition(symbol_str, microstructure, signal.probability)) {
                                                allowEntry = false;
                                            }
                                        } else {
                                            // Warn only periodically to avoid log spam
                                            static std::atomic<int> warnCounter{0};
                                            if ((warnCounter.fetch_add(1, std::memory_order_relaxed) % 500) == 0) {
                                                LOG_WARN("ENTRY_CHECK: AdaptiveExitStrategy not registered yet, allowing entries until registered");
                                            }
                                            // allowEntry stays true so signals flow through
                                        }

                                        if (allowEntry) {
                                            // Use a single microstructure snapshot for both tracking and event metadata
                                            MarketMicrostructure micro;
                                            micro.volatility_1m = m.realize_volatility_1m;
                                            micro.spread_ema = m.emaSpread;
                                            micro.spread_std = m.metrics.count("spread_std") ? m.metrics.at("spread_std") : 0.0;
                                            micro.orderbook_imbalance = m.bidAskVolumeRatio;
                                            micro.tick_size = getConfiguredTickSize(m.exchangeId, m.symbol);
                                            micro.mid_price = m.midPrice;
                                            // If exit strategy becomes available, register the start there too
                                            auto opt2 = trading::strategy::getGlobalAdaptiveExitStrategy();
                                            if (opt2) {
                                                try {
                                                    opt2->startPosition(symbol_str, m.midPrice, dir, signal.probability, micro);
                                                    LOG_TRACE("STARTED TRACKING POSITION: {} dir={} entry={:.6f} conf={:.3f} spread_ema={:.3f}% spread_cur={:.3f}%",
                                                            symbol_str,
                                                            (dir == core::Direction::BUY ? "BUY" : "SELL"),
                                                            m.midPrice,
                                                            signal.probability,
                                                            ema_spread_percent,
                                                            current_spread_percent);
                                                } catch (const std::exception &e) {
                                                    LOG_WARN("startPosition threw: {}", e.what());
                                                } catch (...) {
                                                    LOG_WARN("startPosition threw unknown");
                                                }
                                            } else {
                                                LOG_TRACE("START_POSITION: AdaptiveExitStrategy not registered, proceeding without in-memory tracking for {}", symbol_str);
                                            }

                                            // Emit an entry event and bayes metadata so downstream systems
                                            // (Execution/Risk) and the position tracker can record the position
                                            OrderBookMetrics mcopy = m;
                                            mcopy.metrics["bayes_probability"] = signal.probability;
                                            mcopy.metrics["bayes_expected_value"] = signal.expected_value;
                                            mcopy.metrics["bayes_position_size"] = signal.position_size;
                                            mcopy.metrics["bayes_uncertainty"] = signal.uncertainty;
                                            mcopy.metrics["entry_signal"] = 1.0;

                                            createImbalanceEventForBayesModel(ImbalanceType::CUSTOM, signal.position_size,
                                                                            dir, mcopy, signal.probability, signal.uncertainty);

                                            LOG_INFO("ENTRY SIGNAL: {} {} @ {:.6f} (conf={:.3f})",
                                                    symbol_str, (dir == core::Direction::BUY ? "BUY" : "SELL"),
                                                    m.midPrice, signal.probability);
                                        }
                                }

                            }

                        }
                    }
                    catch (const std::exception &ex) {
                        LOG_WARN("Bayes predict threw: {}", ex.what());
                    } catch (...) {
                        LOG_WARN("Bayes predict threw unknown");
                    }
                }

                // Process pending training samples (only when training enabled)
                if (trainingEnabled_.load()) {
                    try {
                        // tuple: features, label, exchange, symbol, regime
                        std::vector<std::tuple<std::vector<double>, int, core::ExchangeId, core::Symbol, int>> toTrain;
                        {
                            std::lock_guard<std::mutex> lk_pending2(pendingTrainingMutex_);
                            auto workerNow = m.timestamp;
                            while (!pendingTraining_.empty()) {
                                const auto &pt = pendingTraining_.front();
                                // Determine 'now' relative to this pending sample's symbol.
                                // Prefer the latest per-symbol snapshot timestamp; fallback to workerNow.
                                core::Timestamp nowForPt = workerNow;
                                try {
                                    std::lock_guard<std::mutex> lk(latestSnapshotsMutex_);
                                    std::string key = pt.exchangeId.toString() + "_" + pt.symbol.toString();
                                    auto it = latestSnapshots_.find(key);
                                    if (it != latestSnapshots_.end()) nowForPt = it->second.timestamp;
                                } catch (...) {}

                                if (nowForPt - pt.timestamp >= labelingDelay_) {
                                        // Use the stored reference at capture if available, otherwise re-evaluate using
                                        // the *latest snapshot for the same exchange+symbol* to avoid cross-symbol leakage.
                                        double baseRef = pt.referenceValue;
                                        double target = pt.referenceValue;
                                        try {
                                            // Try find latest snapshot for this exchange+symbol
                                            OrderBookMetrics snapshot;
                                            bool haveSnapshot = false;
                                            try {
                                                std::lock_guard<std::mutex> lk(latestSnapshotsMutex_);
                                                std::string key = pt.exchangeId.toString() + "_" + pt.symbol.toString();
                                                auto it = latestSnapshots_.find(key);
                                                if (it != latestSnapshots_.end()) { snapshot = it->second; haveSnapshot = true; }
                                            } catch (...) { haveSnapshot = false; }

                                            // Prefer snapshot values; fallback to current 'm' if same symbol or snapshot missing
                                            const OrderBookMetrics *useMetrics = nullptr;
                                            if (haveSnapshot) useMetrics = &snapshot;
                                            else useMetrics = &m; // fallback

                                            if (pt.referenceType == 1) { // vwap
                                                double cur = useMetrics->getMetric("vwap", 0.0);
                                                if (cur > 0.0) target = cur; else target = baseRef;
                                            } else if (pt.referenceType == 2) { // sma
                                                double cur = useMetrics->getMetric("sma", 0.0);
                                                if (cur > 0.0) target = cur; else target = baseRef;
                                            } else if (pt.referenceType == 3) { // micro
                                                double bidP = useMetrics->bidPrice; double askP = useMetrics->askPrice;
                                                double bidV = (useMetrics->bidVolumesByLevel.empty() ? useMetrics->totalBidVolume : useMetrics->bidVolumesByLevel.front());
                                                double askV = (useMetrics->askVolumesByLevel.empty() ? useMetrics->totalAskVolume : useMetrics->askVolumesByLevel.front());
                                                double denom = bidV + askV;
                                                if (denom > 0.0) target = (bidP * askV + askP * bidV) / denom; else target = baseRef;
                                            } else { target = useMetrics->midPrice; }
                                        } catch (...) { target = baseRef; }

                                        double priceChangeLogRet = 0.0;
                                        if (baseRef > 0.0 && std::isfinite(baseRef) && std::isfinite(target) && target > 0.0) {
                                            priceChangeLogRet = std::log(target) - std::log(baseRef);
                                        }

                                        // FIXED: More robust volatility normalization with strict bounds
                                        // Use volatility from the same snapshot used for target calculation when possible
                                        double vol = 0.0;
                                        try {
                                            std::lock_guard<std::mutex> lk(latestSnapshotsMutex_);
                                            std::string key = pt.exchangeId.toString() + "_" + pt.symbol.toString();
                                            auto it = latestSnapshots_.find(key);
                                            if (it != latestSnapshots_.end()) vol = it->second.realizedVolatility; else vol = m.realizedVolatility;
                                        } catch (...) { vol = m.realizedVolatility; }
                                        vol = std::clamp(vol, 0.001, 0.1); // Tighter vol range: 0.1% - 10%
                                        double normalizedReturn = 0.0;
                                        if (vol > 0.0 && std::isfinite(vol) && std::isfinite(priceChangeLogRet)) {
                                            normalizedReturn = priceChangeLogRet / vol;
                                            // CRITICAL: Clamp normalized return to prevent extreme outliers
                                            normalizedReturn = std::clamp(normalizedReturn, -10.0, 10.0);
                                        }

                                        int regimeIdx = pt.regime;

                                        // Use configured minimum profitable return (if set) to prevent trading on tiny expected profits
                                        double cfgMinProf = config_.min_profitable_return;
                                        auto dyn = g_statThresholdAdapter.calculateThresholds(regimeIdx, vol, cfgMinProf);

                                        // Calculate confidence based on signal strength
                                        double confidence = 0.5 + 0.3 * std::abs(normalizedReturn) / std::max(vol, 1e-6);
                                        confidence = std::clamp(confidence, 0.0, 1.0);

                                        // ENHANCED: Profit-aware labeling - only label if expected profit > fees
                                        core::Direction labelDirection = g_profitLabeling.generateProfitAwareLabel(
                                            normalizedReturn, regimeIdx, confidence, vol
                                        );
                                        
                                        // DEBUG: Track labeling attempts
                                        static int labelCount = 0;
                                        static int buyCount = 0, sellCount = 0, neutralCount = 0;
                                        labelCount++;
                                        
                                        // Fallback to traditional thresholding if profit-aware doesn't have enough data
                                        if (labelDirection == core::Direction::NEUTRAL && 
                                            confidence > 0.6) { // Reduced fallback threshold from 0.7 to 0.6
                                            if (normalizedReturn > dyn.buyThreshold) {
                                                labelDirection = core::Direction::BUY;
                                            } else if (normalizedReturn < dyn.sellThreshold) {
                                                labelDirection = core::Direction::SELL;
                                            }
                                        }
                                        
                                        // Count labels for debugging
                                        if (labelDirection == core::Direction::BUY) buyCount++;
                                        else if (labelDirection == core::Direction::SELL) sellCount++;
                                        else neutralCount++;
                                        
                                        // FIX 4: Add diagnostic logging to track the issue
                                        if (labelCount % 100 == 0) {
                                            // Add diagnostic info about the data distribution
                                            auto dataRange = g_statThresholdAdapter.getDataRange(regimeIdx);
                                            size_t dataPoints = g_statThresholdAdapter.getDataSize(regimeIdx);
                                            double minReturn = dataRange.first;
                                            double maxReturn = dataRange.second;
                                            
                                            // Check for data quality issues
                                            bool dataIssue = (std::abs(dyn.buyThreshold) > 10.0 || 
                                                            std::abs(dyn.sellThreshold) > 10.0 || 
                                                            std::abs(normalizedReturn) > 20.0 ||
                                                            std::abs(minReturn) > 100.0 ||
                                                            std::abs(maxReturn) > 100.0);
                                            
                                            if (dataIssue) {
                                                LOG_WARN("DATA ISSUE: BUY={}, SELL={}, NEUTRAL={}, "
                                                    "BuyThresh={:.3f}, SellThresh={:.3f}, NormRet={:.3f}, "
                                                    "DataRange=[{:.3f}, {:.3f}], DataPoints={}", 
                                                    buyCount, sellCount, neutralCount, 
                                                    dyn.buyThreshold, dyn.sellThreshold, normalizedReturn,
                                                    minReturn, maxReturn, dataPoints);
                                            } else {
                                                LOG_TRACE("Label stats: BUY={}, SELL={}, NEUTRAL={}, "
                                                    "BuyThresh={:.3f}, SellThresh={:.3f}, NormRet={:.3f}", 
                                                    buyCount, sellCount, neutralCount, 
                                                    dyn.buyThreshold, dyn.sellThreshold, normalizedReturn);
                                            }
                                        }
                                        
                                        // Type-safe conversion to integer label
                                        int label = static_cast<int>(labelDirection);

                                        // Thread-safe statistics tracking with proper logging
                                        trackLabelingEvent(labelDirection, pt.symbol.toString(), normalizedReturn, 
                                                        std::chrono::duration_cast<std::chrono::milliseconds>(nowForPt - pt.timestamp));

                                        // Calculate realized profit (after trading fees) 
                                        // preserve sign: positive if price moved in predicted direction, negative otherwise
                                        double realizedPct = std::exp(priceChangeLogRet) - 1.0;
                                        double tradingFee = 0.001; // 0.1% fee
                                        double realizedProfit = realizedPct - tradingFee;
                                        
                                        // Update profit-aware labeling with realized outcome
                                        g_profitLabeling.updateHistory(normalizedReturn, realizedProfit, 
                                                                    regimeIdx, confidence, vol);
                                        
                                        // record for training and update stats with realized (non-normalized) absolute return
                                        toTrain.emplace_back(pt.features, label, pt.exchangeId, pt.symbol, pt.regime);
                                        g_statThresholdAdapter.updateStats(regimeIdx, priceChangeLogRet, vol);
                                        pendingTraining_.pop_front();
                                } else break;
                            }
                        }
                        for (auto &t : toTrain) {
                            try {
                                const auto &features = std::get<0>(t);
                                int label = std::get<1>(t);
                                const auto &exch = std::get<2>(t);
                                const auto &sym = std::get<3>(t);
                                int sampleRegime = std::get<4>(t);

                                // Get the per-regime handles
                                auto handles = getBayesHandlesForSymbol(exch, sym);
                                bool anyHandle = false;
                                for (int i = 0; i < ImbalanceDetectionEngine::NUMBER_OF_REGIMES; ++i) if (handles[i]) { anyHandle = true; break; }
                                if (!anyHandle) continue;

                                // Single-regime update: use the regime snapshot captured with the sample
                                bool anyUpdated = false;
                                int activeRegime = 0;
                                if (sampleRegime >= 0 && sampleRegime < ImbalanceDetectionEngine::NUMBER_OF_REGIMES) activeRegime = sampleRegime;
                                // Lazy create handle if needed
                                if (!handles[activeRegime]) {
                                    try {
                                        handles[activeRegime] = trading::perception::bayes_create();
                                        // Update cached handles
                                        {
                                            std::lock_guard<std::mutex> lk(bayesHandlesMutex_);
                                            std::string key = exch.toString() + "_" + sym.toString();
                                            bayesHandles_[key] = handles;
                                        }
                                        LOG_TRACE("ImbalanceDetectionEngine: lazy created bayes handle for regime {} ({}:{})", 
                                                activeRegime, exch.toString(), sym.toString());
                                    } catch (...) {
                                        handles[activeRegime] = nullptr;
                                    }
                                }
                                if (handles[activeRegime]) {
                                    trading::perception::bayes_update(handles[activeRegime], features, label);
                                    anyUpdated = true;
                                    LOG_TRACE("ImbalanceDetectionEngine: Updated regime {} for {}:{}", activeRegime, exch.toString(), sym.toString());
                                }
                                
                                if (!anyUpdated) {
                                    LOG_TRACE("ImbalanceDetectionEngine: no bayes handle available for active regime {} ({}:{}), skipping update", 
                                            activeRegime, exch.toString(), sym.toString());
                                    continue;
                                }

                                // Runtime persistence policy: increment update counter and
                                // save model when thresholds / intervals are reached.
                                try {
                                    // Update counters atomically with bayes mutex to avoid races
                                    std::lock_guard<std::mutex> lk_bayes(bayesMutex_);
                                    updatesSinceLastSave_++;
                                    auto now = std::chrono::steady_clock::now();
                                    bool minIntervalPassed = (now - lastSaveTime_) >= bayesMinSaveInterval_;
                                    bool intervalPassed = (bayesSaveInterval_.count() > 0) && ((now - lastSaveTime_) >= bayesSaveInterval_);
                                    bool enoughUpdates = (bayesSaveAfterUpdates_ > 0) && (updatesSinceLastSave_ >= bayesSaveAfterUpdates_);

                                    // Check model readiness if configured - use maximum updates across all handles
                                    int maxModelUpdates = 0;
                                    bool anyModelReady = false;
                                    for (int i = 0; i < ImbalanceDetectionEngine::NUMBER_OF_REGIMES; ++i) {
                                        if (!handles[i]) continue;
                                        try {
                                            int mu = trading::perception::bayes_get_num_updates(handles[i]);
                                            if (mu > maxModelUpdates) maxModelUpdates = mu;
                                            if (trading::perception::bayes_is_ready(handles[i])) anyModelReady = true;
                                        } catch (...) {}
                                    }
                                    bool modelReady = (maxModelUpdates >= requireModelReadyUpdates_) || anyModelReady;

                                    if (modelReady && minIntervalPassed && (enoughUpdates || intervalPassed)) {
                                        // Collect save tasks while holding lock then perform I/O outside
                                        std::vector<std::pair<trading::perception::BayesHandle, std::string>> saveTasks;
                                        if (!bayesModelDirectory_.empty()) {
                                            try {
                                                // Only persist the regime that was just updated (activeRegime)
                                                std::string base = bayesModelDirectory_;
                                                if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
                                                auto sanitize = [](const std::string &s){ std::string out; for (char c: s) { if (std::isalnum((unsigned char)c) || c=='_' || c=='-') out.push_back(c); else out.push_back('_'); } return out; };
                                                std::string name = sanitize(exch.toString()) + "_" + sanitize(sym.toString());
                                                int regimeToSave = activeRegime;
                                                if (regimeToSave < 0 || regimeToSave >= NUMBER_OF_REGIMES) regimeToSave = 0;
                                                auto h = handles[regimeToSave];
                                                if (h) {
                                                    // Diagnostic: log which regime we're about to persist and its update count
                                                    try {
                                                        int mu = trading::perception::bayes_get_num_updates(h);
                                                        LOG_DEBUG("ImbalanceDetectionEngine: preparing auto-save for regime {} (updates={}) for {}:{}", regimeToSave, mu, exch.toString(), sym.toString());
                                                    } catch (...) {
                                                        LOG_TRACE("ImbalanceDetectionEngine: preparing auto-save for regime {} (updates=unknown) for {}:{}", regimeToSave, exch.toString(), sym.toString());
                                                    }
                                                    std::string regimeName = marketRegimeToString(static_cast<MarketRegimeType>(regimeToSave));
                                                    std::string filename = base + name + "_" + regimeName + ".json";
                                                    saveTasks.emplace_back(h, filename);
                                                }
                                            } catch (const std::exception &ex) { LOG_WARN("error preparing per-symbol save tasks: {}", ex.what()); } catch(...) { LOG_WARN("error preparing per-symbol save tasks (unknown)"); }
                                        } else if (!bayesModelPath_.empty() && bayes_handle_) {
                                            saveTasks.emplace_back(bayes_handle_, bayesModelPath_);
                                        }

                                        // Reset counters / timestamp now while still holding lock
                                        lastSaveTime_ = now;
                                        updatesSinceLastSave_ = 0;

                                        // release lock scope here
                                        // Perform I/O for save tasks outside of bayesMutex_
                                        for (auto &st : saveTasks) {
                                            try {
                                                if (trading::perception::bayes_save(st.first, st.second)) {
                                                    LOG_INFO("ImbalanceDetectionEngine: auto-saved bayes model to {}", st.second);
                                                } else {
                                                    LOG_WARN("ImbalanceDetectionEngine: auto-save returned false for {}", st.second);
                                                }
                                            } catch (const std::exception &ex) {
                                                LOG_WARN("ImbalanceDetectionEngine: exception during auto-save for {}: {}", st.second, ex.what());
                                            } catch (...) {
                                                LOG_WARN("ImbalanceDetectionEngine: unknown error during auto-save for {}", st.second);
                                            }
                                        }
                                    }
                                } catch (const std::exception &ex) {
                                    LOG_WARN("save bookkeeping threw: {}", ex.what());
                                } catch (...) {
                                    LOG_WARN("save bookkeeping threw unknown");
                                }
                            } catch (...) {}
                        }
                        // end if(trainingEnabled_)
                    } catch (...) {}
                }
            }
        
        });
    }      
    
    LOG_INFO("ImbalanceDetectionEngine: started {} detector worker threads", threads);
}


 // Initialize the detection engine with configuration
 bool ImbalanceDetectionEngine::initialize(const ImbalanceDetectionConfig& config) {
    config_ = config;
     
     // Initialize thresholds with defaults if not specified
     if (thresholds_.empty()) {
        thresholds_.setThreshold("volumeImbalance", 1.2);        // Was 2.0 → now 20% imbalance triggers
        thresholds_.setThreshold("emaVolumeImbalance", 1.1);     // Was 1.5 → EMA smoother, so lower
        thresholds_.setThreshold("priceLevelImbalance", 1.2);    // Was 1.5 → make it more sensitive
        thresholds_.setThreshold("liquidity_flow_imbalance", 1.5); // Was 3.0 → too extreme before
        thresholds_.setThreshold("liquidity_removal", 1.2);      // Was 1.5 → detect smaller removals
        thresholds_.setThreshold("abnormal_spread", 1.4);        // Was 2.5 → flag earlier widening
        thresholds_.setThreshold("marketDepthChange", 0.3);      // 30% change in depth
        thresholds_.setThreshold("largeOrderThreshold", 0.02);   // 2% of top level volume
        thresholds_.setThreshold("orderFlowImbalance", 0.6);     // 60% of orders on one side
        thresholds_.setThreshold("priceLevelConcentration", 0.5); // 50% of volume at one level
        thresholds_.setThreshold("liquidityImbalance", 0.6);     // 60% of liquidity on one side
        thresholds_.setThreshold("trendContinuation", 0.4);      // 40% price move in trend direction
        thresholds_.setThreshold("trendReversal", 0.4);          // 40% price move against trend
        thresholds_.setThreshold("tradeImbalance", 0.6);         // 60% of trades on one side
        thresholds_.setThreshold("cancellationImbalance", 0.6);  // 60% of cancellations on one side
        thresholds_.setThreshold("priceVolatilityImbalance", 0.3);// 30% price volatility change
     }
             
     // Clear any existing state
     clearImbalances();

    // Configure bayes model path from config. Persistence is opt-in: if the
    // config does not provide a path we leave bayesModelPath_ empty and
    // therefore disable model load/save.
    if (!config_.bayesModelPath.empty()) {
        // Decide whether the configured path is a directory (per-symbol mode) or a file (legacy single-file mode).
        std::filesystem::path p(config_.bayesModelPath);
        bool treatAsDirectory = false;
        try {
            if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
                treatAsDirectory = true;
            } else if (!p.has_extension()) {
                // no extension -> likely a directory path (even if it doesn't exist yet)
                treatAsDirectory = true;
            }
        } catch (...) {
            // fallback: if path has no extension treat as directory
            treatAsDirectory = !p.has_extension();
        }

        if (treatAsDirectory) {
            bayesModelDirectory_ = config_.bayesModelPath;
            bayesModelPath_.clear();
            LOG_INFO("ImbalanceDetectionEngine: bayes per-symbol persistence enabled, model directory={}", bayesModelDirectory_);
        } else {
            // treat as single-file legacy mode
            bayesModelPath_ = config_.bayesModelPath;
            bayesModelDirectory_.clear();
            LOG_INFO("ImbalanceDetectionEngine: bayes single-file persistence enabled, model path={}", bayesModelPath_);
        }
    } else {
        bayesModelPath_.clear();
        bayesModelDirectory_.clear();
        LOG_INFO("ImbalanceDetectionEngine: bayes persistence disabled (no model path configured)");
    }

    // Configure optional save policies from config
    if (config_.bayesSaveIntervalMs > 0) {
        bayesSaveInterval_ = std::chrono::milliseconds(config_.bayesSaveIntervalMs);
        LOG_INFO("ImbalanceDetectionEngine: bayes save interval set to {} ms", config_.bayesSaveIntervalMs);
    }
    if (config_.bayesSaveAfterUpdates > 0) {
        bayesSaveAfterUpdates_ = config_.bayesSaveAfterUpdates;
        LOG_INFO("ImbalanceDetectionEngine: bayes will save after every {} model updates", bayesSaveAfterUpdates_);
    }
    // Require model to have this many updates before allowing persistence
    if (config_.requireModelReadyUpdates > 0) {
        requireModelReadyUpdates_ = config_.requireModelReadyUpdates;
        LOG_INFO("ImbalanceDetectionEngine: requireModelReadyUpdates set to {}", requireModelReadyUpdates_);
    }
    // Minimum model updates required before allowing trades
    if (config_.bayesMinUpdatesToTrade > 0) {
        bayesMinUpdatesToTrade_ = config_.bayesMinUpdatesToTrade;
        LOG_INFO("ImbalanceDetectionEngine: bayesMinUpdatesToTrade set to {}", bayesMinUpdatesToTrade_);
    }
    // Minimum debounce interval between saves
    if (config_.bayesMinSaveIntervalMs > 0) {
        bayesMinSaveInterval_ = std::chrono::milliseconds(config_.bayesMinSaveIntervalMs);
        LOG_INFO("ImbalanceDetectionEngine: bayes min save interval set to {} ms", config_.bayesMinSaveIntervalMs);
    }

    // Optional labeling delay from config (in ms)
    if (config_.labelingDelayMs > 0) labelingDelay_ = std::chrono::milliseconds(config_.labelingDelayMs);
    // Training warmup gating: initialize start time and min levels
    trainingStartTime_ = std::chrono::steady_clock::now();
    minOrderbookLevels_ = config_.minOrderbookLevels;
    if (config_.trainingStartDelayMs <= 0 && config_.minOrderbookLevels <= 0) {
        trainingEnabled_.store(true);
    } else {
        trainingEnabled_.store(false);
        LOG_INFO("ImbalanceDetectionEngine: training warmup enabled (startDelayMs={}, minOrderbookLevels={})", config_.trainingStartDelayMs, config_.minOrderbookLevels);
    }
    

    // Create and load Bayesian model adapter handle upfront for legacy single-file mode.
    try {
        std::lock_guard<std::mutex> lk(bayesMutex_);
        if (!bayesModelPath_.empty()) {
            // legacy single-file mode: create a shared global handle
            if (!bayes_handle_) {
                bayes_handle_ = trading::perception::bayes_create();
                LOG_TRACE("ImbalanceDetectionEngine: created bayes adapter handle (single-file mode)");
            }
            if (bayes_handle_) {
                try {
                    if (trading::perception::bayes_load(bayes_handle_, bayesModelPath_)) {
                        LOG_INFO("ImbalanceDetectionEngine: loaded bayes model from {}", bayesModelPath_);
                    } else {
                        LOG_WARN("ImbalanceDetectionEngine: bayes_load returned false for {}", bayesModelPath_);
                    }
                } catch (const std::exception &ex) {
                    LOG_WARN("ImbalanceDetectionEngine: exception while loading bayes model {}: {}", bayesModelPath_, ex.what());
                } catch (...) {
                    LOG_WARN("ImbalanceDetectionEngine: unknown error while loading bayes model {}", bayesModelPath_);
                }
            }
        }
        // Per-symbol mode creates handles lazily; still consider model subsystem initialized
        bayesInitialized_.store(true);
        // Initialize lastSaveTime_ to now to avoid treating an uninitialized
        // time_point as long-ago and triggering an immediate interval save.
        lastSaveTime_ = std::chrono::steady_clock::now();
        LOG_TRACE("ImbalanceDetectionEngine: bayes lastSaveTime initialized");
    } catch (const std::exception &ex) {
        LOG_WARN("ImbalanceDetectionEngine: failed to create bayes adapter: {}", ex.what());
    } catch (...) {
        LOG_WARN("ImbalanceDetectionEngine: unknown error creating bayes adapter");
    }
    

    // Start background detector worker so analyzeMetrics can offload work.
    try {
        startDetectorWorker(); 
    } catch (...) {
        // ignore
    }
     
     return true;
 }

 bool ImbalanceDetectionEngine::analyzeMetrics(const OrderBookMetrics& metrics) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& metric : metrics.metrics) {
            metricHistory_[metric.first] = metric.second;
        }
        if (!lastMetrics_) {
            lastMetrics_ = metrics;
            return false;
        }
    }

    if (metrics.bidVolumesByLevel.empty() || metrics.askVolumesByLevel.empty()) {
        std::cerr << "Invalid metrics: empty bid or ask volumes." << std::endl;
        return false;
    }

    // Helper: run a detector with a timeout to prevent it from blocking the caller thread
    auto runDetectorWithTimeout = [&](const std::string &name,
                                      const std::function<DetectorResult()> &fn,
                                      std::chrono::milliseconds timeout) -> DetectorResult {
        try {
            auto fut = getDetectorThreadPool().submit(fn);
            if (fut.wait_for(timeout) == std::future_status::ready) {
                return fut.get();
            } else {
                LOG_WARN("ImbalanceDetectionEngine: detector '{}' timed out after {} ms", name, timeout.count());
                return DetectorResult{};
            }
        } catch (const std::exception &ex) {
            LOG_WARN("ImbalanceDetectionEngine: detector '{}' threw: {}", name, ex.what());
            return DetectorResult{};
        } catch (...) {
            LOG_WARN("ImbalanceDetectionEngine: detector '{}' threw unknown exception", name);
            return DetectorResult{};
        }
    };

    // If detector worker is running, try to enqueue the metrics and return quickly.
    {
        std::lock_guard<std::mutex> qlk(detectorQueueMutex_);
        if (detectorWorkerRunning_.load()) {
            if (detectorQueue_.size() < detectorQueueMax_) {
                detectorQueue_.push_back(metrics);
                // Store latest per-symbol snapshot for accurate labeling later
                try {
                    std::lock_guard<std::mutex> lk(latestSnapshotsMutex_);
                    std::string key = metrics.exchangeId.toString() + "_" + metrics.symbol.toString();
                    latestSnapshots_[key] = metrics;
                } catch (...) {}
                detectorQueueCv_.notify_one();
                // LOG_TRACE("ImbalanceDetectionEngine: enqueued metrics for {}:{} (queue size={})", metrics.exchangeId, metrics.symbol, detectorQueue_.size());
                return false; // enqueued for async processing
            } else {
                // Rate-limit the warn to avoid log spam
                auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                long long prev = lastQueueFullWarnMs_.load();
                if (now_ms - prev > 2000) { // warn at most once every 2s
                    LOG_WARN("ImbalanceDetectionEngine: detector queue full ({}), falling back to synchronous run", detectorQueue_.size());
                    lastQueueFullWarnMs_.store(now_ms);
                }

                // Synchronous bounded fallback: run a set of detectors on the caller thread
                // using runDetectorWithTimeout to avoid unbounded blocking. This preserves
                // detection capability under overload while protecting caller latency.
                std::vector<DetectorResult> results;
                try {
                    // Per-detector timeout (ms)
                    std::chrono::milliseconds perDetectorTimeout(50);

                    results.push_back(runDetectorWithTimeout("large_order_appearance", [&]{ return detectLargeOrderAppearance(metrics); }, perDetectorTimeout));
                    results.push_back(runDetectorWithTimeout("large_order_disappearance", [&]{ return detectLargeOrderDisappearance(metrics); }, perDetectorTimeout));
                    results.push_back(runDetectorWithTimeout("order_flow_imbalance", [&]{ return detectOrderFlowImbalance(metrics); }, perDetectorTimeout));
                    results.push_back(runDetectorWithTimeout("price_level_concentration", [&]{ return detectPriceLevelConcentration(metrics); }, perDetectorTimeout));
                    results.push_back(runDetectorWithTimeout("liquidity_imbalance", [&]{ return detectLiquidityImbalance(metrics); }, perDetectorTimeout));
                    results.push_back(runDetectorWithTimeout("trend_continuation", [&]{ return detectTrendContinuation(metrics); }, perDetectorTimeout));
                    results.push_back(runDetectorWithTimeout("trend_reversal", [&]{ return detectTrendReversal(metrics); }, perDetectorTimeout));
                    results.push_back(runDetectorWithTimeout("trade_imbalance", [&]{ return detectTradeImbalance(metrics); }, perDetectorTimeout));
                } catch (const std::exception &ex) {
                    LOG_WARN("ImbalanceDetectionEngine: synchronous fallback detectors threw: {}", ex.what());
                } catch (...) {
                    LOG_WARN("ImbalanceDetectionEngine: synchronous fallback detectors threw unknown exception");
                }

                // Build features (same helper used by worker)
                // Store latest snapshot for synchronous path as well
                try {
                    std::lock_guard<std::mutex> lk(latestSnapshotsMutex_);
                    std::string key = metrics.exchangeId.toString() + "_" + metrics.symbol.toString();
                    latestSnapshots_[key] = metrics;
                } catch (...) {}
                try {
                    std::vector<double> features = buildFeatures(metrics, results);

                    // Buffer training sample (thread-safe)
                    PendingTraining pt;
                    pt.timestamp = metrics.timestamp;
                    pt.features = features;
                    pt.midPrice = metrics.midPrice;
                    try { pt.vwap = metrics.getMetric("vwap", 0.0); } catch(...) {}
                    try { pt.sma = metrics.getMetric("sma", 0.0); } catch(...) {}
                    auto ref = selectReference(metrics, pt.vwap, pt.sma);
                    pt.referenceValue = ref.first;
                    pt.referenceType = ref.second;
                    pt.exchangeId = metrics.exchangeId;
                    pt.symbol = metrics.symbol;

                    {
                        std::lock_guard<std::mutex> lk_pending(pendingTrainingMutex_);
                        if (pendingTraining_.size() >= maxPendingTraining_) pendingTraining_.pop_front();
                        pendingTraining_.push_back(pt);
                    }
                } catch (const std::exception &ex) {
                    LOG_WARN("ImbalanceDetectionEngine: error in synchronous fallback feature build: {}", ex.what());
                } catch (...) {
                    LOG_WARN("ImbalanceDetectionEngine: unknown error in synchronous fallback feature build");
                }
                // Return after fallback processing (caller continues)
                return false;
            }
        }
    }

    return false;
}

bool ImbalanceDetectionEngine::isUnderBackpressure() const {
    std::lock_guard<std::mutex> lk(detectorQueueMutex_);
    return detectorQueue_.size() >= detectorQueueMax_;
}

size_t ImbalanceDetectionEngine::getDetectorQueueSize() const {
    std::lock_guard<std::mutex> lk(detectorQueueMutex_);
    return detectorQueue_.size();
}

 
 // Get all detected imbalance events
 std::vector<ImbalanceEvent> ImbalanceDetectionEngine::getDetectedImbalances() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return detectedImbalances_;
 }
 
 // Check if a specific type of imbalance has been detected
 bool ImbalanceDetectionEngine::hasSignificantImbalance(ImbalanceType type) const {
     std::lock_guard<std::mutex> lock(mutex_);
     auto it = imbalanceState_.find(type);
     return (it != imbalanceState_.end()) ? it->second : false;
 }
 
 // Set detection thresholds
 void ImbalanceDetectionEngine::setThresholds(const ThresholdSet& thresholds) {
     std::lock_guard<std::mutex> lock(mutex_);
     thresholds_ = thresholds;
 }
 
 // Get current detection thresholds
 ThresholdSet ImbalanceDetectionEngine::getThresholds() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return thresholds_;
 }
 
 // Register callback for imbalance detection events
 size_t ImbalanceDetectionEngine::registerImbalanceCallback(const ImbalanceCallback& callback) {
     std::lock_guard<std::mutex> lock(mutex_);
     size_t id = nextCallbackId_++;
     callbacks_[id] = callback;
     return id;
 }
 
 // Unregister a previously registered callback
 bool ImbalanceDetectionEngine::unregisterImbalanceCallback(size_t callbackId) {
     std::lock_guard<std::mutex> lock(mutex_);
     auto it = callbacks_.find(callbackId);
     if (it != callbacks_.end()) {
         callbacks_.erase(it);
         return true;
     }
     return false;
 }
 
 // Clear all detected imbalances
 void ImbalanceDetectionEngine::clearImbalances() {
     detectedImbalances_.clear();
     
     // Reset imbalance state for all types
     for (auto& pair : imbalanceState_) {
         pair.second = false;
     }
 }
 
 // Get the engine status
 std::string ImbalanceDetectionEngine::getStatus() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Build a simple status string
     std::string status = "ImbalanceDetectionEngine Status:\n";
     status += "  Active imbalances: " + std::to_string(detectedImbalances_.size()) + "\n";
     
     // Add status for each imbalance type
     for (const auto& pair : imbalanceState_) {
         std::string typeName;
         switch (pair.first) {
             case ImbalanceType::VOLUME_IMBALANCE:
                 typeName = "VOLUME_IMBALANCE";
                 break;
             case ImbalanceType::PRICE_LEVEL_IMBALANCE:
                 typeName = "PRICE_LEVEL_IMBALANCE";
                 break;
             case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE:
                 typeName = "LIQUIDITY_FLOW_IMBALANCE";
                 break;
             case ImbalanceType::ABNORMAL_SPREAD:
                 typeName = "ABNORMAL_SPREAD";
                 break;
            case ImbalanceType::LIQUIDITY_IMBALANCE:
                 typeName = "LIQUIDITY_IMBALANCE";
                 break;
            case ImbalanceType::LIQUIDITY_REMOVAL:
                 typeName = "LIQUIDITY_REMOVAL";
                 break;
            case ImbalanceType::ORDER_FLOW_IMBALANCE:
                 typeName = "ORDER_FLOW_IMBALANCE";
                 break;
            case ImbalanceType::LARGE_ORDER_APPEARANCE:
                 typeName = "LARGE_ORDER_APPEARANCE";
                 break;
            case ImbalanceType::LARGE_ORDER_DISAPPEARANCE:
                 typeName = "LARGE_ORDER_DISAPPEARANCE";
                 break;
            case ImbalanceType::PRICE_LEVEL_CONCENTRATION:
                 typeName = "PRICE_LEVEL_CONCENTRATION";
                 break;
            case ImbalanceType::BID_ASK_SPREAD_CHANGE:
                 typeName = "BID_ASK_SPREAD_CHANGE";
                 break;
            case ImbalanceType::MARKET_DEPTH_CHANGE:
                 typeName = "MARKET_DEPTH_CHANGE";
                 break;
            case ImbalanceType::TREND_CONTINUATION:
                 typeName = "TREND_CONTINUATION";
                 break;
            case ImbalanceType::TREND_REVERSAL:
                 typeName = "TREND_REVERSAL";
                 break;
             case ImbalanceType::CUSTOM:
                 typeName = "CUSTOM";
                 break;
             default:
                 typeName = "UNKNOWN";
                 break;
         }
         status += "  " + typeName + ": " + (pair.second ? "ACTIVE" : "INACTIVE") + "\n";
     }
     
     return status;
 }
   
 void ImbalanceDetectionEngine::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Define default values for each threshold
    const double DEFAULT_VOLUME_IMBALANCE = 2.0;
    const double DEFAULT_PRICE_LEVEL_IMBALANCE = 1.5;
    const double DEFAULT_LIQUIDITY_FLOW_IMBALANCE = 3.0;
    const double DEFAULT_ABNORMAL_SPREAD = 2.5;
    
    // Update volume imbalance threshold
    if (params.numeric_params.find("volume_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("volume_imbalance_threshold", DEFAULT_VOLUME_IMBALANCE);
        thresholds_.setThreshold("volumeImbalance ", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::VOLUME_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::VOLUME_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update price level imbalance threshold
    if (params.numeric_params.find("price_level_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("price_level_imbalance_threshold", DEFAULT_PRICE_LEVEL_IMBALANCE);
        thresholds_.setThreshold("priceLevelImbalance", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::PRICE_LEVEL_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update liquidity flow imbalance threshold
    if (params.numeric_params.find("liquidity_flow_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("liquidity_flow_imbalance_threshold", DEFAULT_LIQUIDITY_FLOW_IMBALANCE);
        thresholds_.setThreshold("liquidity_flow_imbalance", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update abnormal spread threshold
    if (params.numeric_params.find("abnormal_spread_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("abnormal_spread_threshold", DEFAULT_ABNORMAL_SPREAD);
        thresholds_.setThreshold("abnormal_spread", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::ABNORMAL_SPREAD) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::ABNORMAL_SPREAD);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::ABNORMAL_SPREAD, imbalanceThreshold);
        }
    }
    // liquidity_removal
    if (params.numeric_params.find("liquidity_removal_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("liquidity_removal_threshold", 1.5);
        thresholds_.setThreshold("liquidity_removal", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::LIQUIDITY_REMOVAL) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::LIQUIDITY_REMOVAL);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::LIQUIDITY_REMOVAL, imbalanceThreshold);
        }
    }
    // /marketDepthChange
    if (params.numeric_params.find("market_depth_change_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("market_depth_change_threshold", 0.3);
        thresholds_.setThreshold("marketDepthChange", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::MARKET_DEPTH_CHANGE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::MARKET_DEPTH_CHANGE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::MARKET_DEPTH_CHANGE, imbalanceThreshold);
        }
    }
    // largeOrderThreshold
    if (params.numeric_params.find("large_order_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("large_order_threshold", 0.02);
        thresholds_.setThreshold("largeOrderThreshold", threshold);
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::LARGE_ORDER_APPEARANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::LARGE_ORDER_APPEARANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::LARGE_ORDER_APPEARANCE, imbalanceThreshold);
        }
        if (thresholds_.type_thresholds.find(ImbalanceType::LARGE_ORDER_DISAPPEARANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::LARGE_ORDER_DISAPPEARANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::LARGE_ORDER_DISAPPEARANCE, imbalanceThreshold);
        }
    }
    // orderFlowImbalance
    if (params.numeric_params.find("order_flow_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("order_flow_imbalance_threshold", 0.6);
        thresholds_.setThreshold("orderFlowImbalance", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::ORDER_FLOW_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::ORDER_FLOW_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::ORDER_FLOW_IMBALANCE, imbalanceThreshold);
        }
    }
    // priceLevelConcentration
    if (params.numeric_params.find("price_level_concentration_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("price_level_concentration_threshold", 0.5);
        thresholds_.setThreshold("priceLevelConcentration", threshold); 
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::PRICE_LEVEL_CONCENTRATION) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::PRICE_LEVEL_CONCENTRATION);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::PRICE_LEVEL_CONCENTRATION, imbalanceThreshold);
        }
    }
    // liquidityImbalance
    if (params.numeric_params.find("liquidity_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("liquidity_imbalance_threshold", 0.6);
        thresholds_.setThreshold("liquidityImbalance", threshold);
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::LIQUIDITY_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::LIQUIDITY_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::LIQUIDITY_IMBALANCE, imbalanceThreshold);
        }
    }
    // trendContinuation
    if (params.numeric_params.find("trend_continuation_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("trend_continuation_threshold", 0.4);
        thresholds_.setThreshold("trendContinuation", threshold);
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::TREND_CONTINUATION) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::TREND_CONTINUATION);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::TREND_CONTINUATION, imbalanceThreshold);
        }
    }
    // trendReversal
    if (params.numeric_params.find("trend_reversal_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("trend_reversal_threshold", 0.4);
        thresholds_.setThreshold("trendReversal", threshold);
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::TREND_REVERSAL) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::TREND_REVERSAL);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::TREND_REVERSAL, imbalanceThreshold);
        }
    }
    // tradeImbalance
    if (params.numeric_params.find("trade_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("trade_imbalance_threshold", 0.6);
        thresholds_.setThreshold("tradeImbalance", threshold);
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::TRADE_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::TRADE_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::TRADE_IMBALANCE, imbalanceThreshold);
        }
    }
    // cancellationImbalance
    if (params.numeric_params.find("cancellation_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("cancellation_imbalance_threshold", 0.6);
        thresholds_.setThreshold("cancellationImbalance", threshold);
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::CANCELLATION_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::CANCELLATION_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::CANCELLATION_IMBALANCE, imbalanceThreshold);
        }
    }
    // priceVolatilityImbalance
    if (params.numeric_params.find("price_volatility_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("price_volatility_imbalance_threshold", 0.3);
        thresholds_.setThreshold("priceVolatilityImbalance", threshold);
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::PRICE_VOLATILITY_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::PRICE_VOLATILITY_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::PRICE_VOLATILITY_IMBALANCE, imbalanceThreshold);
        }
    }
    
    
    // Update global sensitivity if provided
    if (params.numeric_params.find("global_sensitivity") != params.numeric_params.end()) {
        thresholds_.global_sensitivity = params.getDouble("global_sensitivity", 1.0);
    }
}
 // PRIVATE METHODS

double ImbalanceDetectionEngine::calculateImbalanceStrength(double ratio, double threshold) {
    double strength;
    if (ratio > threshold) {
        strength = (ratio - threshold) / threshold;
    } else if (ratio < 1.0 / threshold) {
        strength = (1.0/threshold - ratio) / (1.0/threshold);
    } else {
        strength = 0.0;
    }
    
    strength = std::min(1.0, std::max(0.0, strength)); // Clamp [0,1]
    return strength;
}


double ImbalanceDetectionEngine::calculateImbalanceConfidence(ImbalanceType type, double strength, const OrderBookMetrics& metrics) {
    // Strength is already normalized (0-1), so use it directly
    double baseConfidence = std::clamp(strength, 0.0, 1.0);
    
    // Adjust confidence based on market conditions
    double liquidityFactor = (metrics.totalBidVolume  + metrics.totalAskVolume) / 2.0;
    double volatilityFactor = metrics.volatilityExpectation;
    
    // Simple heuristic adjustments
    double liquidityWeight = 0.1; // Reduced impact
    double volatilityWeight = 0.1; // Reduced impact
    
    double liquidityAdjustment = (liquidityFactor > 1000.0) ? liquidityWeight : (liquidityFactor / 1000.0) * liquidityWeight;
    double volatilityAdjustment = (volatilityFactor < 0.5) ? volatilityWeight : (1.0 - volatilityFactor) * volatilityWeight;
    
    double adjustedConfidence = baseConfidence + liquidityAdjustment - volatilityAdjustment;
    adjustedConfidence = std::clamp(adjustedConfidence, 0.0, 1.0);
    
    return std::clamp(adjustedConfidence, 0.0, 1.0);
}

// DetectorResult is a Bayesian friendly struct
DetectorResult ImbalanceDetectionEngine::detectVolumeImbalance(const OrderBookMetrics& metrics) {
    DetectorResult r; 
    r.type = ImbalanceType::VOLUME_IMBALANCE;

    try {
        // --- 1. Lấy dữ liệu ---
        auto bidVolIt = metrics.metrics.find("bidVolume");
        auto askVolIt = metrics.metrics.find("askVolume");
        if (bidVolIt == metrics.metrics.end() || askVolIt == metrics.metrics.end()) {
            return r; // Không có dữ liệu
        }

        double bidVolume = bidVolIt->second;
        double askVolume = std::max(askVolIt->second, 1e-10); // tránh chia 0
        double rawRatio = bidVolume / askVolume;

        // --- 2. EMA smoothing (nếu có) ---
        double volumeRatio = rawRatio;
        auto emaIt = metrics.metrics.find("emaVolumeImbalance");
        const double EMA_BLEND_ALPHA = 0.7;
        if (emaIt != metrics.metrics.end()) {
            double emaVal = emaIt->second;
            if (std::isfinite(emaVal) && emaVal > 0.0) {
                volumeRatio = EMA_BLEND_ALPHA * emaVal + (1.0 - EMA_BLEND_ALPHA) * rawRatio;
            }
        }

        // --- 3. Lấy threshold ---
        double baseThreshold = 1.0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            baseThreshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE)
                                        .activation_threshold;
        }

        // --- 4. Kiểm tra thay đổi để tránh spam ---
        std::pair<core::ExchangeId, core::Symbol> key{metrics.exchangeId, metrics.symbol};
        double lastVolumeRatio = volumeRatio;
        {
            std::lock_guard<std::mutex> lock(lastVolumeMutex_);
            auto it = lastVolumeRatio_.find(key);
            if (it != lastVolumeRatio_.end()) lastVolumeRatio = it->second;
            lastVolumeRatio_[key] = volumeRatio; // update snapshot
        }

        double ratioChange = 0.0;
        if (lastVolumeRatio > 0.0 && std::isfinite(lastVolumeRatio) &&
            volumeRatio > 0.0 && std::isfinite(volumeRatio)) {
            ratioChange = std::abs(std::log(volumeRatio) - std::log(lastVolumeRatio));
        }
        if (ratioChange < 0.01) { // thay đổi quá nhỏ → coi như không event
            return r;
        }

        // --- 5. Tính strength liên tục ---
        double strength = calculateImbalanceStrength(volumeRatio, baseThreshold);
        Direction direction = (volumeRatio > 1.0) ? Direction::BUY : Direction::SELL;

        // --- 6. Xuất kết quả ---
        if (strength > 0.0) {
            r.imbalanceValue = volumeRatio;
            r.strength = strength;
            r.direction = direction;
            r.shouldEmitEvent = (strength > 0.2);
        }

    } catch (const std::exception& ex) {
        std::cerr << "Exception in detectVolumeImbalance: " << ex.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in detectVolumeImbalance." << std::endl;
    }

    return r;
}

DetectorResult ImbalanceDetectionEngine::detectPriceLevelImbalance(const OrderBookMetrics& metrics) {
    DetectorResult r;
    r.type = ImbalanceType::PRICE_LEVEL_IMBALANCE;

    try {
        // --- 1. Lấy dữ liệu ---
        auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
        auto askLevelsIt = metrics.metrics.find("askLevelCount");
        if (bidLevelsIt == metrics.metrics.end() || askLevelsIt == metrics.metrics.end()) {
            return r; // Không có dữ liệu
        }

        double bidLevels = bidLevelsIt->second;
        double askLevels = std::max(askLevelsIt->second, 1e-10); // tránh chia 0
        double levelRatio = bidLevels / askLevels;

        // --- 2. Lấy threshold ---
        double threshold = 1.0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            threshold = thresholds_.getTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE)
                                         .activation_threshold;
        }

        // --- 3. Tính strength liên tục ---
        double strength = calculateImbalanceStrength(levelRatio, threshold);
        Direction direction = (levelRatio > 1.0) ? Direction::BUY : Direction::SELL;

        // --- 4. Xuất kết quả ---
        if (strength > 0.0) {
            r.imbalanceValue = levelRatio;
            r.strength = strength;
            r.direction = direction;
            r.shouldEmitEvent = (strength > 0.2); // ngưỡng event cơ bản
        }

    } catch (const std::exception& ex) {
        std::cerr << "Exception in detectPriceLevelImbalance: " << ex.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in detectPriceLevelImbalance." << std::endl;
    }

    return r;
}


DetectorResult ImbalanceDetectionEngine::detectLiquidityFlowImbalance(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::LIQUIDITY_FLOW_IMBALANCE;
    
    try {
        auto bidFlowIt = metrics.metrics.find("bidFlow");
        auto askFlowIt = metrics.metrics.find("askFlow");
        
        if (bidFlowIt == metrics.metrics.end() || askFlowIt == metrics.metrics.end()) {
            return result;
        }
        
        double bidFlow = bidFlowIt->second;
        double askFlow = askFlowIt->second;
        double totalFlow = std::abs(bidFlow) + std::abs(askFlow);
        
        if (totalFlow < 1e-10) {
            return result;
        }
        
        // Safe division - avoid division by zero
        double flowRatio = 1.0;
        if (bidFlow > askFlow) {
            if (std::abs(askFlow) > 1e-10) {
                flowRatio = bidFlow / askFlow;
            } else {
                flowRatio = bidFlow > 0 ? 1000.0 : 1.0; // Cap extreme ratios
            }
        } else {
            if (std::abs(bidFlow) > 1e-10) {
                flowRatio = askFlow / bidFlow;
            } else {
                flowRatio = askFlow > 0 ? 1000.0 : 1.0; // Cap extreme ratios
            }
        }
        
        // Clamp ratio to reasonable bounds
        flowRatio = std::min(1000.0, std::max(1.0, flowRatio));
        result.imbalanceValue = flowRatio;
        
        // Get threshold safely
        double threshold = 1.5; // Default fallback
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            threshold = thresholds_.getTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE).activation_threshold;
        } catch (...) {
            // Use default if threshold access fails
        }
        
        // Use existing calculateImbalanceStrength function
        result.strength = calculateImbalanceStrength(flowRatio, threshold);
        result.direction = (bidFlow > askFlow) ? Direction::BUY : Direction::SELL;
        result.shouldEmitEvent = (result.strength > 0.2);
        
    } catch (const std::exception& ex) {
        std::cerr << "Exception in detectLiquidityFlowImbalance: " << ex.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in detectLiquidityFlowImbalance." << std::endl;
    }
    
    return result;
}

 DetectorResult ImbalanceDetectionEngine::detectAbnormalSpread(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::ABNORMAL_SPREAD;
    
    auto spreadIt = metrics.metrics.find("spreadPercent");
    auto avgSpreadIt = metrics.metrics.find("averageSpreadPercent");
    
    if (spreadIt == metrics.metrics.end() || avgSpreadIt == metrics.metrics.end()) {
        return result;
    }
    
    double currentSpread = spreadIt->second;
    double averageSpread = std::max(avgSpreadIt->second, 1e-10);
    double spreadRatio = currentSpread / averageSpread;
    
    result.imbalanceValue = spreadRatio;
    
    // Get threshold
    double threshold;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threshold = thresholds_.getTypeThreshold(ImbalanceType::ABNORMAL_SPREAD).activation_threshold;
    }
    
    result.strength = calculateImbalanceStrength(spreadRatio, threshold);
    
    // Use original direction logic from your code
    auto bidVolumeIt = metrics.metrics.find("bidVolume");
    auto askVolumeIt = metrics.metrics.find("askVolume");
    
    if (bidVolumeIt != metrics.metrics.end() && askVolumeIt != metrics.metrics.end()) {
        double bidVolume = bidVolumeIt->second;
        double askVolume = askVolumeIt->second;
        
        if (bidVolume > askVolume*1.2) {
            result.direction = Direction::BUY;
        } else {
            result.direction = Direction::SELL;
        }
    } else {
        result.direction = Direction::NEUTRAL;
    }
    
    result.shouldEmitEvent = (result.strength > 0.2);
    
    return result;
}

DetectorResult ImbalanceDetectionEngine::detectMarketDepthChange(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::MARKET_DEPTH_CHANGE;
    
    // Copy previous snapshot under lock
    std::optional<OrderBookMetrics> prevOpt;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prevOpt = lastMetrics_;
    }
    
    if (!prevOpt) {
        return result;
    }
    
    const auto& prev = *prevOpt;
    
    // Calculate normalized change metrics
    double spreadChange = std::abs(metrics.spread - prev.spread) / std::max(prev.spread, 1e-10);
    double imbalanceChange = std::abs(metrics.volumeImbalance - prev.volumeImbalance);
    double pressureChange = std::abs(metrics.liquidityPressure - prev.liquidityPressure);
    double densityChange = std::abs(metrics.bidDensity - prev.bidDensity) + std::abs(metrics.askDensity - prev.askDensity);
    
    // Check level volume changes
    auto checkVectorChange = [&](const std::vector<double>& cur, const std::vector<double>& old) -> double {
        size_t n = std::min(cur.size(), old.size());
        double maxChange = 0.0;
        for (size_t i = 0; i < n; i++) {
            double change = std::abs(cur[i] - old[i]) / std::max(old[i], 1e-10);
            maxChange = std::max(maxChange, change);
        }
        return maxChange;
    };
    
    double bidLevelChange = checkVectorChange(metrics.bidVolumesByLevel, prev.bidVolumesByLevel);
    double askLevelChange = checkVectorChange(metrics.askVolumesByLevel, prev.askVolumesByLevel);
    double maxLevelChange = std::max(bidLevelChange, askLevelChange);
    
    // Combine all changes into overall depth change ratio
    double depthChangeRatio = std::max({spreadChange, imbalanceChange, pressureChange, densityChange, maxLevelChange});
    
    result.imbalanceValue = depthChangeRatio;
    
    // Get threshold
    double threshold;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threshold = thresholds_.getTypeThreshold(ImbalanceType::MARKET_DEPTH_CHANGE).activation_threshold;
    }
    
    result.strength = calculateImbalanceStrength(depthChangeRatio, threshold);
    result.direction = Direction::NEUTRAL;
    result.shouldEmitEvent = (result.strength > 0.2);
    
    return result;
}


DetectorResult ImbalanceDetectionEngine::detectLargeOrderAppearance(const OrderBookMetrics& metrics) {
    // Configuration thresholds - should be configurable parameters
    static const double VOLUME_IMBALANCE_THRESHOLD = 2.0;        // 100% imbalance for strength=1
    static const double WEIGHTED_IMBALANCE_THRESHOLD = 2.0;      // 100% weighted imbalance 
    static const double DEPTH_IMBALANCE_THRESHOLD = 2.5;         // 150% depth imbalance
    static const double PRESSURE_IMBALANCE_THRESHOLD = 2.3;      // 130% pressure imbalance
    static const double BID_ASK_RATIO_THRESHOLD = 2.0;           // 2:1 ratio threshold
    static const double DENSITY_RATIO_THRESHOLD = 2.5;          // 150% density difference
    static const double MARKET_DIRECTIONAL_THRESHOLD = 1.67;     // ~67% directional signal
    static const double LIQUIDITY_PRESSURE_THRESHOLD = 2.5;     // 150% liquidity pressure
    static const double FLOW_IMBALANCE_THRESHOLD = 2.0;         // 100% flow imbalance
    static const double EMISSION_THRESHOLD = 0.2;               // Configurable emission threshold
    
    DetectorResult result;
    result.type = ImbalanceType::LARGE_ORDER_APPEARANCE;
    
    // Calculate individual strength components
    std::vector<double> strengths;
    std::vector<double> ratios;
    
    // 1. Volume imbalance strength
    double volumeRatio = std::abs(metrics.volumeImbalance) + 1.0;
    strengths.push_back(calculateImbalanceStrength(volumeRatio, VOLUME_IMBALANCE_THRESHOLD));
    ratios.push_back(volumeRatio);
    
    // 2. Weighted volume imbalance strength  
    double weightedRatio = std::abs(metrics.weightedVolumeImbalance) + 1.0;
    strengths.push_back(calculateImbalanceStrength(weightedRatio, WEIGHTED_IMBALANCE_THRESHOLD));
    ratios.push_back(weightedRatio);
    
    // 3. Depth imbalance strength
    double depthRatio = std::abs(metrics.depthImbalance) + 1.0;
    strengths.push_back(calculateImbalanceStrength(depthRatio, DEPTH_IMBALANCE_THRESHOLD));
    ratios.push_back(depthRatio);
    
    // 4. Pressure imbalance strength
    double pressureRatio = std::abs(metrics.pressureImbalance) + 1.0;
    strengths.push_back(calculateImbalanceStrength(pressureRatio, PRESSURE_IMBALANCE_THRESHOLD));
    ratios.push_back(pressureRatio);
    
    // 5. Bid-ask volume ratio strength - safe division
    double bidAskRatio = 1.0;
    if (metrics.bidAskVolumeRatio > 1e-10) {
        bidAskRatio = std::max(metrics.bidAskVolumeRatio, 1.0 / metrics.bidAskVolumeRatio);
    }
    strengths.push_back(calculateImbalanceStrength(bidAskRatio, BID_ASK_RATIO_THRESHOLD));
    ratios.push_back(bidAskRatio);
    
    // 6. Density imbalance strength
    if (metrics.bidDensity > 0 && metrics.askDensity > 0) {
        double densityRatio = std::max(metrics.bidDensity / metrics.askDensity,
                                     metrics.askDensity / metrics.bidDensity);
        strengths.push_back(calculateImbalanceStrength(densityRatio, DENSITY_RATIO_THRESHOLD));
        ratios.push_back(densityRatio);
    }
    
    // 7. Market directional coefficient strength
    double directionalRatio = std::abs(metrics.marketDirectionalCoefficient) + 1.0;
    strengths.push_back(calculateImbalanceStrength(directionalRatio, MARKET_DIRECTIONAL_THRESHOLD));
    ratios.push_back(directionalRatio);
    
    // 8. Liquidity pressure strength
    double liquidityRatio = std::abs(metrics.liquidityPressure) + 1.0;
    strengths.push_back(calculateImbalanceStrength(liquidityRatio, LIQUIDITY_PRESSURE_THRESHOLD));
    ratios.push_back(liquidityRatio);
    
    // 9. Volume concentration strength
    double concentrationStrength = getVolumeConcentrationStrength(metrics);
    strengths.push_back(concentrationStrength);
    ratios.push_back(concentrationStrength + 1.0);
    
    // 10. Flow imbalance strength
    double bidFlow = metrics.getMetric("bidFlow", 0.0);
    double askFlow = metrics.getMetric("askFlow", 0.0);
    double flowRatio = std::abs(bidFlow - askFlow) + 1.0;
    strengths.push_back(calculateImbalanceStrength(flowRatio, FLOW_IMBALANCE_THRESHOLD));
    ratios.push_back(flowRatio);
    
    // Calculate composite strength using weighted average of top signals
    std::sort(strengths.begin(), strengths.end(), std::greater<double>());
    
    // Use top 60% of signals with decreasing weights
    double compositeStrength = 0.0;
    double totalWeight = 0.0;
    size_t topSignals = std::max(1UL, static_cast<size_t>(strengths.size() * 0.6));
    
    for (size_t i = 0; i < topSignals; ++i) {
        double weight = 1.0 / (i + 1.0);  // Decreasing weight: 1.0, 0.5, 0.33, ...
        compositeStrength += strengths[i] * weight;
        totalWeight += weight;
    }
    
    compositeStrength /= totalWeight;
    
    // Apply smoothing if enabled (thread-safe)
    static thread_local double smoothedStrength = 0.0;
    static const double SMOOTHING_ALPHA = 0.3;
    smoothedStrength = SMOOTHING_ALPHA * compositeStrength + (1.0 - SMOOTHING_ALPHA) * smoothedStrength;
    
    // Set result values
    result.strength = std::min(1.0, std::max(0.0, smoothedStrength));
    
    // Calculate composite ratio for imbalanceValue
    double maxRatio = *std::max_element(ratios.begin(), ratios.end());
    result.imbalanceValue = maxRatio;
    
    // Determine direction based on strongest signal's bias
    result.direction = determineDirection(metrics);
    
    // Emission condition
    result.shouldEmitEvent = (result.strength > EMISSION_THRESHOLD);
    
    return result;
}

// Helper function for volume concentration strength
double ImbalanceDetectionEngine::getVolumeConcentrationStrength(const OrderBookMetrics& metrics) {
    // Convert binary concentration check to continuous strength
    // This assumes checkVolumeConcentration logic can be adapted
    
    double concentrationRatio = 1.0;
    
    // Example implementation - adapt based on your checkVolumeConcentration logic
    if (!metrics.bidVolumesByLevel.empty() && !metrics.askVolumesByLevel.empty()) {
        // Calculate concentration in top levels vs total volume
        double topLevelVolume = 0.0;
        double totalVolume = 0.0;

        // Sum top 3 levels for bids
        size_t bidLevelsToCheck = std::min(static_cast<size_t>(3), metrics.bidVolumesByLevel.size());
        for (size_t i = 0; i < bidLevelsToCheck; ++i) {
            topLevelVolume += metrics.bidVolumesByLevel[i];
        }
        // Sum top 3 levels for asks
        size_t askLevelsToCheck = std::min(static_cast<size_t>(3), metrics.askVolumesByLevel.size());
        for (size_t i = 0; i < askLevelsToCheck; ++i) {
            topLevelVolume += metrics.askVolumesByLevel[i];
        }

        // Sum all bid levels
        for (const auto& volume : metrics.bidVolumesByLevel) {
            totalVolume += volume;
        }
        // Sum all ask levels
        for (const auto& volume : metrics.askVolumesByLevel) {
            totalVolume += volume;
        }

        if (totalVolume > 0) {
            double concentrationPercent = topLevelVolume / totalVolume;
            // Convert to ratio where higher concentration = higher ratio
            concentrationRatio = 1.0 + (concentrationPercent - 0.3) * 3.33; // Map [0.3,1.0] -> [1.0,3.33]
            concentrationRatio = std::max(1.0, concentrationRatio);
        }
    }
    
    return calculateImbalanceStrength(concentrationRatio, 2.0); // 100% concentration threshold
}
// Helper function to determine direction
Direction ImbalanceDetectionEngine::determineDirection(const OrderBookMetrics& metrics) {
    // EMERGENCY FIX: Force SELL when no USDT balance (BUY blocked)
    // Check if we have insufficient USDT balance based on recent logs
    static std::chrono::steady_clock::time_point lastEmergencySell = std::chrono::steady_clock::now() - std::chrono::minutes(5);
    auto now = std::chrono::steady_clock::now();
    
    // Force SELL every 30 seconds when we detect balance issues
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastEmergencySell).count() > 30) {
        // Check if we're in a position where we need to sell to get USDT
        // When we have coins but no USDT, force SELL to rebalance
        std::cout << "[EMERGENCY] Force SELL to rebalance USDT - Portfolio has coins but insufficient USDT" << std::endl;
        lastEmergencySell = now;
        return Direction::SELL;
    }
    
    // Aggregate directional signals
    double buySignal = 0.0;
    double sellSignal = 0.0;
    
    // Volume imbalance direction
    if (metrics.volumeImbalance > 0) buySignal += std::abs(metrics.volumeImbalance);
    else sellSignal += std::abs(metrics.volumeImbalance);
    
    // Weighted imbalance direction  
    if (metrics.weightedVolumeImbalance > 0) buySignal += std::abs(metrics.weightedVolumeImbalance);
    else sellSignal += std::abs(metrics.weightedVolumeImbalance);
    
    // Bid-ask ratio direction - safe division
    if (metrics.bidAskVolumeRatio > 1.0) {
        buySignal += (metrics.bidAskVolumeRatio - 1.0);
    } else if (metrics.bidAskVolumeRatio > 1e-10) {
        sellSignal += (1.0 / metrics.bidAskVolumeRatio - 1.0);
    }
    
    // Market directional coefficient
    if (metrics.marketDirectionalCoefficient > 0) buySignal += std::abs(metrics.marketDirectionalCoefficient);
    else sellSignal += std::abs(metrics.marketDirectionalCoefficient);
    
    
    return (buySignal > sellSignal) ? Direction::BUY : Direction::SELL;
}

// Helper function to check volume concentration at specific levels
// Note: Add this declaration to your ImbalanceDetectionEngine class header:
// bool checkVolumeConcentration(const OrderBookMetrics& metrics);

bool ImbalanceDetectionEngine::checkVolumeConcentration(const OrderBookMetrics& metrics) {
    const double CONCENTRATION_THRESHOLD = 0.6;  // 60% of volume in top levels
    
    // Check bid side concentration
    if (!metrics.bidVolumesByLevel.empty() && metrics.totalBidVolume > 0) {
        // Check if top 2 levels contain majority of volume
        double topBidVolume = 0.0;
        int levelsToCheck = std::min(2, static_cast<int>(metrics.bidVolumesByLevel.size()));
        
        for (int i = 0; i < levelsToCheck; ++i) {
            topBidVolume += metrics.bidVolumesByLevel[i];
        }
        
        if (topBidVolume / metrics.totalBidVolume > CONCENTRATION_THRESHOLD) {
            return true;
        }
    }
    
    // Check ask side concentration
    if (!metrics.askVolumesByLevel.empty() && metrics.totalAskVolume > 0) {
        double topAskVolume = 0.0;
        int levelsToCheck = std::min(2, static_cast<int>(metrics.askVolumesByLevel.size()));
        
        for (int i = 0; i < levelsToCheck; ++i) {
            topAskVolume += metrics.askVolumesByLevel[i];
        }
        
        if (topAskVolume / metrics.totalAskVolume > CONCENTRATION_THRESHOLD) {
            return true;
        }
    }
    
    return false;
}
DetectorResult ImbalanceDetectionEngine::detectLargeOrderDisappearance(const OrderBookMetrics& metrics) {
    // Configuration thresholds for disappearance detection
    static const double VOLUME_REDUCTION_THRESHOLD = 2.0;       // 100% volume reduction
    static const double DEPTH_REDUCTION_THRESHOLD = 2.5;       // 150% depth reduction  
    static const double SPREAD_EXPANSION_THRESHOLD = 2.0;      // 100% spread expansion
    static const double LIQUIDITY_DROP_THRESHOLD = 2.5;        // 150% liquidity drop
    static const double PRICE_REVERSION_THRESHOLD = 1.5;       // 50% price reversion
    static const double ORDER_COUNT_DROP_THRESHOLD = 2.0;      // 100% order count drop
    static const double MARKET_IMPACT_DECAY_THRESHOLD = 1.8;   // 80% impact decay
    static const double VOLATILITY_DECLINE_THRESHOLD = 2.0;    // 100% volatility decline
    static const double EMISSION_THRESHOLD = 0.2;              // Configurable emission threshold

    DetectorResult result;
    result.type = ImbalanceType::LARGE_ORDER_DISAPPEARANCE;
    
    // Calculate individual strength components for disappearance signals
    std::vector<double> strengths;
    std::vector<double> ratios;
    
    // 1. Volume reduction strength (inverse of volume increase)
    // Higher reduction = higher ratio
    double volumeReductionRatio = calculateVolumeReductionRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(volumeReductionRatio, VOLUME_REDUCTION_THRESHOLD));
    ratios.push_back(volumeReductionRatio);
    
    // 2. Depth reduction strength
    double depthReductionRatio = calculateDepthReductionRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(depthReductionRatio, DEPTH_REDUCTION_THRESHOLD));
    ratios.push_back(depthReductionRatio);
    
    // 3. Spread expansion strength (large orders removed -> spread widens)
    double spreadExpansionRatio = calculateSpreadExpansionRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(spreadExpansionRatio, SPREAD_EXPANSION_THRESHOLD));
    ratios.push_back(spreadExpansionRatio);
    
    // 4. Liquidity drop strength
    double liquidityDropRatio = calculateLiquidityDropRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(liquidityDropRatio, LIQUIDITY_DROP_THRESHOLD));
    ratios.push_back(liquidityDropRatio);
    
    // 5. Price reversion strength (price moves back after large order removal)
    double priceReversionRatio = calculatePriceReversionRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(priceReversionRatio, PRICE_REVERSION_THRESHOLD));
    ratios.push_back(priceReversionRatio);
    
    // 6. Order count reduction strength
    double orderCountDropRatio = calculateOrderCountDropRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(orderCountDropRatio, ORDER_COUNT_DROP_THRESHOLD));
    ratios.push_back(orderCountDropRatio);
    
    // 7. Market impact decay strength
    double impactDecayRatio = calculateMarketImpactDecayRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(impactDecayRatio, MARKET_IMPACT_DECAY_THRESHOLD));
    ratios.push_back(impactDecayRatio);
    
    // 8. Volatility decline strength
    double volatilityDeclineRatio = calculateVolatilityDeclineRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(volatilityDeclineRatio, VOLATILITY_DECLINE_THRESHOLD));
    ratios.push_back(volatilityDeclineRatio);
    
    // 9. Flow normalization strength (flow returns to normal after large order removal)
    double flowNormalizationRatio = calculateFlowNormalizationRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(flowNormalizationRatio, 1.8));
    ratios.push_back(flowNormalizationRatio);
    
    // 10. Pressure relief strength (pressure indicators normalize)
    double pressureReliefRatio = calculatePressureReliefRatio(metrics);
    strengths.push_back(calculateImbalanceStrength(pressureReliefRatio, 2.0));
    ratios.push_back(pressureReliefRatio);
    
    // Calculate composite strength using weighted average of top signals
    std::sort(strengths.begin(), strengths.end(), std::greater<double>());
    
    // Use top 60% of signals with decreasing weights
    double compositeStrength = 0.0;
    double totalWeight = 0.0;
    size_t topSignals = std::max(1UL, static_cast<size_t>(strengths.size() * 0.6));
    
    for (size_t i = 0; i < topSignals; ++i) {
        double weight = 1.0 / (i + 1.0);  // Decreasing weight
        compositeStrength += strengths[i] * weight;
        totalWeight += weight;
    }
    
    compositeStrength /= totalWeight;
    
    // Apply smoothing (thread-safe)
    static thread_local double smoothedStrength = 0.0;
    static const double SMOOTHING_ALPHA = 0.3;
    smoothedStrength = SMOOTHING_ALPHA * compositeStrength + (1.0 - SMOOTHING_ALPHA) * smoothedStrength;
    
    // Set result values
    result.strength = std::min(1.0, std::max(0.0, smoothedStrength));
    
    // Calculate composite ratio for imbalanceValue
    double maxRatio = *std::max_element(ratios.begin(), ratios.end());
    result.imbalanceValue = maxRatio;
    
    // Determine direction (opposite of disappearing order)
    result.direction = determineDisappearanceDirection(metrics);
    
    // Emission condition
    result.shouldEmitEvent = (result.strength > EMISSION_THRESHOLD);
    
    return result;
}

// Helper functions for disappearance detection
double ImbalanceDetectionEngine::calculateVolumeReductionRatio(const OrderBookMetrics& metrics) {
    // Compare current volume concentration vs historical average
    static thread_local double prevVolumeConcentration = 1.0;
    
    double currentConcentration = getCurrentVolumeConcentration(metrics);
    double reductionRatio = 1.0;
    
    if (currentConcentration > 0 && prevVolumeConcentration > 0) {
        if (prevVolumeConcentration > currentConcentration) {
            reductionRatio = prevVolumeConcentration / currentConcentration;
        }
    }
    
    prevVolumeConcentration = currentConcentration;
    return std::min(5.0, reductionRatio); // Cap at 5x reduction
}

double ImbalanceDetectionEngine::calculateDepthReductionRatio(const OrderBookMetrics& metrics) {
    static thread_local double prevTotalDepth = 1.0;
    
    // Use safe metric accessor in case OrderBookMetrics does not expose bidDepth/askDepth members
    double currentDepth = metrics.getMetric("bidDepth", 0.0) + metrics.getMetric("askDepth", 0.0);
    double reductionRatio = 1.0;
    
    if (currentDepth > 0 && prevTotalDepth > 0) {
        if (prevTotalDepth > currentDepth) {
            reductionRatio = prevTotalDepth / currentDepth;
        }
    }
    
    prevTotalDepth = currentDepth;
    return std::min(5.0, reductionRatio);
}

double ImbalanceDetectionEngine::calculateSpreadExpansionRatio(const OrderBookMetrics& metrics) {
    static thread_local double prevSpread = 0.001; // Avoid division by zero
    
    double currentSpread = metrics.spread;
    double expansionRatio = 1.0;
    
    if (currentSpread > 0 && prevSpread > 0) {
        if (currentSpread > prevSpread) {
            expansionRatio = currentSpread / prevSpread;
        }
    }
    
    prevSpread = currentSpread;
    return std::min(10.0, expansionRatio);
}

double ImbalanceDetectionEngine::calculateLiquidityDropRatio(const OrderBookMetrics& metrics) {
    static thread_local double prevLiquidity = 1.0;
    
    // Use inverse of liquidity pressure as liquidity measure
    double currentLiquidity = 1.0 / (std::abs(metrics.liquidityPressure) + 0.01);
    double dropRatio = 1.0;
    
    if (prevLiquidity > currentLiquidity && currentLiquidity > 0) {
        dropRatio = prevLiquidity / currentLiquidity;
    }
    
    prevLiquidity = currentLiquidity;
    return std::min(5.0, dropRatio);
}

double ImbalanceDetectionEngine::calculatePriceReversionRatio(const OrderBookMetrics& metrics) {
    // Measure how much price has reverted from recent extreme
    double midPrice = metrics.midPrice;
    static thread_local double recentExtreme = midPrice;
    static thread_local double basePrice = midPrice;
    static thread_local int extremeAge = 0;
    
    // Update extreme tracking
    if (std::abs(midPrice - basePrice) > std::abs(recentExtreme - basePrice)) {
        recentExtreme = midPrice;
        extremeAge = 0;
    } else {
        extremeAge++;
    }
    
    // Calculate reversion ratio
    double reversionRatio = 1.0;
    if (extremeAge > 5 && std::abs(recentExtreme - basePrice) > 0) {
        double totalMove = std::abs(recentExtreme - basePrice);
        double currentMove = std::abs(midPrice - basePrice);
        if (currentMove < totalMove) {
            reversionRatio = 1.0 + (totalMove - currentMove) / totalMove;
        }
    }
    
    // Update base price periodically
    if (extremeAge > 20) {
        basePrice = midPrice;
        extremeAge = 0;
    }
    
    return std::min(3.0, reversionRatio);
}

double ImbalanceDetectionEngine::calculateOrderCountDropRatio(const OrderBookMetrics& metrics) {
    static thread_local double prevOrderCount = 1.0;
    
    double currentOrderCount = metrics.bidVolumesByLevel.size() + metrics.askVolumesByLevel.size();
    double dropRatio = 1.0;
    
    if (currentOrderCount > 0 && prevOrderCount > 0) {
        if (prevOrderCount > currentOrderCount) {
            dropRatio = prevOrderCount / currentOrderCount;
        }
    }
    
    prevOrderCount = currentOrderCount;
    return std::min(3.0, dropRatio);
}

double ImbalanceDetectionEngine::calculateMarketImpactDecayRatio(const OrderBookMetrics& metrics) {
    // Measure decay in market directional coefficient
    static thread_local double prevImpact = 0.0;
    
    double currentImpact = std::abs(metrics.marketDirectionalCoefficient);
    double decayRatio = 1.0;
    
    if (prevImpact > 0.1 && currentImpact < prevImpact) {
        decayRatio = 1.0 + (prevImpact - currentImpact) / prevImpact;
    }
    
    prevImpact = currentImpact;
    return std::min(3.0, decayRatio);
}

double ImbalanceDetectionEngine::calculateVolatilityDeclineRatio(const OrderBookMetrics& metrics) {
    // Measure decline in price volatility
    static thread_local double prevVolatility = 0.01;
    
    double currentVolatility = metrics.getMetric("volatility", 0.01);
    double declineRatio = 1.0;
    
    if (currentVolatility > 0 && prevVolatility > 0) {
        if (prevVolatility > currentVolatility) {
            declineRatio = prevVolatility / currentVolatility;
        }
    }
    
    prevVolatility = currentVolatility;
    return std::min(5.0, declineRatio);
}

double ImbalanceDetectionEngine::calculateFlowNormalizationRatio(const OrderBookMetrics& metrics) {
    // Measure return to balanced flow
    double bidFlow = metrics.getMetric("bidFlow", 0.0);
    double askFlow = metrics.getMetric("askFlow", 0.0);
    static thread_local double prevFlowImbalance = 0.0;
    
    double currentFlowImbalance = std::abs(bidFlow - askFlow);
    double normalizationRatio = 1.0;
    
    if (prevFlowImbalance > 0.1 && currentFlowImbalance < prevFlowImbalance) {
        normalizationRatio = 1.0 + (prevFlowImbalance - currentFlowImbalance) / prevFlowImbalance;
    }
    
    prevFlowImbalance = currentFlowImbalance;
    return std::min(3.0, normalizationRatio);
}

double ImbalanceDetectionEngine::calculatePressureReliefRatio(const OrderBookMetrics& metrics) {
    // Measure relief in pressure indicators
    static thread_local double prevPressure = 0.0;
    
    double currentPressure = std::abs(metrics.pressureImbalance);
    double reliefRatio = 1.0;
    
    if (prevPressure > 0.1 && currentPressure < prevPressure) {
        reliefRatio = 1.0 + (prevPressure - currentPressure) / prevPressure;
    }
    
    prevPressure = currentPressure;
    return std::min(3.0, reliefRatio);
}

double ImbalanceDetectionEngine::getCurrentVolumeConcentration(const OrderBookMetrics& metrics) {
    // Calculate current volume concentration in top levels
    double topLevelVolume = 0.0;
    double totalVolume = 0.0;
    
    // Sum top 3 levels
    for (size_t i = 0; i < std::min(static_cast<size_t>(3), metrics.bidVolumesByLevel.size()); ++i) {
        topLevelVolume += metrics.bidVolumesByLevel[i];
    }
    for (size_t i = 0; i < std::min(static_cast<size_t>(3), metrics.askVolumesByLevel.size()); ++i) {
        topLevelVolume += metrics.askVolumesByLevel[i];
    }
    
    // Sum all levels
    for (const auto& volume : metrics.bidVolumesByLevel) {
        totalVolume += volume;
    }
    for (const auto& volume : metrics.askVolumesByLevel) {
        totalVolume += volume;
    }
    
    return (totalVolume > 0) ? (topLevelVolume / totalVolume) : 0.0;
}
Direction ImbalanceDetectionEngine::determineDisappearanceDirection(const OrderBookMetrics& metrics) {
    // Direction indicates where the disappeared order was likely located
    // If bid side shows more relief signals -> disappeared order was on bid side -> direction is SELL
    // If ask side shows more relief signals -> disappeared order was on ask side -> direction is BUY
    
    double bidSideRelief = 0.0;
    double askSideRelief = 0.0;
    
    // Analyze which side shows more signs of large order removal
    // Use safe metric accessors in case OrderBookMetrics does not expose bidDepth/askDepth members
    double bidDepth = metrics.getMetric("bidDepth", 0.0);
    double askDepth = metrics.getMetric("askDepth", 0.0);
    if (bidDepth < askDepth) bidSideRelief += 1.0;
    else askSideRelief += 1.0;
    
    if (metrics.bidAskVolumeRatio < 1.0) bidSideRelief += (1.0 - metrics.bidAskVolumeRatio);
    else askSideRelief += (metrics.bidAskVolumeRatio - 1.0);
    
    if (metrics.volumeImbalance > 0) askSideRelief += metrics.volumeImbalance;
    else bidSideRelief += std::abs(metrics.volumeImbalance);
    
    // Return opposite direction of where the order likely disappeared
    return (bidSideRelief > askSideRelief) ? Direction::SELL : Direction::BUY;
}

DetectorResult ImbalanceDetectionEngine::detectOrderFlowImbalance(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::ORDER_FLOW_IMBALANCE;
    
    // Calculate flow imbalance ratio
    double totalFlow = std::abs(metrics.bidFlow) + std::abs(metrics.askFlow);
    if (totalFlow < 1e-9) {
        return result; // No meaningful flow
    }
    
    double flowImbalanceRatio = (metrics.bidFlow - metrics.askFlow) / totalFlow;
    result.imbalanceValue = std::abs(flowImbalanceRatio);
    
    // Check against threshold
    double threshold = thresholds_.orderFlowImbalance;
    if (result.imbalanceValue > threshold) {
        result.strength = calculateImbalanceStrength(result.imbalanceValue, threshold);
        result.direction = flowImbalanceRatio > 0 ? Direction::BUY : Direction::SELL;
        result.shouldEmitEvent = true;
        
        // Enhanced strength calculation based on flow velocity
        if (lastMetrics_.has_value()) {
            double flowAcceleration = std::abs(totalFlow - (std::abs(lastMetrics_->bidFlow) + std::abs(lastMetrics_->askFlow)));
            result.strength *= (1.0 + flowAcceleration * 0.1); // Boost for accelerating flow
        }
    }
    
    
    return result;
}

DetectorResult ImbalanceDetectionEngine::detectPriceLevelConcentration(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::PRICE_LEVEL_CONCENTRATION;
    
    // Calculate concentration on both sides
    double bidConcentration = 0.0;
    double askConcentration = 0.0;
    
    // Calculate Herfindahl-Hirschman Index (HHI) for concentration
    if (!metrics.bidVolumesByLevel.empty()) {
        double totalBidVolume = metrics.totalBidVolume;
        if (totalBidVolume > 0) {
            for (double volume : metrics.bidVolumesByLevel) {
                double share = volume / totalBidVolume;
                bidConcentration += share * share;
            }
        }
    }
    
    if (!metrics.askVolumesByLevel.empty()) {
        double totalAskVolume = metrics.totalAskVolume;
        if (totalAskVolume > 0) {
            for (double volume : metrics.askVolumesByLevel) {
                double share = volume / totalAskVolume;
                askConcentration += share * share;
            }
        }
    }
    
    // Use maximum concentration as the imbalance value
    result.imbalanceValue = std::max(bidConcentration, askConcentration);
    
    double threshold = thresholds_.priceLevelConcentration;
    if (result.imbalanceValue > threshold) {
        result.strength = calculateImbalanceStrength(result.imbalanceValue, threshold);
        result.direction = bidConcentration > askConcentration ? Direction::BUY : Direction::SELL;
        result.shouldEmitEvent = true;
        
        // Additional check for top-level dominance
        if (!metrics.bidVolumesByLevel.empty() && !metrics.askVolumesByLevel.empty()) {
            double topBidShare = metrics.bidVolumesByLevel[0] / metrics.totalBidVolume;
            double topAskShare = metrics.askVolumesByLevel[0] / metrics.totalAskVolume;
            if (std::max(topBidShare, topAskShare) > 0.7) {
                result.strength *= 1.5; // Boost for extreme concentration
            }
        }
    }
    
    return result;
}

DetectorResult ImbalanceDetectionEngine::detectLiquidityImbalance(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::LIQUIDITY_IMBALANCE;
    
    // Calculate liquidity imbalance using multiple factors
    double volumeImbalance = std::abs(metrics.bidAskVolumeRatio - 1.0);
    double depthImbalance = std::abs(metrics.depthImbalance);
    double pressureImbalance = std::abs(metrics.pressureImbalance);
    
    // Composite liquidity imbalance score
    result.imbalanceValue = (volumeImbalance * 0.4 + depthImbalance * 0.3 + pressureImbalance * 0.3);
    
    double threshold = thresholds_.liquidityImbalance;
    if (result.imbalanceValue > threshold) {
        result.strength = calculateImbalanceStrength(result.imbalanceValue, threshold);
        
        // Determine direction based on which side has more liquidity
        if (metrics.totalBidVolume > metrics.totalAskVolume) {
            result.direction = Direction::SELL; // More bids suggests selling pressure coming
        } else {
            result.direction = Direction::BUY; // More asks suggests buying pressure coming
        }
        
        result.shouldEmitEvent = true;
        
        // Enhance strength based on liquidity pressure
        if (metrics.liquidityPressure > 0.5) {
            result.strength *= (1.0 + metrics.liquidityPressure * 0.3);
        }
    }
    
    return result;
}

DetectorResult ImbalanceDetectionEngine::detectTrendContinuation(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::TREND_CONTINUATION;
    
    if (!lastMetrics_.has_value()) {
        return result; // Need historical data for trend analysis
    }
    
    // Calculate price momentum
    double priceChange = metrics.midPrice - lastMetrics_->midPrice;
    double priceMomentum = priceChange / lastMetrics_->midPrice;
    
    // Calculate volume momentum
    double volumeChange = metrics.totalVolume - lastMetrics_->totalVolume;
    double volumeMomentum = volumeChange / std::max(lastMetrics_->totalVolume, 1.0);
    
    // Calculate order flow momentum
    double currentNetFlow = metrics.bidFlow - metrics.askFlow;
    double lastNetFlow = lastMetrics_->bidFlow - lastMetrics_->askFlow;
    bool flowAligned = (currentNetFlow > 0 && lastNetFlow > 0) || (currentNetFlow < 0 && lastNetFlow < 0);
    
    // Trend continuation signal
    result.imbalanceValue = std::abs(priceMomentum) * (flowAligned ? 1.5 : 1.0);
    if (volumeMomentum > 0.1) {
        result.imbalanceValue *= (1.0 + volumeMomentum * 0.5); // Volume confirmation
    }
    
    double threshold = thresholds_.trendContinuation;
    if (result.imbalanceValue > threshold && flowAligned) {
        result.strength = calculateImbalanceStrength(result.imbalanceValue, threshold);
        result.direction = priceChange > 0 ? Direction::BUY : Direction::SELL;
        result.shouldEmitEvent = true;
        
        // Additional confirmation from market directional coefficient
        if (std::abs(metrics.marketDirectionalCoefficient) > 0.6) {
            result.strength *= 1.3; // Strong directional confirmation
        }
    }
    
    return result;
}

DetectorResult ImbalanceDetectionEngine::detectTrendReversal(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::TREND_REVERSAL;
    
    if (!lastMetrics_.has_value()) {
        return result; // Need historical data for reversal analysis
    }
    
    // Calculate price momentum
    double priceChange = metrics.midPrice - lastMetrics_->midPrice;
    double priceMomentum = priceChange / lastMetrics_->midPrice;
    
    // Calculate order flow divergence
    double currentNetFlow = metrics.bidFlow - metrics.askFlow;
    double lastNetFlow = lastMetrics_->bidFlow - lastMetrics_->askFlow;
    
    // Look for flow divergence from price movement
    bool bearishDivergence = (priceChange > 0 && currentNetFlow < lastNetFlow && currentNetFlow < 0);
    bool bullishDivergence = (priceChange < 0 && currentNetFlow > lastNetFlow && currentNetFlow > 0);
    
    if (!bearishDivergence && !bullishDivergence) {
        return result; // No divergence detected
    }
    
    // Calculate divergence strength
    double flowDivergence = std::abs(currentNetFlow - lastNetFlow) / std::max(std::abs(lastNetFlow), 1.0);
    result.imbalanceValue = flowDivergence * std::abs(priceMomentum) * 10.0;
    
    double threshold = thresholds_.trendReversal;
    if (result.imbalanceValue > threshold) {
        result.strength = calculateImbalanceStrength(result.imbalanceValue, threshold);
        result.direction = bearishDivergence ? Direction::SELL : Direction::BUY;
        result.shouldEmitEvent = true;
        
        // Enhance strength based on volatility expectation
        if (metrics.volatilityExpectation > 1.2 * lastMetrics_->volatilityExpectation) {
            result.strength *= 1.4; // Volatility spike suggests strong reversal
        }
        
        // Additional confirmation from spread behavior
        if (metrics.spread > 1.5 * lastMetrics_->spread) {
            result.strength *= 1.2; // Widening spread supports reversal thesis
        }
    }
    
    return result;
}

DetectorResult ImbalanceDetectionEngine::detectTradeImbalance(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::TRADE_IMBALANCE;
    
    // Calculate trade-based imbalance using volume velocity and flow
    double tradeIntensity = std::abs(metrics.volumeVelocity);
    double netFlow = metrics.bidFlow - metrics.askFlow;
    double totalFlow = std::abs(metrics.bidFlow) + std::abs(metrics.askFlow);
    
    if (totalFlow < 1e-9) {
        return result; // No meaningful trading activity
    }
    
    // Trade imbalance combines flow imbalance with trade intensity
    double flowImbalanceRatio = netFlow / totalFlow;
    result.imbalanceValue = std::abs(flowImbalanceRatio) * (1.0 + tradeIntensity * 0.1);
    
    double threshold = thresholds_.tradeImbalance;
    if (result.imbalanceValue > threshold) {
        result.strength = calculateImbalanceStrength(result.imbalanceValue, threshold);
        result.direction = flowImbalanceRatio > 0 ? Direction::BUY : Direction::SELL;
        result.shouldEmitEvent = true;
        
        // Boost strength for sustained trade imbalance
        if (lastMetrics_.has_value()) {
            double lastNetFlow = lastMetrics_->bidFlow - lastMetrics_->askFlow;
            double lastTotalFlow = std::abs(lastMetrics_->bidFlow) + std::abs(lastMetrics_->askFlow);
            if (lastTotalFlow > 1e-9) {
                double lastFlowImbalance = lastNetFlow / lastTotalFlow;
                // Same direction flow in consecutive periods
                if ((flowImbalanceRatio > 0 && lastFlowImbalance > 0) || 
                    (flowImbalanceRatio < 0 && lastFlowImbalance < 0)) {
                    result.strength *= 1.3; // Sustained imbalance
                }
            }
        }
    }
    
    return result;
}

DetectorResult ImbalanceDetectionEngine::detectCancellationImbalance(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::CANCELLATION_IMBALANCE;
    
    if (!lastMetrics_.has_value()) {
        return result; // Need historical data to detect cancellations
    }
    
    // Detect cancellations by looking for sudden volume drops without corresponding trades
    double bidVolumeChange = metrics.totalBidVolume - lastMetrics_->totalBidVolume;
    double askVolumeChange = metrics.totalAskVolume - lastMetrics_->totalAskVolume;
    
    // If volume drops significantly more on one side, it suggests cancellations
    double bidCancellationSignal = bidVolumeChange < 0 ? std::abs(bidVolumeChange) : 0.0;
    double askCancellationSignal = askVolumeChange < 0 ? std::abs(askVolumeChange) : 0.0;
    
    double totalCancellationSignal = bidCancellationSignal + askCancellationSignal;
    double netCancellationImbalance = bidCancellationSignal - askCancellationSignal;
    
    if (totalCancellationSignal < 1e-9) {
        return result; // No significant cancellation activity
    }
    
    result.imbalanceValue = std::abs(netCancellationImbalance) / totalCancellationSignal;
    
    double threshold = thresholds_.cancellationImbalance;
    if (result.imbalanceValue > threshold && totalCancellationSignal > 0.01 * lastMetrics_->totalVolume) {
        result.strength = calculateImbalanceStrength(result.imbalanceValue, threshold);
        result.direction = bidCancellationSignal > askCancellationSignal ? Direction::SELL : Direction::BUY;
        result.shouldEmitEvent = true;
        
        // Enhance strength based on the magnitude of cancellations relative to total volume
        double cancellationRate = totalCancellationSignal / lastMetrics_->totalVolume;
        if (cancellationRate > 0.1) {
            result.strength *= (1.0 + cancellationRate * 2.0); // Significant cancellation activity
        }
        
        // Additional boost if cancellations coincide with spread widening
        if (metrics.spread > 1.2 * lastMetrics_->spread) {
            result.strength *= 1.25; // Liquidity withdrawal pattern
        }
    }
    
    return result;
}

DetectorResult ImbalanceDetectionEngine::detectPriceVolatilityImbalance(const OrderBookMetrics& metrics) {
    DetectorResult result;
    result.type = ImbalanceType::PRICE_VOLATILITY_IMBALANCE;
    
    // Calculate volatility-based imbalance
    double volatilityRatio = metrics.realizedVolatility > 0 ? 
        metrics.volatilityExpectation / metrics.realizedVolatility : 1.0;
    
    double priceVelocity = std::abs(metrics.priceVelocity);
    double spreadExpansion = lastMetrics_.has_value() ? 
        metrics.spread / std::max(lastMetrics_->spread, 1e-9) : 1.0;
    
    // Volatility imbalance indicates mismatch between expected and realized volatility
    result.imbalanceValue = std::abs(volatilityRatio - 1.0) * (1.0 + priceVelocity * 0.5) * spreadExpansion;
    
    double threshold = thresholds_.priceVolatilityImbalance;
    if (result.imbalanceValue > threshold) {
        result.strength = calculateImbalanceStrength(result.imbalanceValue, threshold);
        
        // Direction based on price velocity and volatility relationship
        if (priceVelocity > 0.001) {
            result.direction = metrics.priceVelocity > 0 ? Direction::BUY : Direction::SELL;
        } else {
            // Use volatility expectation vs realized as direction hint
            result.direction = volatilityRatio > 1.0 ? Direction::BUY : Direction::SELL;
        }
        
        result.shouldEmitEvent = true;
        
        // Enhance strength for extreme volatility mismatches
        if (std::abs(volatilityRatio - 1.0) > 0.5) {
            result.strength *= 1.4; // Significant volatility mismatch
        }
        
        // Additional boost for high volatility per unit time
        if (metrics.volatilityPerUnitTime > 1.0) {
            result.strength *= (1.0 + metrics.volatilityPerUnitTime * 0.2);
        }
    }
    
    return result;
}


DetectorResult ImbalanceDetectionEngine::detectHiddenLiquidityImbalance(const OrderBookMetrics& metrics) {
    // Configuration thresholds for hidden liquidity detection
    static const double VELOCITY_ANOMALY_THRESHOLD = 0.5;          // Significant velocity changes
    static const double PRESSURE_VELOCITY_CORRELATION = 0.3;      // Pressure vs velocity correlation
    static const double DEPTH_VOLUME_INCONSISTENCY = 0.4;         // Depth doesn't match volume
    static const double LIQUIDITY_PROFILE_ANOMALY = 0.6;          // Unusual liquidity profile
    static const double SPREAD_PERSISTENCE_THRESHOLD = 0.8;       // Spread remains stable despite volume
    static const double VOLATILITY_SUPPRESSION_THRESHOLD = 0.3;   // Volatility lower than expected
    static const double FLOW_RESISTANCE_THRESHOLD = 0.4;          // Resistance to price movement
    static const double MARKET_IMPACT_ANOMALY = 0.5;              // Lower than expected market impact
    
    int hiddenLiquiditySignals = 0;
    const int REQUIRED_SIGNALS = 2;  // Need fewer signals as hidden liquidity is subtle
    
    // 1. Velocity Anomaly Detection
    // Hidden liquidity often causes unusual velocity patterns
    if (detectVelocityAnomalies(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 2. Spread Persistence Analysis
    // Hidden liquidity can keep spreads stable despite volume changes
    if (detectSpreadPersistence(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 3. Depth-Volume Inconsistency
    // Visible depth doesn't match the apparent market strength
    if (detectDepthVolumeInconsistency(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 4. Liquidity Profile Anomaly
    // The liquidity profile suggests more depth than visible
    if (metrics.liquidityProfileScore > LIQUIDITY_PROFILE_ANOMALY && 
        std::abs(metrics.depthImbalance) < 0.2) {  // Low visible imbalance but high profile
        hiddenLiquiditySignals++;
    }
    
    // 5. Volatility Suppression
    // Hidden liquidity often suppresses expected volatility
    if (detectVolatilitySuppression(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 6. Market Impact Resistance
    // Price movements are smaller than volume would suggest
    if (detectMarketImpactResistance(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 7. Flow Imbalance vs Price Stability
    // Significant flow imbalance but price remains stable
    double bidFlow = metrics.bidFlow;
    double askFlow = metrics.askFlow;
    double flowImbalance = std::abs(bidFlow - askFlow) / (bidFlow + askFlow + 1e-10);
    
    if (flowImbalance > FLOW_RESISTANCE_THRESHOLD && 
        std::abs(metrics.priceVelocity) < 0.1) {  // High flow imbalance but low price movement
        hiddenLiquiditySignals++;
    }
    
    // 8. Density-Pressure Mismatch
    // Order density doesn't match liquidity pressure
    if (detectDensityPressureMismatch(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 9. Time-Series Pattern Analysis
    // Look for patterns in historical metrics that suggest hidden liquidity
    if (detectHiddenLiquidityPatterns(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 10. Cross-Metric Correlation Anomalies
    // Correlations between metrics are different than expected
    if (detectCorrelationAnomalies(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // return hiddenLiquiditySignals >= REQUIRED_SIGNALS;
    DetectorResult result;
    result.type = ImbalanceType::HIDDEN_LIQUIDITY_IMBALANCE;
    result.strength = std::min(1.0, static_cast<double>(hiddenLiquiditySignals) / 10.0);
    result.direction = Direction::NEUTRAL; // Hidden liquidity is often neutral
    result.imbalanceValue = static_cast<double>(hiddenLiquiditySignals);
    result.shouldEmitEvent = (hiddenLiquiditySignals >= REQUIRED_SIGNALS);
    return result;
}

// Helper function to detect velocity anomalies
bool ImbalanceDetectionEngine::detectVelocityAnomalies(const OrderBookMetrics& metrics) {
    // Check for unusual patterns in price, volume, and spread velocities
    
    // Price velocity vs volume velocity inconsistency
    double priceVolRatio = std::abs(metrics.priceVelocity) / (std::abs(metrics.volumeVelocity) + 1e-10);
    if (priceVolRatio < 0.2) {  // Price moves much less than volume suggests
        return true;
    }
    
    // Spread velocity vs other velocities
    double spreadVolRatio = std::abs(metrics.spreadVelocity) / (std::abs(metrics.volumeVelocity) + 1e-10);
    if (spreadVolRatio < 0.3) {  // Spread stable despite volume changes
        return true;
    }
    
    // Check if liquidity pressure doesn't match velocity patterns
    if (std::abs(metrics.liquidityPressure) > 0.3 && std::abs(metrics.priceVelocity) < 0.1) {
        return true;
    }
    
    return false;
}

// Helper function to detect spread persistence
bool ImbalanceDetectionEngine::detectSpreadPersistence(const OrderBookMetrics& metrics) {
    // Hidden liquidity often keeps spreads tight despite volume imbalances
    
    // Check if spread is stable despite volume imbalance
    if (std::abs(metrics.volumeImbalance) > 0.3 && metrics.spreadPercent < 0.05) {
        return true;
    }
    
    // Check if spread velocity is low despite other changes
    if (std::abs(metrics.spreadVelocity) < 0.1 && 
        (std::abs(metrics.volumeVelocity) > 0.3 || std::abs(metrics.priceVelocity) > 0.2)) {
        return true;
    }
    
    return false;
}

// Helper function to detect depth-volume inconsistency
bool ImbalanceDetectionEngine::detectDepthVolumeInconsistency(const OrderBookMetrics& metrics) {
    // Compare visible depth with market behavior
    
    // Calculate apparent market strength vs visible depth
    double apparentStrength = std::abs(metrics.marketDirectionalCoefficient) * 
                             std::abs(metrics.liquidityPressure);
    
    double visibleDepthStrength = (metrics.totalBidVolume + metrics.totalAskVolume) * 
                                 (1.0 - std::abs(metrics.depthImbalance));
    
    // If apparent strength is much higher than visible depth suggests
    if (apparentStrength > 0.4 && visibleDepthStrength < 0.3) {
        return true;
    }
    
    // Check for density vs volume mismatch
    double avgDensity = (metrics.bidDensity + metrics.askDensity) / 2.0;
    double totalVolume = metrics.totalBidVolume + metrics.totalAskVolume;
    
    if (avgDensity > 0.7 && totalVolume < 0.3) {  // High density but low visible volume
        return true;
    }
    
    return false;
}

// Helper function to detect volatility suppression
bool ImbalanceDetectionEngine::detectVolatilitySuppression(const OrderBookMetrics& metrics) {
    // Hidden liquidity often suppresses volatility below expected levels
    
    // Compare realized vs expected volatility
    if (metrics.volatilityExpectation > 0.3 && metrics.realizedVolatility < 0.15) {
        return true;
    }
    
    // Check if volatility is low despite market pressure
    if (std::abs(metrics.liquidityPressure) > 0.4 && metrics.realizedVolatility < 0.2) {
        return true;
    }
    
    // Volatility per unit time is lower than pressure suggests
    double expectedVolatility = std::abs(metrics.liquidityPressure) * 0.5;
    if (metrics.volatilityPerUnitTime < expectedVolatility * 0.5) {
        return true;
    }
    
    return false;
}

// Helper function to detect market impact resistance
bool ImbalanceDetectionEngine::detectMarketImpactResistance(const OrderBookMetrics& metrics) {
    // Price impact is lower than volume/pressure would suggest
    
    // Calculate expected price impact based on volume imbalance
    double expectedImpact = std::abs(metrics.volumeImbalance) * 0.3;
    if (expectedImpact > 0.2 && std::abs(metrics.priceVelocity) < expectedImpact * 0.3) {
        return true;
    }
    
    // Check if weighted imbalance suggests more impact than realized
    double expectedWeightedImpact = std::abs(metrics.weightedVolumeImbalance) * 0.4;
    if (expectedWeightedImpact > 0.15 && std::abs(metrics.priceVelocity) < expectedWeightedImpact * 0.25) {
        return true;
    }
    
    return false;
}

// Helper function to detect density-pressure mismatch
bool ImbalanceDetectionEngine::detectDensityPressureMismatch(const OrderBookMetrics& metrics) {
    // Order density patterns don't match liquidity pressure
    
    double densityImbalance = (metrics.bidDensity - metrics.askDensity) / 
                             (metrics.bidDensity + metrics.askDensity + 1e-10);
    
    // Density suggests one direction but pressure suggests another
    if (std::abs(densityImbalance) > 0.3 && std::abs(metrics.liquidityPressure) > 0.3) {
        // Check if they point in opposite directions
        if ((densityImbalance > 0 && metrics.liquidityPressure < -0.1) ||
            (densityImbalance < 0 && metrics.liquidityPressure > 0.1)) {
            return true;
        }
    }
    
    return false;
}

// Helper function to detect hidden liquidity patterns in time series
bool ImbalanceDetectionEngine::detectHiddenLiquidityPatterns(const OrderBookMetrics& metrics) {
    // This would analyze patterns in the timeSeriesMetrics map
    // For now, implement basic pattern detection
    
    // Check if we have time series data
    if (metrics.timeSeriesMetrics.empty()) {
        return false;
    }
    
    // Look for patterns where metrics suggest hidden liquidity
    // This is a simplified implementation - in practice, you'd want more sophisticated pattern recognition
    
    auto spreadSeries = metrics.timeSeriesMetrics.find("spreadPercent");
    auto volumeSeries = metrics.timeSeriesMetrics.find("totalVolume");
    
    if (spreadSeries != metrics.timeSeriesMetrics.end() && 
        volumeSeries != metrics.timeSeriesMetrics.end()) {
        
        const auto& spreads = spreadSeries->second;
        const auto& volumes = volumeSeries->second;
        
        if (spreads.size() >= 3 && volumes.size() >= 3) {
            // Check for spread stability despite volume changes
            double spreadVariance = calculateVariance(spreads);
            double volumeVariance = calculateVariance(volumes);
            
            if (spreadVariance < 0.01 && volumeVariance > 0.1) {  // Stable spread, volatile volume
                return true;
            }
        }
    }
    
    return false;
}

// Helper function to detect correlation anomalies
bool ImbalanceDetectionEngine::detectCorrelationAnomalies(const OrderBookMetrics& metrics) {
    // Check for unusual correlations between metrics that might indicate hidden liquidity
    
    // Example: High market directional coefficient but low depth imbalance
    if (std::abs(metrics.marketDirectionalCoefficient) > 0.6 && 
        std::abs(metrics.depthImbalance) < 0.2) {
        return true;
    }
    
    // High liquidity profile score but moderate visible volume
    if (metrics.liquidityProfileScore > 0.7 && 
        (metrics.totalBidVolume + metrics.totalAskVolume) < 0.5) {
        return true;
    }
    
    return false;
}

// Custom detector hook: looks for a user-provided "customSignal" metric and
// exposes it as a DetectorResult of type CUSTOM. This is intentionally small
// and non-invasive — it avoids heavy dependencies and provides a safe default
// so callers (and the linker) have a concrete implementation.
DetectorResult ImbalanceDetectionEngine::detectCustomImbalance(const OrderBookMetrics& metrics) {
    DetectorResult r;
    r.type = ImbalanceType::CUSTOM;

    try {
        auto it = metrics.metrics.find("customSignal");
        if (it != metrics.metrics.end()) {
            double v = it->second;
            r.imbalanceValue = v;
            r.strength = std::min(1.0, std::max(0.0, std::abs(v)));
            if (v > 0) r.direction = Direction::BUY;
            else if (v < 0) r.direction = Direction::SELL;
            else r.direction = Direction::NEUTRAL;
            r.shouldEmitEvent = (r.strength > 0.2);
        }
    } catch (...) {
        // swallow errors to keep detector non-fatal
    }

    return r;
}

// Utility function to calculate variance
double ImbalanceDetectionEngine::calculateVariance(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    double variance = 0.0;
    
    for (double value : data) {
        variance += (value - mean) * (value - mean);
    }
    
    return variance / data.size();
}
// Calculate imbalance strength based on type and metrics
 double ImbalanceDetectionEngine::calculateImbalanceStrength(ImbalanceType type, const OrderBookMetrics& metrics) {
     double strength = 0.0;
     
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: {
             auto bidVolIt = metrics.metrics.find("bidVolume");
             auto askVolIt = metrics.metrics.find("askVolume");
             
             if (bidVolIt != metrics.metrics.end() && askVolIt != metrics.metrics.end()) {
                 double bidVol = bidVolIt->second;
                 double askVol = askVolIt->second;
                 
                 if (askVol < 1e-10) askVol = 1e-10;
                 double ratio = bidVol / askVol;
                 
                 strength = std::abs(ratio - 1.0);
             }
             break;
         }
         case ImbalanceType::PRICE_LEVEL_IMBALANCE: {
             auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
             auto askLevelsIt = metrics.metrics.find("askLevelCount");
             
             if (bidLevelsIt != metrics.metrics.end() && askLevelsIt != metrics.metrics.end()) {
                 double bidLevels = bidLevelsIt->second;
                 double askLevels = askLevelsIt->second;
                 
                 if (askLevels < 1e-10) askLevels = 1e-10;
                 double ratio = bidLevels / askLevels;
                 
                 strength = std::abs(ratio - 1.0);
             }
             break;
         }
         case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE: {
             auto bidFlowIt = metrics.metrics.find("bidFlow");
             auto askFlowIt = metrics.metrics.find("askFlow");
             
             if (bidFlowIt != metrics.metrics.end() && askFlowIt != metrics.metrics.end()) {
                 double bidFlow = bidFlowIt->second;
                 double askFlow = askFlowIt->second;
                 
                 double netFlow = bidFlow - askFlow;
                 double totalFlow = std::abs(bidFlow) + std::abs(askFlow);
                 
                 if (totalFlow < 1e-10) totalFlow = 1e-10;
                 strength = std::abs(netFlow) / totalFlow;
             }
             break;
         }
         case ImbalanceType::ABNORMAL_SPREAD: {
             auto spreadIt = metrics.metrics.find("spreadPercent");
             auto avgSpreadIt = metrics.metrics.find("averageSpreadPercent");
             
             if (spreadIt != metrics.metrics.end() && avgSpreadIt != metrics.metrics.end()) {
                 double currentSpread = spreadIt->second;
                 double averageSpread = avgSpreadIt->second;
                 
                 if (averageSpread < 1e-10) averageSpread = 1e-10;
                 strength = (currentSpread / averageSpread) - 1.0;
                 
                 // Clamp negative values to zero
                 if (strength < 0) strength = 0;
             }
             break;
         }

        // VOLUME_IMBALANCE,              // bid vs ask volume
        // PRICE_LEVEL_IMBALANCE,         // level count imbalance
        // LIQUIDITY_REMOVAL,             // sudden removal
        // ORDER_FLOW_IMBALANCE,          // order submissions imbalance
        // LARGE_ORDER_APPEARANCE,        // big orders enter
        // LARGE_ORDER_DISAPPEARANCE,     // big orders vanish
        // PRICE_LEVEL_CONCENTRATION,     // volume concentrated at specific price
        // BID_ASK_SPREAD_CHANGE,         // spread change
        // MARKET_DEPTH_CHANGE,           // depth change
        // LIQUIDITY_FLOW_IMBALANCE,      // liquidity flow imbalance
        // ABNORMAL_SPREAD,               // spread abnormal
        // LIQUIDITY_IMBALANCE,           // general liquidity imbalance
        // TREND_CONTINUATION,            // price trend continuation
        // TREND_REVERSAL,                // price trend reversal
        // TRADE_IMBALANCE,               // aggressive trades imbalance
        // CANCELLATION_IMBALANCE,        // one-sided cancellations
        // PRICE_VOLATILITY_IMBALANCE,    // volatility surge
        // HIDDEN_LIQUIDITY_IMBALANCE,    // iceberg / hidden liquidity
        case ImbalanceType::MARKET_DEPTH_CHANGE:
            strength = std::abs(metrics.metrics.at("depthChange"));
            break;
        case ImbalanceType::LIQUIDITY_IMBALANCE:
            strength = std::abs(metrics.metrics.at("liquidityImbalance"));
            break;
        case ImbalanceType::ORDER_FLOW_IMBALANCE:
            strength = std::abs(metrics.metrics.at("orderFlowImbalance"));
            break;
        case ImbalanceType::LARGE_ORDER_APPEARANCE:
            strength = std::abs(metrics.metrics.at("largeOrderImpact"));
            break;
        case ImbalanceType::LARGE_ORDER_DISAPPEARANCE:
            strength = std::abs(metrics.metrics.at("largeOrderImpact"));
            break;
        case ImbalanceType::PRICE_LEVEL_CONCENTRATION:
            strength = std::abs(metrics.metrics.at("priceLevelConcentration"));
            break;
        case ImbalanceType::BID_ASK_SPREAD_CHANGE:
            strength = std::abs(metrics.metrics.at("spreadChange"));
            break;
        case ImbalanceType::TREND_CONTINUATION:
            strength = std::abs(metrics.metrics.at("trendStrength"));
            break;
        case ImbalanceType::TREND_REVERSAL:
            strength = std::abs(metrics.metrics.at("trendStrength"));
            break;
        case ImbalanceType::TRADE_IMBALANCE:
            strength = std::abs(metrics.metrics.at("tradeImbalanceRatio"));
            break;
        case ImbalanceType::CANCELLATION_IMBALANCE:
            strength = std::abs(metrics.metrics.at("cancellationImbalanceRatio"));
            break;
        case ImbalanceType::PRICE_VOLATILITY_IMBALANCE:
            strength = std::abs(metrics.metrics.at("volatilityImbalanceRatio"));
            break;
        // case ImbalanceType::CUSTOM:
        //     strength = std::abs(metrics.metrics.at("customSignal"));
        //     break;  
        
         default:
             break;
     }
     
     return strength;
 }
 

 // Determine imbalance direction based on type and metrics
 Direction ImbalanceDetectionEngine::determineImbalanceDirection(ImbalanceType type, const OrderBookMetrics& metrics) {
     Direction direction = Direction::NEUTRAL;
     
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: {
             auto bidVolIt = metrics.metrics.find("bidVolume");
             auto askVolIt = metrics.metrics.find("askVolume");
             
             if (bidVolIt != metrics.metrics.end() && askVolIt != metrics.metrics.end()) {
                 double bidVol = bidVolIt->second;
                 double askVol = askVolIt->second;
                 
                 direction = (bidVol > askVol) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::PRICE_LEVEL_IMBALANCE: {
             auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
             auto askLevelsIt = metrics.metrics.find("askLevelCount");
             
             if (bidLevelsIt != metrics.metrics.end() && askLevelsIt != metrics.metrics.end()) {
                 double bidLevels = bidLevelsIt->second;
                 double askLevels = askLevelsIt->second;
                 
                 direction = (bidLevels > askLevels) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE: {
             auto bidFlowIt = metrics.metrics.find("bidFlow");
             auto askFlowIt = metrics.metrics.find("askFlow");
             
             if (bidFlowIt != metrics.metrics.end() && askFlowIt != metrics.metrics.end()) {
                 double bidFlow = bidFlowIt->second;
                 double askFlow = askFlowIt->second;
                 
                 direction = (bidFlow > askFlow) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::ABNORMAL_SPREAD: {
             // For abnormal spread, direction determination is more complex
             // Default to SELL as widening spread often indicates selling pressure
             direction = Direction::SELL;
             
             // Check if there's additional data to determine direction more accurately
             auto bidVolumeIt = metrics.metrics.find("bidVolume");
             auto askVolumeIt = metrics.metrics.find("askVolume");
             
             if (bidVolumeIt != metrics.metrics.end() && askVolumeIt != metrics.metrics.end()) {
                 double bidVolume = bidVolumeIt->second;
                 double askVolume = askVolumeIt->second;
                 
                 // If bid volume is significantly higher, it might indicate buying pressure despite the wide spread
                 if (bidVolume > askVolume * 1.5) {
                     direction = Direction::BUY;
                 }
             }
             break;
        }
         case ImbalanceType::MARKET_DEPTH_CHANGE:
         //is this correct?
             direction = (metrics.metrics.at("depthChange") > 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::TREND_CONTINUATION:
             direction = (metrics.metrics.at("trendStrength") > 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::TREND_REVERSAL:
             direction = (metrics.metrics.at("trendStrength") < 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::CUSTOM:
             direction = (metrics.metrics.at("customSignal") > 0) ? Direction::BUY : Direction::SELL;
             break;
         default:
             break;
    }
     return direction;
 }

 void ImbalanceDetectionEngine::createImbalanceEvent(ImbalanceType type, double strength, Direction direction, const OrderBookMetrics& metrics) {
    // Validate inputs
    if (strength < 0.0 || strength > 1.0) {
        LOG_WARN("Invalid strength value: {}", strength);
        return;
    }
    
    if (direction == Direction::NEUTRAL) {
        LOG_WARN("Cannot create imbalance event with NEUTRAL direction");
        return;
    }
    
    ImbalanceEvent event;
    event.type = type;
    event.strength = std::clamp(strength, 0.0, 1.0);
    event.direction = direction;
    event.detectionTime = std::chrono::system_clock::now();
    event.exchangeId = metrics.exchangeId;
    event.symbol = metrics.symbol;
    event.confidence = trading::core::valueToConfidenceLevel(calculateImbalanceConfidence(type, strength, metrics)); 
    event.eventId = generateEventId();
    
    // Copy relevant metrics
    for (const auto& metric : metrics.metrics) {
        event.supportingMetrics[metric.first] = metric.second;
    }
    //only print for every 100 events
    static std::atomic<int> eventCounter{0};
    int count = ++eventCounter;
    if (count % 1000 == 0 || event.strength > 0.2 || event.confidence >= ConfidenceLevel::LOW) {
        LOG_TRACE("ImbalanceDetectionEngine: created ImbalanceEvent id={} type={} strength={} dir={}, confidence={} for {}:{} on thread {}, count ={}",
                event.eventId,
                static_cast<int>(event.type),
                event.strength,
                static_cast<int>(event.direction),
                static_cast<int>(event.confidence),
                event.exchangeId,
                event.symbol,
                std::this_thread::get_id(),
                count);
    }

    // Publish to EventBus / callbacks unless publishing is disabled for debugging
    if (!publishEvents_.load()) {
        LOG_TRACE("ImbalanceDetectionEngine: publishing disabled, skipping ImbalanceEvent id={}", event.eventId);
        return;
    }

    // Option 2: Event bus only (better for decoupling)
    common::EventBus::getInstance().publish(events::ImbalanceDetectedEvent{event});
}

void ImbalanceDetectionEngine::createImbalanceEventForBayesModel(ImbalanceType type, double strength,
    core::Direction direction, const OrderBookMetrics& metrics, double bayesProbability, double uncertainty) {
    // Validate inputs
    if (strength < 0.0 || strength > 1.0) {
        LOG_WARN("Invalid strength value: {}", strength);
        return;
    }
    
    if (direction == core::Direction::NEUTRAL) {
        LOG_WARN("Cannot create imbalance event with NEUTRAL direction");
        return;
    }
    
    ImbalanceEvent event;
    event.type = type;
    event.strength = std::clamp(strength, 0.0, 1.0);
    event.direction = direction;
    event.detectionTime = std::chrono::system_clock::now();
    event.exchangeId = metrics.exchangeId;
    event.symbol = metrics.symbol;
    
    // event.confidence = trading::core::valueToConfidenceLevel(calculateImbalanceConfidence(type, strength, metrics)); 
    event.eventId = generateEventId();
    //convert probability to confidence level
    // event.confidence = trading::core::valueToConfidenceLevel(bayesProbability);
    auto confidence = static_cast<int>((bayesProbability * (1.0 - uncertainty)) * 100);
    event.confidence = trading::core::valueToConfidenceLevel(confidence);
    // Copy relevant metrics
    for (const auto& metric : metrics.metrics) {
        event.supportingMetrics[metric.first] = metric.second;
    }
    //only print for every 100 events
    static std::atomic<int> eventCounter{0};
    int count = ++eventCounter;
    if (count % 1000 == 0 || event.strength > 0.2) {
        LOG_TRACE("createImbalanceEventForBayesModel: created ImbalanceEvent id={} type={} strength={} dir={}, confidence={} bayesProb={} for {}:{} on thread {}, count ={}",
                event.eventId,
                static_cast<int>(event.type),
                event.strength,
                static_cast<int>(event.direction),
                static_cast<int>(event.confidence),
                bayesProbability,
                event.exchangeId,
                event.symbol,
                std::this_thread::get_id(),
                count);
    }

    // Publish to EventBus / callbacks unless publishing is disabled for debugging
    if (!publishEvents_.load()) {
        LOG_TRACE("ImbalanceDetectionEngine: publishing disabled, skipping ImbalanceEvent id={}", event.eventId);
        return;
    }

    // Option 2: Event bus only (better for decoupling)
    common::EventBus::getInstance().publish(events::ImbalanceDetectedEvent{event});    
}

 void ImbalanceDetectionEngine::notifyImbalanceCallbacks(const ImbalanceEvent& event) {
    if (!publishEvents_.load()) {
        LOG_TRACE("ImbalanceDetectionEngine: publishing disabled, skipping callback notify for eventId={}", event.eventId);
        return;
    }
    // Simply publish to event bus - no locks, no deadlocks
    common::EventBus::getInstance().publish(events::ImbalanceDetectedEvent{event});
}
 // Generate a unique event ID
 uint64_t ImbalanceDetectionEngine::generateEventId() {
     static uint64_t nextId = 0;
     return ++nextId;
 } 
 
// PRODUCTION FIX: Thread-safe labeling statistics implementation
void ImbalanceDetectionEngine::trackLabelingEvent(core::Direction labelDirection, 
                                                 const std::string& symbol,
                                                 double normalizedReturn, 
                                                 std::chrono::milliseconds delay) {
    // Type-safe statistics update
    size_t totalCount = labelingStats_.totalLabels.fetch_add(1) + 1;
    
    switch (labelDirection) {
        case core::Direction::BUY:
            labelingStats_.buyLabels.fetch_add(1);
            break;
        case core::Direction::SELL:
            labelingStats_.sellLabels.fetch_add(1);
            break;
        case core::Direction::NEUTRAL:
            labelingStats_.neutralLabels.fetch_add(1);
            break;
    }
    
    // Periodic logging with proper enum-to-string conversion
    if (totalCount % 100 == 0) {
        double buyPct = (double)labelingStats_.buyLabels.load() / totalCount * 100.0;
        double sellPct = (double)labelingStats_.sellLabels.load() / totalCount * 100.0;
        double neutralPct = (double)labelingStats_.neutralLabels.load() / totalCount * 100.0;
        
        // Type-safe direction string conversion
        const char* directionStr = "";
        switch (labelDirection) {
            case core::Direction::BUY: directionStr = "BUY"; break;
            case core::Direction::SELL: directionStr = "SELL"; break; 
            case core::Direction::NEUTRAL: directionStr = "NEUTRAL"; break;
        }
        
        LOG_TRACE("LabelingStats [PRODUCTION]", 
                "Total={} BUY={:.1f}% SELL={:.1f}% NEUTRAL={:.1f}% | Current: Symbol={} ret={:.4f} label={} delay={}ms", 
                totalCount, buyPct, sellPct, neutralPct,
                symbol, normalizedReturn, directionStr, delay.count());
    }
}

// Optimal exit strategy methods replaced the old transition tracking
// Old methods removed - now using OptimalExitStrategy for better HFT performance

} // namespace strategy
} // namespace trading

// Define the global registration function in the global namespace so it
// matches the declaration in the public header
// void registerGlobalOptimalExitCallback(const trading::strategy::OptimalExitStrategy::ExitCallback& callback) {
//     // Instrumentation: log registration calls forwarded to file-scoped g_optimalExit
//     LOG_INFO("registerGlobalOptimalExitCallback: forwarding registration to file-scoped g_optimalExit (addr={})", static_cast<const void*>(&trading::strategy::g_optimalExit));
//     // Forward to the internal global instance defined in this translation unit
//     trading::strategy::g_optimalExit.registerExitCallback(callback);
// }