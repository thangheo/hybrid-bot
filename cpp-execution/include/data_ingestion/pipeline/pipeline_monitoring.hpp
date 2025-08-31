/**
 * @file pipeline_monitoring.hpp
 * @brief Pipeline monitoring, metrics, and status tracking
 * 
 * Provides comprehensive monitoring capabilities for the normalization pipeline
 * including performance metrics, error tracking, and health status.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
// thread
#include <thread>
namespace trading {
namespace data_ingestion {

/**
 * Pipeline performance and operational metrics
 */
class PipelineMetrics {
public:
    PipelineMetrics() = default;
    
    // Input/Output counters
    void incrementInputCount(const std::string& stageName = "");
    void incrementOutputCount(const std::string& stageName = "");
    void incrementErrorCount(const std::string& stageName = "");
    void incrementDroppedCount(const std::string& stageName = "");
    
    // Latency tracking
    void recordLatency(const std::string& stageName, std::chrono::microseconds latency);
    void recordEndToEndLatency(std::chrono::microseconds latency);
    
    // Queue monitoring
    void updateQueueSize(const std::string& queueName, size_t size);
    void recordQueueFullEvent(const std::string& queueName);
    
    // Getters for total metrics
    uint64_t getTotalInputCount() const;
    uint64_t getTotalOutputCount() const;
    uint64_t getTotalErrorCount() const;
    uint64_t getTotalDroppedCount() const;
    
    // Stage-specific metrics
    uint64_t getInputCount(const std::string& stageName) const;
    uint64_t getOutputCount(const std::string& stageName) const;
    uint64_t getErrorCount(const std::string& stageName) const;
    uint64_t getDroppedCount(const std::string& stageName) const;
    
    // Latency metrics
    double getAverageLatency(const std::string& stageName) const;
    double getAverageEndToEndLatency() const;
    std::chrono::microseconds getMaxLatency(const std::string& stageName) const;
    std::chrono::microseconds getMinLatency(const std::string& stageName) const;
    
    // Queue metrics
    size_t getCurrentQueueSize(const std::string& queueName) const;
    size_t getMaxQueueSize(const std::string& queueName) const;
    uint64_t getQueueFullCount(const std::string& queueName) const;
    
    // Throughput calculations
    double getThroughputPerSecond() const;
    double getStageThroughputPerSecond(const std::string& stageName) const;
    
    // Error rates
    double getErrorRate() const;
    double getStageErrorRate(const std::string& stageName) const;
    
    // Utility methods
    std::vector<std::string> getStageNames() const;
    std::vector<std::string> getQueueNames() const;
    void reset();
    
    // Time window for rate calculations
    void setTimeWindow(std::chrono::seconds window) { timeWindow_ = window; }
    
private:
    struct StageMetrics {
        std::atomic<uint64_t> inputCount{0};
        std::atomic<uint64_t> outputCount{0};
        std::atomic<uint64_t> errorCount{0};
        std::atomic<uint64_t> droppedCount{0};
        
        // Latency tracking
        mutable std::mutex latencyMutex;
        std::vector<std::chrono::microseconds> latencies;
        std::chrono::microseconds totalLatency{0};
        std::chrono::microseconds maxLatency{0};
        std::chrono::microseconds minLatency{std::chrono::microseconds::max()};
        uint64_t latencyCount{0};
        
        void addLatency(std::chrono::microseconds latency) {
            std::lock_guard<std::mutex> lock(latencyMutex);
            totalLatency += latency;
            latencyCount++;
            maxLatency = std::max(maxLatency, latency);
            minLatency = std::min(minLatency, latency);
            
            // Keep recent latencies for percentile calculations
            latencies.push_back(latency);
            if (latencies.size() > 1000) { // Keep last 1000 samples
                latencies.erase(latencies.begin());
            }
        }
        
        double getAverageLatency() const {
            std::lock_guard<std::mutex> lock(latencyMutex);
            if (latencyCount == 0) return 0.0;
            return static_cast<double>(totalLatency.count()) / latencyCount;
        }
    };
    
    struct QueueMetrics {
        std::atomic<size_t> currentSize{0};
        std::atomic<size_t> maxSize{0};
        std::atomic<uint64_t> fullCount{0};
        
        void updateSize(size_t size) {
            currentSize.store(size);
            size_t current_max = maxSize.load();
            while (size > current_max && 
                   !maxSize.compare_exchange_weak(current_max, size)) {
                // Retry until successful or size is no longer max
            }
        }
    };
    
    mutable std::mutex metricsMutex_;
    std::unordered_map<std::string, StageMetrics> stageMetrics_;
    std::unordered_map<std::string, QueueMetrics> queueMetrics_;
    
    // End-to-end latency tracking
    mutable std::mutex endToEndMutex_;
    std::chrono::microseconds totalEndToEndLatency_{0};
    uint64_t endToEndCount_{0};
    
    // Time tracking for throughput calculations
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::seconds timeWindow_{60}; // Default 1 minute window
    
    StageMetrics& getOrCreateStageMetrics(const std::string& stageName);
    QueueMetrics& getOrCreateQueueMetrics(const std::string& queueName);
};

/**
 * Pipeline status and health information
 */
class PipelineStatus {
public:
    enum class StageState {
        STOPPED,
        STARTING,
        RUNNING,
        STOPPING,
        ERROR,
        UNKNOWN
    };
    
    enum class HealthState {
        HEALTHY,
        WARNING,
        CRITICAL,
        UNKNOWN
    };
    
    PipelineStatus() : overallState_(StageState::STOPPED), healthState_(HealthState::UNKNOWN) {}
    
    // Overall pipeline state
    void setOverallState(StageState state);
    StageState getOverallState() const;
    bool isRunning() const;
    bool isStopped() const;
    
