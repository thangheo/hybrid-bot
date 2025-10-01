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
 #include <sstream>
#include <filesystem>
#include <logger.hpp>
#include "perception/bayesian_trading_adapter.hpp"

namespace trading {
namespace strategy {
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
    std::string key = exch.toString() + "_" + sym.toString();
    {
        std::lock_guard<std::mutex> lk(bayesHandlesMutex_);
        auto it = bayesHandles_.find(key);
        if (it != bayesHandles_.end()) return it->second;
    }

    trading::perception::BayesHandle h = nullptr;
    try {
        h = trading::perception::bayes_create();
    } catch (...) {
        return nullptr;
    }

    // Try load from per-symbol path if configured
    if (!bayesModelDirectory_.empty()) {
        try {
            std::string path = buildSymbolModelPath(bayesModelDirectory_, exch, sym);
            if (!path.empty() && std::filesystem::exists(path)) {
                trading::perception::bayes_load(h, path);
                // LOG_DEBUG("ImbalanceDetectionEngine: loaded per-symbol bayes model {}", path);
            }
        } catch (const std::exception &ex) {
            LOG_WARN("ImbalanceDetectionEngine: failed to load per-symbol bayes model: {}", ex.what());
        } catch (...) {
            LOG_WARN("ImbalanceDetectionEngine: unknown error loading per-symbol bayes model");
        }
    }

    {
        std::lock_guard<std::mutex> lk(bayesHandlesMutex_);
        bayesHandles_[key] = h;
    }
    return h;
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

