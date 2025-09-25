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
#include <logger.hpp>
#include "perception/bayesian_trading_adapter.hpp"

 namespace trading {
 namespace strategy {

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
    // Stop detector worker if running
    try {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            detectorWorkerRunning_.store(false);
        }
        detectorQueueCv_.notify_all();
        if (detectorWorkerThread_.joinable()) detectorWorkerThread_.join();
    } catch (...) {
        // swallow in destructor
    }
    // Persist bayesian model if configured and destroy adapter handle
    try {
        if (bayes_handle_ && !bayesModelPath_.empty()) {
            try {
                trading::perception::bayes_save(bayes_handle_, bayesModelPath_);
                LOG_INFO("ImbalanceDetectionEngine: saved bayes model to {}", bayesModelPath_);
            } catch (const std::exception &ex) {
                LOG_WARN("ImbalanceDetectionEngine: failed to save bayes model {}: {}", bayesModelPath_, ex.what());
            } catch (...) {
                LOG_WARN("ImbalanceDetectionEngine: unknown error while saving bayes model {}");
            }
        }
    } catch (...) {
        // swallow
    }
    try {
        if (bayes_handle_) {
            trading::perception::bayes_destroy(bayes_handle_);
            bayes_handle_ = nullptr;
            LOG_DEBUG("ImbalanceDetectionEngine: destroyed bayes adapter handle");
        }
    } catch (...) {}
}