    // Stage-specific state
    void setStageState(const std::string& stageName, StageState state);
    StageState getStageState(const std::string& stageName) const;
    std::unordered_map<std::string, StageState> getStageStates() const;
    
    // Health monitoring
    void setHealthState(HealthState health);
    HealthState getHealthState() const;
    bool isHealthy() const;
    
    // Error tracking
    void setLastError(const std::string& error);
    const std::string& getLastError() const;
    void addError(const std::string& stageName, const std::string& error);
    std::vector<std::string> getRecentErrors(size_t maxErrors = 10) const;
    
    // Warning tracking
    void addWarning(const std::string& stageName, const std::string& warning);
    std::vector<std::string> getRecentWarnings(size_t maxWarnings = 10) const;
    
    // Configuration status
    void setConfigured(bool configured);
    bool isConfigured() const;
    
    // Startup time tracking
    void setStartTime(std::chrono::steady_clock::time_point startTime);
    std::chrono::steady_clock::duration getUptime() const;
    
    // Stage health assessment
    bool isStageHealthy(const std::string& stageName) const;
    std::vector<std::string> getUnhealthyStages() const;
    
    // Utility methods
    std::string getStatusSummary() const;
    std::vector<std::string> getStageNames() const;
    
private:
    mutable std::mutex statusMutex_;
    
    std::atomic<StageState> overallState_;
    std::atomic<HealthState> healthState_;
    std::atomic<bool> configured_{false};
    
    std::unordered_map<std::string, StageState> stageStates_;
    
    std::string lastError_;
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::string>> errorHistory_;
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::string>> warningHistory_;
    
    std::chrono::steady_clock::time_point startTime_;
    
    static std::string stateToString(StageState state);
    static std::string healthToString(HealthState health);
};

/**
 * Pipeline configuration parameters
 */
struct PipelineConfig {
    size_t maxQueueSize = 10000;
    size_t threadPoolSize = 4;
    bool enableMetrics = true;
    std::chrono::milliseconds metricsInterval{5000};
    bool enableDetailedLogging = false;
    bool enableHealthChecks = true;
    std::chrono::seconds healthCheckInterval{10};
    
    // Queue configuration
    bool enableQueueBackpressure = true;
    std::chrono::milliseconds queueTimeoutMs{1000};
    
    // Error handling
    size_t maxRetryAttempts = 3;
    std::chrono::milliseconds retryDelayMs{100};
    bool stopOnCriticalError = false;
    
    // Performance tuning
    bool enableBatching = false;
    size_t batchSize = 100;
    std::chrono::milliseconds batchTimeoutMs{10};
    
    bool isValid() const {
        return maxQueueSize > 0 && 
               threadPoolSize > 0 && 
               metricsInterval.count() > 0 &&
               healthCheckInterval.count() > 0 &&
               maxRetryAttempts > 0;
    }
};

/**
 * Enhanced pipeline with monitoring capabilities
 */
class MonitoredPipeline {
public:
    MonitoredPipeline() : metrics_(std::make_shared<PipelineMetrics>()),
                         status_(std::make_shared<PipelineStatus>()) {}
    
    virtual ~MonitoredPipeline() = default;
    
    // Configuration
    virtual bool configure(const PipelineConfig& config);
    virtual const PipelineConfig& getConfig() const { return config_; }
    
    // Lifecycle
    virtual bool start();
    virtual void stop();
    virtual bool restart();
    
    // Monitoring interface
    std::shared_ptr<PipelineMetrics> getMetrics() const { return metrics_; }
    std::shared_ptr<PipelineStatus> getStatus() const { return status_; }
    
    // Health checks
    bool performHealthCheck();
    void enablePeriodicHealthCheck(bool enable);
    
protected:
    PipelineConfig config_;
    std::shared_ptr<PipelineMetrics> metrics_;
    std::shared_ptr<PipelineStatus> status_;
    
    // Monitoring thread
    std::unique_ptr<std::thread> monitoringThread_;
    std::atomic<bool> monitoringEnabled_{false};
    
    // Subclass hooks
    virtual bool doStart() = 0;
    virtual void doStop() = 0;
    virtual bool doHealthCheck() = 0;
    
private:
    void monitoringLoop();
    void updateHealthState();
};

// Inline implementations for simple getters
inline uint64_t PipelineMetrics::getTotalInputCount() const {
    uint64_t total = 0;
    std::lock_guard<std::mutex> lock(metricsMutex_);
    for (const auto& [name, metrics] : stageMetrics_) {
        total += metrics.inputCount.load();
    }
    return total;
}

inline uint64_t PipelineMetrics::getTotalOutputCount() const {
    uint64_t total = 0;
    std::lock_guard<std::mutex> lock(metricsMutex_);
    for (const auto& [name, metrics] : stageMetrics_) {
        total += metrics.outputCount.load();
    }
    return total;
}

inline uint64_t PipelineMetrics::getTotalErrorCount() const {
    uint64_t total = 0;
    std::lock_guard<std::mutex> lock(metricsMutex_);
    for (const auto& [name, metrics] : stageMetrics_) {
        total += metrics.errorCount.load();
    }
    return total;
}

inline bool PipelineStatus::isRunning() const {
    return overallState_.load() == StageState::RUNNING;
}

inline bool PipelineStatus::isHealthy() const {
    return healthState_.load() == HealthState::HEALTHY;
}

inline PipelineStatus::StageState PipelineStatus::getOverallState() const {
    return overallState_.load();
}

inline PipelineStatus::HealthState PipelineStatus::getHealthState() const {
    return healthState_.load();
}

} // namespace data_ingestion
} // namespace trading