                trading::perception::bayes_save(bayes_handle_, bayesModelPath_);
                LOG_INFO("ImbalanceDetectionEngine: saved bayes model to {}", bayesModelPath_);
            } catch (const std::exception &ex) {
                LOG_WARN("ImbalanceDetectionEngine: failed to save bayes model {}: {}", bayesModelPath_, ex.what());
            } catch (...) {
                LOG_WARN("ImbalanceDetectionEngine: unknown error while saving bayes model {}", bayesModelPath_);
            }
        }
        // Save and destroy any per-symbol handles
        {
            std::lock_guard<std::mutex> lk2(bayesHandlesMutex_);
            for (auto &pr : bayesHandles_) {
                auto &symKey = pr.first;
                auto h = pr.second;
                try {
                    if (!bayesModelDirectory_.empty()) {
                        // Save using the key-derived filename
                        std::string filename = bayesModelDirectory_;
                        if (!filename.empty() && filename.back() != '/' && filename.back() != '\\') filename += '/';
                        filename += symKey + ".json";
                        trading::perception::bayes_save(h, filename);
                        LOG_INFO("ImbalanceDetectionEngine: saved per-symbol bayes model {}", filename);
                    }
                } catch (...) {
                    LOG_WARN("ImbalanceDetectionEngine: failed to save per-symbol bayes model for {}", symKey);
                }
                try { trading::perception::bayes_destroy(h); } catch(...) {}
            }
            bayesHandles_.clear();
        }

        if (bayes_handle_) {
            try {
                trading::perception::bayes_destroy(bayes_handle_);
            } catch (...) {}
            bayes_handle_ = nullptr;
            bayesInitialized_.store(false);
            LOG_DEBUG("ImbalanceDetectionEngine: destroyed bayes adapter handle");
        }
    } catch (...) {
        // swallow
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
                    results.push_back(detectVolumeImbalance(m));
                    results.push_back(detectPriceLevelImbalance(m));
                    results.push_back(detectLiquidityFlowImbalance(m));
                    results.push_back(detectAbnormalSpread(m));
                    results.push_back(detectMarketDepthChange(m));
                    results.push_back(detectLargeOrderAppearance(m));
                    results.push_back(detectLargeOrderDisappearance(m));
                    results.push_back(detectOrderFlowImbalance(m));
                    results.push_back(detectPriceLevelConcentration(m));
                    results.push_back(detectLiquidityImbalance(m));
                    results.push_back(detectTrendContinuation(m));
                    results.push_back(detectTrendReversal(m));
                    results.push_back(detectTradeImbalance(m));
                    results.push_back(detectCancellationImbalance(m));
                    results.push_back(detectPriceVolatilityImbalance(m));
                    results.push_back(detectHiddenLiquidityImbalance(m));
                    results.push_back(detectCustomImbalance(m));
                } catch (...) {
                    // swallow detector errors
                }

                // Build feature vector from detectors
                std::vector<double> features;
                features.reserve(results.size());
                for (const auto &r : results) {
                    double v = 0.0;
                    if (std::isfinite(r.strength) && r.strength > 0.0) {
                        v = r.strength;
                    } else if (std::isfinite(r.imbalanceValue)) {
                        v = calculateImbalanceStrength(r.imbalanceValue, 1.0);
                    }
                    features.push_back(v);
                }

                // Buffer features for later labeling/training
                {
                    std::lock_guard<std::mutex> pl(pendingTrainingMutex_);
                    if (pendingTraining_.size() >= maxPendingTraining_) pendingTraining_.pop_front();
                    pendingTraining_.push_back(PendingTraining{m.timestamp, features, m.midPrice, m.exchangeId, m.symbol});
                }

                // Predict with Bayesian model and emit event if needed
                if (bayesInitialized_.load()) {
                    try {
                        trading::perception::BayesSignal signal;
                        trading::perception::BayesHandle handle = getBayesHandleForSymbol(m.exchangeId, m.symbol);
                        if (handle) signal = trading::perception::bayes_predict(handle, features);
                        if (signal.should_trade) {
                            core::Direction dir = signal.direction;
                            OrderBookMetrics mcopy = m;
                            mcopy.metrics["bayes_probability"] = signal.probability;
                            mcopy.metrics["bayes_expected_value"] = signal.expected_value;
                            mcopy.metrics["bayes_position_size"] = signal.position_size;
                            mcopy.metrics["bayes_uncertainty"] = signal.uncertainty;
                            double event_strength = signal.position_size;
                            createImbalanceEventForBayesModel(ImbalanceType::CUSTOM, event_strength,
                                                            dir, mcopy, signal.probability, signal.uncertainty);
                        }
                    } catch (const std::exception &ex) {
                        LOG_WARN("Bayes predict threw: {}", ex.what());
                    } catch (...) {
                        LOG_WARN("Bayes predict threw unknown");
                    }
                }

                // Process pending training samples
                try {
                    std::vector<std::tuple<std::vector<double>, int, core::ExchangeId, core::Symbol>> toTrain;
                    {
                        std::lock_guard<std::mutex> pl(pendingTrainingMutex_);
                        auto now = m.timestamp;
                        while (!pendingTraining_.empty()) {
                            const auto &pt = pendingTraining_.front();
                            if (now - pt.timestamp >= labelingDelay_) {
                                double priceChangePct = (m.midPrice - pt.midPrice) / pt.midPrice;
                                int label;
                                const double THRESHOLD = 0.1/100;
                                if (priceChangePct > THRESHOLD) label = 2; else if (priceChangePct < -THRESHOLD) label = 0; else label = 1;
                                toTrain.emplace_back(pt.features, label, pt.exchangeId, pt.symbol);
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
                            trading::perception::BayesHandle handle = getBayesHandleForSymbol(exch, sym);
                            if (!handle) continue;
                            trading::perception::bayes_update(handle, features, label);
                        } catch (...) {}
                    }
                } catch (...) {}
            }
        });
    }
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
    // Minimum debounce interval between saves
    if (config_.bayesMinSaveIntervalMs > 0) {
        bayesMinSaveInterval_ = std::chrono::milliseconds(config_.bayesMinSaveIntervalMs);
        LOG_INFO("ImbalanceDetectionEngine: bayes min save interval set to {} ms", config_.bayesMinSaveIntervalMs);
    }

    // Optional labeling delay from config (in ms)
    if (config_.labelingDelayMs > 0) labelingDelay_ = std::chrono::milliseconds(config_.labelingDelayMs);
    

    // Create and load Bayesian model adapter handle upfront for legacy single-file mode.
    try {
        std::lock_guard<std::mutex> lk(bayesMutex_);
        if (!bayesModelPath_.empty()) {
            // legacy single-file mode: create a shared global handle
            if (!bayes_handle_) {
                bayes_handle_ = trading::perception::bayes_create();
                LOG_DEBUG("ImbalanceDetectionEngine: created bayes adapter handle (single-file mode)");
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
        LOG_DEBUG("ImbalanceDetectionEngine: bayes lastSaveTime initialized");
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
                detectorQueueCv_.notify_one();
                // LOG_DEBUG("ImbalanceDetectionEngine: enqueued metrics for {}:{} (queue size={})", metrics.exchangeId, metrics.symbol, detectorQueue_.size());
                return false; // enqueued for async processing
            } else {
                LOG_WARN("ImbalanceDetectionEngine: detector queue full ({}), falling back to synchronous run", detectorQueue_.size());
            }
        }
    }

    return false;
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
    
    // Calculate flow ratio (like volume ratio in your existing function)
    double flowRatio = (bidFlow > askFlow) ? bidFlow / askFlow : askFlow / bidFlow;
    result.imbalanceValue = flowRatio;
    
    // Get threshold
    double threshold;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threshold = thresholds_.getTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE).activation_threshold;
    }
    
    // Use existing calculateImbalanceStrength function
    result.strength = calculateImbalanceStrength(flowRatio, threshold);
    result.direction = (bidFlow > askFlow) ? Direction::BUY : Direction::SELL;
    result.shouldEmitEvent = (result.strength > 0.2);
    
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
    
    // 5. Bid-ask volume ratio strength
    double bidAskRatio = std::max(metrics.bidAskVolumeRatio, 1.0 / metrics.bidAskVolumeRatio);
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
    // Aggregate directional signals
    double buySignal = 0.0;
    double sellSignal = 0.0;
    
    // Volume imbalance direction
    if (metrics.volumeImbalance > 0) buySignal += std::abs(metrics.volumeImbalance);
    else sellSignal += std::abs(metrics.volumeImbalance);
    
    // Weighted imbalance direction  
    if (metrics.weightedVolumeImbalance > 0) buySignal += std::abs(metrics.weightedVolumeImbalance);
    else sellSignal += std::abs(metrics.weightedVolumeImbalance);
    
    // Bid-ask ratio direction
    if (metrics.bidAskVolumeRatio > 1.0) buySignal += (metrics.bidAskVolumeRatio - 1.0);
    else sellSignal += (1.0 / metrics.bidAskVolumeRatio - 1.0);
    
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
        LOG_DEBUG("ImbalanceDetectionEngine: publishing disabled, skipping ImbalanceEvent id={}", event.eventId);
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
        LOG_DEBUG("ImbalanceDetectionEngine: publishing disabled, skipping ImbalanceEvent id={}", event.eventId);
        return;
    }

    // Option 2: Event bus only (better for decoupling)
    common::EventBus::getInstance().publish(events::ImbalanceDetectedEvent{event});    
}

 void ImbalanceDetectionEngine::notifyImbalanceCallbacks(const ImbalanceEvent& event) {
    if (!publishEvents_.load()) {
        LOG_DEBUG("ImbalanceDetectionEngine: publishing disabled, skipping callback notify for eventId={}", event.eventId);
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
 
 } // namespace strategy
 } // namespace trading