// Start the detector worker (call from initialization path if desired)
void ImbalanceDetectionEngine::startDetectorWorker(size_t threads) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (detectorWorkerRunning_.load()) return;
    detectorWorkerRunning_.store(true);
    detectorWorkerThread_ = std::thread([this](){
        while (detectorWorkerRunning_.load()) {
            OrderBookMetrics m;
            {
                std::unique_lock<std::mutex> qlk(detectorQueueMutex_);
                detectorQueueCv_.wait_for(qlk, std::chrono::milliseconds(50), [this]{
                    return !detectorQueue_.empty() || !detectorWorkerRunning_.load();
                });
                if (!detectorQueue_.empty()) {
                    m = std::move(detectorQueue_.front());
                    detectorQueue_.pop_front();
                } else {
                    continue;
                }
            }
            // Run detectors directly in the worker thread and time them to detect
            // any slow detector. Running here avoids nested submission to the
            // shared thread-pool which could cause starvation.
            const auto DETECTOR_WARN_MS = 100; // warn threshold
            std::vector<DetectorResult> results;
            auto runDirect = [&](const char *name, const std::function<DetectorResult()> &fn) {
                try {
                    auto t0 = std::chrono::steady_clock::now();
                    DetectorResult res = fn();
                    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
                    if (dt > DETECTOR_WARN_MS) {
                        LOG_WARN("Detector worker: '{}' took {} ms", name, dt);
                    } else {
                        LOG_DEBUG("Detector worker: '{}' took {} ms", name, dt);
                    }
                    return res;
                } catch (const std::exception &ex) {
                    LOG_WARN("Detector worker: '{}' threw: {}", name, ex.what());
                } catch (...) {
                    LOG_WARN("Detector worker: '{}' threw unknown exception", name);
                }
                return DetectorResult{};
            };

            results.emplace_back(runDirect("detectVolumeImbalance", [&]{ return detectVolumeImbalance(m); }));
            results.emplace_back(runDirect("detectPriceLevelImbalance", [&]{ return detectPriceLevelImbalance(m); }));
            results.emplace_back(runDirect("detectLiquidityFlowImbalance", [&]{ return detectLiquidityFlowImbalance(m); }));
            results.emplace_back(runDirect("detectAbnormalSpread", [&]{ return detectAbnormalSpread(m); }));
            results.emplace_back(runDirect("detectMarketDepthChange", [&]{ return detectMarketDepthChange(m); }));
            results.emplace_back(runDirect("detectLargeOrderAppearance", [&]{ return detectLargeOrderAppearance(m); }));
            results.emplace_back(runDirect("detectLargeOrderDisappearance", [&]{ return detectLargeOrderDisappearance(m); }));
            // results.emplace_back(runDirect("detectLiquidityRemoval", [&]{ return detectLiquidityRemoval(m); }));
            results.emplace_back(runDirect("detectOrderFlowImbalance", [&]{ return detectOrderFlowImbalance(m); }));
            results.emplace_back(runDirect("detectPriceLevelConcentration", [&]{ return detectPriceLevelConcentration(m); }));
            results.emplace_back(runDirect("detectLiquidityImbalance", [&]{ return detectLiquidityImbalance(m); }));
            results.emplace_back(runDirect("detectTrendContinuation", [&]{ return detectTrendContinuation(m); }));
            results.emplace_back(runDirect("detectTrendReversal", [&]{ return detectTrendReversal(m); }));
            // Add other detectors as needed

            // Commit shared state under single lock
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto &r : results) {
                    // preserve boolean detector state but we'll drive emission via Bayesian model
                    imbalanceState_[r.type] = r.shouldEmitEvent;
                }
                lastMetrics_ = m;
            }

            // Build feature vector from detectors (use imbalanceValue / strength mapping)
            std::vector<double> features;
            features.reserve(results.size());
            for (const auto &r : results) {
                double v = 0.0;
                if (std::isfinite(r.strength) && r.strength > 0.0) {
                    v = std::clamp(r.strength, 0.0, 1.0);
                } else if (std::isfinite(r.imbalanceValue)) {
                    // map raw ratio-ish values into [0,1] using strength calc with default threshold
                    v = calculateImbalanceStrength(r.imbalanceValue, 1.0);
                }
                features.push_back(v);
            }

            // Buffer features for later labeling/training
            {
                std::lock_guard<std::mutex> pl(pendingTrainingMutex_);
                if (pendingTraining_.size() >= maxPendingTraining_) pendingTraining_.pop_front();
                pendingTraining_.push_back(PendingTraining{m.timestamp, features, m.midPrice});
            }

            // Ensure Bayesian model exists and attempt to load if path provided (use adapter handle)
            if (!bayes_handle_) {
                bayes_handle_ = trading::perception::bayes_create();
                if (bayesModelPath_.empty()) {
                    const char* p = std::getenv("BAYES_MODEL_PATH");
                    if (p) bayesModelPath_ = std::string(p);
                }
                if (!bayesModelPath_.empty()) {
                    try { trading::perception::bayes_load(bayes_handle_, bayesModelPath_); } catch(...){ }
                }
            }

            // Predict with Bayesian model and emit a single aggregated event if signal indicates trade
            if (bayes_handle_) {
                try {
                    auto signal = trading::perception::bayes_predict(bayes_handle_, features);
                    if (signal.should_trade) {
                        Direction dir = (signal.direction > 0) ? Direction::BUY : Direction::SELL;
                        OrderBookMetrics mcopy = m;
                        mcopy.metrics["bayes_probability"] = signal.probability;
                        // Use model probability as the event strength (0..1)
                        createImbalanceEvent(ImbalanceType::CUSTOM, signal.probability, dir, mcopy);
                    }
                } catch (const std::exception &ex) {
                    LOG_WARN("Bayes predict threw: {}", ex.what());
                } catch (...) {
                    LOG_WARN("Bayes predict threw unknown");
                }
            }

            // Process pending training samples: label them based on future midPrice movement
            try {
                std::vector<std::pair<std::vector<double>, int>> toTrain;
                {
                    std::lock_guard<std::mutex> pl(pendingTrainingMutex_);
                    auto now = m.timestamp;
                    while (!pendingTraining_.empty()) {
                        const auto &pt = pendingTraining_.front();
                        // if sample is older than labelingDelay_
                        if (now - pt.timestamp >= labelingDelay_) {
                            // determine label by comparing current midPrice and stored midPrice
                            double priceNow = m.midPrice;
                            double priceThen = pt.midPrice;
                            int label = (priceNow > priceThen) ? 1 : 0;
                            toTrain.emplace_back(pt.features, label);
                            pendingTraining_.pop_front();
                        } else break;
                    }
                }
                // update model outside lock
                for (auto &pair : toTrain) {
                    try {
                        trading::perception::bayes_update(bayes_handle_, pair.first, pair.second);
                    } catch (const std::exception &ex) {
                        LOG_WARN("Bayes update threw: {}", ex.what());
                    } catch (...) {
                        LOG_WARN("Bayes update threw unknown");
                    }
                }
            } catch (...) {
                // swallow
            }
        }
    });
}


 // Initialize the detection engine with configuration
 bool ImbalanceDetectionEngine::initialize(const ImbalanceDetectionConfig& config) {
    config_ = config;
     
     // Initialize thresholds with defaults if not specified
     if (thresholds_.empty()) {
         // Set default thresholds if none provided
         thresholds_.setThreshold("volumeImbalance ", 2.0); // Volume imbalance threshold
         thresholds_.setThreshold("emaVolumeImbalance", 1.5); // EMA volume imbalance threshold
         thresholds_.setThreshold("priceLevelImbalance", 1.5); // Price level imbalance threshold
         thresholds_.setThreshold("liquidity_flow_imbalance", 3.0); // Liquidity flow imbalance threshold
         thresholds_.setThreshold("liquidity_removal", 1.5); // Liquidity removal threshold
         thresholds_.setThreshold("priceLevelImbalance", 1.5); // Price level imbalance threshold
         thresholds_.setThreshold("abnormal_spread", 2.5); // Abnormal spread threshold
        thresholds_.setThreshold("liquidity_flow_imbalance", 3.0); // Liquidity flow imbalance threshold
     }
     
     // Clear any existing state
     clearImbalances();

    // Configure bayes model path from config or environment
    if (!config_.bayesModelPath.empty()) bayesModelPath_ = config_.bayesModelPath;
    if (bayesModelPath_.empty()) {
        const char* p = std::getenv("BAYES_MODEL_PATH");
        if (p) bayesModelPath_ = std::string(p);
    }

    // Optional labeling delay from config (in ms)
    if (config_.labelingDelayMs > 0) labelingDelay_ = std::chrono::milliseconds(config_.labelingDelayMs);

    // Create and load Bayesian model adapter handle upfront so load errors surface at init
    try {
        if (!bayes_handle_) {
            bayes_handle_ = trading::perception::bayes_create();
            LOG_DEBUG("ImbalanceDetectionEngine: created bayes adapter handle");
        }
        if (bayes_handle_ && !bayesModelPath_.empty()) {
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
    } catch (const std::exception &ex) {
        LOG_WARN("ImbalanceDetectionEngine: failed to create bayes adapter: {}", ex.what());
    } catch (...) {
        LOG_WARN("ImbalanceDetectionEngine: unknown error creating bayes adapter");
    }

    // Start background detector worker so analyzeMetrics can offload work.
    // Start after releasing any locks to avoid lock ordering issues.
    // try {
    //     startDetectorWorker(); //already have metric_analyzer u idiot this is dupplcate. should call everytime we receive new ob metrics
    // } catch (...) {
    //     // ignore
    // }
     
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
                LOG_DEBUG("ImbalanceDetectionEngine: enqueued metrics for {}:{} (queue size={})", metrics.exchangeId, metrics.symbol, detectorQueue_.size());
                return false; // enqueued for async processing
            } else {
                LOG_WARN("ImbalanceDetectionEngine: detector queue full (%zu), falling back to synchronous run", detectorQueue_.size());
            }
        }
    }

    // If detector worker is running, try to enqueue the metrics and return quickly.
    // {
    //     std::lock_guard<std::mutex> ql(mutex_);
    //     // Note: use the public detectorQueue_ only if worker was initialized elsewhere
    // }

    // Run detectors and collect results (detectors must NOT mutate shared state or call createImbalanceEvent)
    // std::vector<DetectorResult> results;
    // const auto DETECTOR_TIMEOUT = std::chrono::milliseconds(50);

    // Wrap each detector so a blocking detector cannot stall the ZMQ receiver thread
    // results.emplace_back(runDetectorWithTimeout("detectVolumeImbalance", [&]{ return detectVolumeImbalance(metrics); }, DETECTOR_TIMEOUT));
    // results.emplace_back(runDetectorWithTimeout("detectPriceLevelImbalance", [&]{ return detectPriceLevelImbalance(metrics); }, DETECTOR_TIMEOUT));
    // results.emplace_back(runDetectorWithTimeout("detectLiquidityFlowImbalance", [&]{ return detectLiquidityFlowImbalance(metrics); }, DETECTOR_TIMEOUT));
    // results.emplace_back(runDetectorWithTimeout("detectAbnormalSpread", [&]{ return detectAbnormalSpread(metrics); }, DETECTOR_TIMEOUT));
    // results.emplace_back(runDetectorWithTimeout("detectMarketDepthChange", [&]{ return detectMarketDepthChange(metrics); }, DETECTOR_TIMEOUT));
    // detectVolumeImbalance(metrics);
    // detectPriceLevelImbalance(metrics);
    // detectLiquidityFlowImbalance(metrics);
    // detectAbnormalSpread(metrics);
    // detectMarketDepthChange(metrics);

    // Commit shared state under single lock
    // {
    //     std::lock_guard<std::mutex> lock(mutex_);
    //     for (const auto &r : results) {
    //         imbalanceState_[r.type] = r.shouldEmitEvent;
    //     }
    //     lastMetrics_ = metrics;
    // }

    //create event for each detector is a terrible idea, such a waste of resource
    bool any = false;
    // for (const auto &r : results) {
    //     if (r.isImbalance) any = true;
    //     if (r.shouldEmitEvent) {
    //         createImbalanceEvent(r.type, r.strength, r.direction, metrics);
    //     }
    // }
    return any;
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
    
    // Update global sensitivity if provided
    if (params.numeric_params.find("global_sensitivity") != params.numeric_params.end()) {
        thresholds_.global_sensitivity = params.getDouble("global_sensitivity", 1.0);
    }
}
 // PRIVATE METHODS

double ImbalanceDetectionEngine::calculateImbalanceStrength(double volumeRatio, double threshold) {
    double strength;
    if (volumeRatio > threshold) {
        strength = (volumeRatio - threshold) / threshold;
    } else if (volumeRatio < 1.0 / threshold) {
        strength = (1.0/threshold - volumeRatio) / (1.0/threshold);
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

// DetectorResult ImbalanceDetectionEngine::detectMarketDepthChange(const OrderBookMetrics& metrics) {
//     // Copy previous snapshot under lock - detectors run concurrently
//     std::optional<OrderBookMetrics> prevOpt;
//     {
//         std::lock_guard<std::mutex> lock(mutex_);
//         prevOpt = lastMetrics_;
//     }
//     if (!prevOpt) return DetectorResult{};
//     const auto& prev = *prevOpt;
//     bool depthChanged = false;

//     // --- 1. Spread widening/narrowing ---
//     double spreadDiff = std::abs(metrics.spread - prev.spread);
//     if (spreadDiff > config_.spreadChangeThreshold) {
//         depthChanged = true;
//     }

//     // --- 2. Volume imbalance shift ---
//     double imbalanceDiff = std::abs(metrics.volumeImbalance - prev.volumeImbalance);
//     if (imbalanceDiff > config_.volumeImbalanceThreshold) {
//         depthChanged = true;
//     }

//     // --- 3. Depth distribution change ---
//     auto checkVectorChange = [&](const std::vector<double>& cur,
//                                  const std::vector<double>& old,
//                                  double threshold) -> bool {
//         size_t n = std::min(cur.size(), old.size());
//         for (size_t i = 0; i < n; i++) {
//             if (std::abs(cur[i] - old[i]) > threshold) {
//                 return true;
//             }
//         }
//         return false;
//     };

//     if (checkVectorChange(metrics.bidVolumesByLevel, prev.bidVolumesByLevel, config_.levelVolumeChangeThreshold) ||
//         checkVectorChange(metrics.askVolumesByLevel, prev.askVolumesByLevel, config_.levelVolumeChangeThreshold)) {
//         depthChanged = true;
//     }

//     // --- 4. Liquidity pressure or density shifts ---
//     double pressureDiff = std::abs(metrics.liquidityPressure - prev.liquidityPressure);
//     if (pressureDiff > config_.liquidityPressureThreshold) {
//         depthChanged = true;
//     }
//     double densityDiff = std::abs(metrics.bidDensity - prev.bidDensity) + std::abs(metrics.askDensity - prev.askDensity);
//     if (densityDiff > config_.densityChangeThreshold) {
//         depthChanged = true;
//     }
//     // If any significant change detected, create an event
//     DetectorResult r;
//     r.type = ImbalanceType::MARKET_DEPTH_CHANGE;
//     if (depthChanged) {
//         r.isImbalance = true;
//         r.strength = 1.0; // Could be refined to a computed value
//         r.direction = Direction::NEUTRAL; // Depth change is neutral
//         r.shouldEmitEvent = false; // Currently not emitting events for depth changes
//     }
//     return r;
// }

// double ImbalanceDetectionEngine::detectLargeOrderAppearance(const OrderBookMetrics& metrics) {
//     // Configuration thresholds - these should be configurable parameters
//     static const double VOLUME_IMBALANCE_THRESHOLD = 0.3;        // 30% imbalance
//     static const double WEIGHTED_IMBALANCE_THRESHOLD = 0.25;     // 25% weighted imbalance
//     static const double DEPTH_IMBALANCE_THRESHOLD = 0.4;         // 40% depth imbalance
//     static const double PRESSURE_IMBALANCE_THRESHOLD = 0.35;     // 35% pressure imbalance
//     static const double BID_ASK_RATIO_THRESHOLD_HIGH = 2.0;      // 2:1 ratio or higher
//     static const double BID_ASK_RATIO_THRESHOLD_LOW = 0.5;       // 1:2 ratio or lower
//     static const double DENSITY_RATIO_THRESHOLD = 1.5;          // 50% difference in density
//     static const double MARKET_DIRECTIONAL_THRESHOLD = 0.6;     // Strong directional signal
//     static const double LIQUIDITY_PRESSURE_THRESHOLD = 0.4;     // High liquidity pressure
    
//     // Counter for detection signals
//     int detectionSignals = 0;
//     const int REQUIRED_SIGNALS = 3;  // Need at least 3 signals for confirmation
    
//     // 1. Check volume imbalance - indicates large orders affecting volume distribution
//     double absVolumeImbalance = std::abs(metrics.volumeImbalance);
//     if (absVolumeImbalance > VOLUME_IMBALANCE_THRESHOLD) {
//         detectionSignals++;
//     }
    
//     // 2. Check weighted volume imbalance - price-weighted indicator is more sensitive
//     double absWeightedImbalance = std::abs(metrics.weightedVolumeImbalance);
//     if (absWeightedImbalance > WEIGHTED_IMBALANCE_THRESHOLD) {
//         detectionSignals++;
//     }
    
//     // 3. Check depth imbalance - large orders create depth asymmetries
//     double absDepthImbalance = std::abs(metrics.depthImbalance);
//     if (absDepthImbalance > DEPTH_IMBALANCE_THRESHOLD) {
//         detectionSignals++;
//     }
    
//     // 4. Check pressure imbalance - combined pressure indicator
//     double absPressureImbalance = std::abs(metrics.pressureImbalance);
//     if (absPressureImbalance > PRESSURE_IMBALANCE_THRESHOLD) {
//         detectionSignals++;
//     }
    
//     // 5. Check bid-ask volume ratio for extreme values
//     if (metrics.bidAskVolumeRatio > BID_ASK_RATIO_THRESHOLD_HIGH || 
//         metrics.bidAskVolumeRatio < BID_ASK_RATIO_THRESHOLD_LOW) {
//         detectionSignals++;
//     }
    
//     // 6. Check density imbalance - large orders create density asymmetries
//     if (metrics.bidDensity > 0 && metrics.askDensity > 0) {
//         double densityRatio = std::max(metrics.bidDensity / metrics.askDensity, 
//                                      metrics.askDensity / metrics.bidDensity);
//         if (densityRatio > DENSITY_RATIO_THRESHOLD) {
//             detectionSignals++;
//         }
//     }
    
//     // 7. Check market directional coefficient - strong directional bias
//     double absMarketDirectional = std::abs(metrics.marketDirectionalCoefficient);
//     if (absMarketDirectional > MARKET_DIRECTIONAL_THRESHOLD) {
//         detectionSignals++;
//     }
    
//     // 8. Check liquidity pressure - high pressure indicates large order impact
//     double absLiquidityPressure = std::abs(metrics.liquidityPressure);
//     if (absLiquidityPressure > LIQUIDITY_PRESSURE_THRESHOLD) {
//         detectionSignals++;
//     }
    
//     // 9. Check for significant level-by-level volume concentration
//     bool hasConcentratedVolume = checkVolumeConcentration(metrics);
//     if (hasConcentratedVolume) {
//         detectionSignals++;
//     }
    
//     // 10. Additional check using custom metrics if available
//     double bidFlow = metrics.getMetric("bidFlow", 0.0);
//     double askFlow = metrics.getMetric("askFlow", 0.0);
//     if (std::abs(bidFlow - askFlow) > 0.5) {  // Significant flow imbalance
//         detectionSignals++;
//     }
    
//     // Return true if we have enough confirming signals
//     return detectionSignals >= REQUIRED_SIGNALS;
// }

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


DetectorResult ImbalanceDetectionEngine::detectOrderFlowImbalance(const OrderBookMetrics& metrics){
    return DetectorResult{};
}
DetectorResult ImbalanceDetectionEngine::detectPriceLevelConcentration(const OrderBookMetrics& metrics){
    return DetectorResult{};
}
DetectorResult ImbalanceDetectionEngine::detectLiquidityImbalance(const OrderBookMetrics& metrics){
    return DetectorResult{};
}
DetectorResult ImbalanceDetectionEngine::detectTrendContinuation(const OrderBookMetrics& metrics){
    return DetectorResult{};
}
DetectorResult ImbalanceDetectionEngine::detectTrendReversal(const OrderBookMetrics& metrics){
    return DetectorResult{};
}
DetectorResult ImbalanceDetectionEngine::detectTradeImbalance(const OrderBookMetrics& metrics)   {
    return DetectorResult{};
}
DetectorResult ImbalanceDetectionEngine::detectCancellationImbalance(const OrderBookMetrics& metrics){
    return DetectorResult{};
}
DetectorResult ImbalanceDetectionEngine::detectPriceVolatilityImbalance(const OrderBookMetrics& metrics){
    return DetectorResult{};
}

DetectorResult ImbalanceDetectionEngine::detectCustomImbalance(const OrderBookMetrics& metrics){
    return DetectorResult{};
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
         default:
             break;
     }
     
     return strength;
 }
 
 //this function need to be rewrite carefully and follow below logic
 /**

function determineDirection(metrics):

    scoreBuy = 0
    scoreSell = 0

    # --- Volume imbalance ---
    if has(bidVolume, askVolume):
        ratio = (bidVolume - askVolume) / (bidVolume + askVolume)
        if ratio > +0.2: scoreBuy += 1
        if ratio < -0.2: scoreSell += 1

    # --- Price level imbalance ---
    if has(bidLevels, askLevels):
        ratio = (bidLevels - askLevels) / (bidLevels + askLevels)
        if ratio > +0.3: scoreBuy += 1
        if ratio < -0.3: scoreSell += 1

    # --- Liquidity removal ---
    if has(liquidityRemovalSide):
        if liquidityRemovalSide == "bid": scoreSell += 2
        if liquidityRemovalSide == "ask": scoreBuy += 2

    # --- Order flow imbalance (submissions) ---
    if has(bidOrdersSubmitted, askOrdersSubmitted):
        ratio = (bidOrdersSubmitted - askOrdersSubmitted) / (total)
        if ratio > +0.2: scoreBuy += 1
        if ratio < -0.2: scoreSell += 1

    # --- Large order appearance/disappearance ---
    if has(largeBidOrderAppeared): scoreBuy += 1
    if has(largeAskOrderAppeared): scoreSell += 1
    if has(largeBidOrderRemoved):  scoreSell += 1
    if has(largeAskOrderRemoved):  scoreBuy += 1

    # --- Price level concentration ---
    if has(concentratedBidLevels): scoreBuy += 1
    if has(concentratedAskLevels): scoreSell += 1

    # --- Spread changes ---
    if has(spreadChange):
        if spreadChange < 0: scoreBuy += 1   # tighter spread bullish
        if spreadChange > 0: scoreSell += 1  # wider spread bearish

    if has(spread) and spread > ABNORMAL_THRESHOLD:
        if bidVolume > askVolume * 1.5: scoreBuy += 1
        else: scoreSell += 1

    # --- Market depth change ---
    if has(bidDepthChange, askDepthChange):
        diff = bidDepthChange - askDepthChange
        if diff > +THRESH: scoreBuy += 1
        if diff < -THRESH: scoreSell += 1

    # --- Liquidity flow imbalance ---
    if has(bidFlow, askFlow):
        ratio = (bidFlow - askFlow) / (bidFlow + askFlow)
        if ratio > +0.1: scoreBuy += 2
        if ratio < -0.1: scoreSell += 2

    # --- Trend signals ---
    if has(trendStrength):
        if trendStrength > TREND_THRESHOLD: scoreBuy += 1
        if trendStrength < -TREND_THRESHOLD: scoreSell += 1

    if has(prevTrendStrength, trendStrength):
        if prevTrendStrength > 0 and trendStrength < -TREND_THRESHOLD:
            scoreSell += 2
        if prevTrendStrength < 0 and trendStrength > +TREND_THRESHOLD:
            scoreBuy += 2

    # --- Trade imbalance (executed trades) ---
    if has(marketBuyVolume, marketSellVolume):
        ratio = (marketBuyVolume - marketSellVolume) / (total)
        if ratio > +0.2: scoreBuy += 2
        if ratio < -0.2: scoreSell += 2

    # --- Cancellation imbalance ---
    if has(bidCancellations, askCancellations):
        ratio = (bidCancels - askCancels) / (total)
        if ratio > +0.2: scoreSell += 1    # buyers pulling liquidity
        if ratio < -0.2: scoreBuy += 1     # sellers pulling liquidity

    # --- Price volatility imbalance ---
    if has(volatility):
        if volatility > VOLATILITY_THRESHOLD:
            if lastMoveUp: scoreBuy += 1
            else: scoreSell += 1

    # --- Hidden liquidity imbalance (icebergs) ---
    if has(icebergOnBid): scoreBuy += 2
    if has(icebergOnAsk): scoreSell += 2

    # --- Custom ---
    if has(customSignal):
        if customSignal > 0: scoreBuy += 1
        if customSignal < 0: scoreSell += 1

    # --- Final decision ---
    if scoreBuy > scoreSell and scoreBuy >= MIN_CONFIDENCE:
        return BUY
    if scoreSell > scoreBuy and scoreSell >= MIN_CONFIDENCE:
        return SELL
    return NEUTRAL

  * 
  */

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
    
    // // Log the event emission for diagnostics
    LOG_INFO("ImbalanceDetectionEngine: created ImbalanceEvent id={} type={} strength={} dir={} for {}:{} on thread {}",
             event.eventId,
             static_cast<int>(type),
             event.strength,
             static_cast<int>(direction),
             event.exchangeId.toString(),
             event.symbol.toString(),
             std::this_thread::get_id());

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