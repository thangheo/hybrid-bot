/**
 * @file pipeline_monitoring.cpp
 * @brief Implementation of pipeline monitoring classes
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#include "pipeline/pipeline_monitoring.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>

namespace trading {
namespace data_ingestion {

// =============================================================================
// PipelineMetrics Implementation
// =============================================================================

void PipelineMetrics::incrementInputCount(const std::string& stageName) {
    if (!stageName.empty()) {
        getOrCreateStageMetrics(stageName).inputCount++;
    }
}

void PipelineMetrics::incrementOutputCount(const std::string& stageName) {
    if (!stageName.empty()) {
        getOrCreateStageMetrics(stageName).outputCount++;
    }
}

void PipelineMetrics::incrementErrorCount(const std::string& stageName) {
    if (!stageName.empty()) {
        getOrCreateStageMetrics(stageName).errorCount++;
    }
}

void PipelineMetrics::incrementDroppedCount(const std::string& stageName) {
    if (!stageName.empty()) {
        getOrCreateStageMetrics(stageName).droppedCount++;
    }
}

void PipelineMetrics::recordLatency(const std::string& stageName, std::chrono::microseconds latency) {
    if (!stageName.empty()) {
        getOrCreateStageMetrics(stageName).addLatency(latency);
    }
}

void PipelineMetrics::recordEndToEndLatency(std::chrono::microseconds latency) {
    std::lock_guard<std::mutex> lock(endToEndMutex_);
    totalEndToEndLatency_ += latency;
    endToEndCount_++;
}

void PipelineMetrics::updateQueueSize(const std::string& queueName, size_t size) {
    if (!queueName.empty()) {
        getOrCreateQueueMetrics(queueName).updateSize(size);
    }
}

void PipelineMetrics::recordQueueFullEvent(const std::string& queueName) {
    if (!queueName.empty()) {
        getOrCreateQueueMetrics(queueName).fullCount++;
    }
}

uint64_t PipelineMetrics::getInputCount(const std::string& stageName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = stageMetrics_.find(stageName);
    return (it != stageMetrics_.end()) ? it->second.inputCount.load() : 0;
}

uint64_t PipelineMetrics::getOutputCount(const std::string& stageName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = stageMetrics_.find(stageName);
    return (it != stageMetrics_.end()) ? it->second.outputCount.load() : 0;
}

uint64_t PipelineMetrics::getErrorCount(const std::string& stageName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = stageMetrics_.find(stageName);
    return (it != stageMetrics_.end()) ? it->second.errorCount.load() : 0;
}

uint64_t PipelineMetrics::getDroppedCount(const std::string& stageName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = stageMetrics_.find(stageName);
    return (it != stageMetrics_.end()) ? it->second.droppedCount.load() : 0;
}

double PipelineMetrics::getAverageLatency(const std::string& stageName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = stageMetrics_.find(stageName);
    return (it != stageMetrics_.end()) ? it->second.getAverageLatency() : 0.0;
}

double PipelineMetrics::getAverageEndToEndLatency() const {
    std::lock_guard<std::mutex> lock(endToEndMutex_);
    if (endToEndCount_ == 0) return 0.0;
    return static_cast<double>(totalEndToEndLatency_.count()) / endToEndCount_;
}

std::chrono::microseconds PipelineMetrics::getMaxLatency(const std::string& stageName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = stageMetrics_.find(stageName);
    return (it != stageMetrics_.end()) ? it->second.maxLatency : std::chrono::microseconds{0};
}

std::chrono::microseconds PipelineMetrics::getMinLatency(const std::string& stageName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = stageMetrics_.find(stageName);
    return (it != stageMetrics_.end()) ? it->second.minLatency : std::chrono::microseconds{0};
}

size_t PipelineMetrics::getCurrentQueueSize(const std::string& queueName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = queueMetrics_.find(queueName);
    return (it != queueMetrics_.end()) ? it->second.currentSize.load() : 0;
}

size_t PipelineMetrics::getMaxQueueSize(const std::string& queueName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = queueMetrics_.find(queueName);
    return (it != queueMetrics_.end()) ? it->second.maxSize.load() : 0;
}

uint64_t PipelineMetrics::getQueueFullCount(const std::string& queueName) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    auto it = queueMetrics_.find(queueName);
    return (it != queueMetrics_.end()) ? it->second.fullCount.load() : 0;
}

double PipelineMetrics::getThroughputPerSecond() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);
    if (elapsed.count() == 0) return 0.0;
    return static_cast<double>(getTotalOutputCount()) / elapsed.count();
}

double PipelineMetrics::getStageThroughputPerSecond(const std::string& stageName) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);
    if (elapsed.count() == 0) return 0.0;
    return static_cast<double>(getOutputCount(stageName)) / elapsed.count();
}

double PipelineMetrics::getErrorRate() const {
    uint64_t total = getTotalInputCount();
    if (total == 0) return 0.0;
    return static_cast<double>(getTotalErrorCount()) / total;
}

double PipelineMetrics::getStageErrorRate(const std::string& stageName) const {
    uint64_t input = getInputCount(stageName);
    if (input == 0) return 0.0;
    return static_cast<double>(getErrorCount(stageName)) / input;
}

std::vector<std::string> PipelineMetrics::getStageNames() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    std::vector<std::string> names;
    for (const auto& [name, metrics] : stageMetrics_) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> PipelineMetrics::getQueueNames() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    std::vector<std::string> names;
    for (const auto& [name, metrics] : queueMetrics_) {
        names.push_back(name);
    }
    return names;
}

void PipelineMetrics::reset() {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    stageMetrics_.clear();
    queueMetrics_.clear();
    
    std::lock_guard<std::mutex> endLock(endToEndMutex_);
    totalEndToEndLatency_ = std::chrono::microseconds{0};
    endToEndCount_ = 0;
    
    startTime_ = std::chrono::steady_clock::now();
}

PipelineMetrics::StageMetrics& PipelineMetrics::getOrCreateStageMetrics(const std::string& stageName) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return stageMetrics_[stageName];
}

PipelineMetrics::QueueMetrics& PipelineMetrics::getOrCreateQueueMetrics(const std::string& queueName) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return queueMetrics_[queueName];
}

// =============================================================================
// PipelineStatus Implementation
// =============================================================================

void PipelineStatus::setOverallState(StageState state) {
    overallState_.store(state);
}

void PipelineStatus::setStageState(const std::string& stageName, StageState state) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    stageStates_[stageName] = state;
    
    // Update overall state based on stage states
    bool allRunning = true;
    bool anyError = false;
    
    for (const auto& [name, stageState] : stageStates_) {
        if (stageState != StageState::RUNNING) {
            allRunning = false;
        }
        if (stageState == StageState::ERROR) {
            anyError = true;
        }
    }
    
    if (anyError) {
        setOverallState(StageState::ERROR);
        setHealthState(HealthState::CRITICAL);
    } else if (allRunning && !stageStates_.empty()) {
        setOverallState(StageState::RUNNING);
        setHealthState(HealthState::HEALTHY);
    }
}

PipelineStatus::StageState PipelineStatus::getStageState(const std::string& stageName) const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    auto it = stageStates_.find(stageName);
    return (it != stageStates_.end()) ? it->second : StageState::UNKNOWN;
}

std::unordered_map<std::string, PipelineStatus::StageState> PipelineStatus::getStageStates() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return stageStates_;
}

void PipelineStatus::setHealthState(HealthState health) {
    healthState_.store(health);
}

void PipelineStatus::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    lastError_ = error;
    errorHistory_.emplace_back(std::chrono::steady_clock::now(), error);
    
    // Keep only recent errors
    if (errorHistory_.size() > 100) {
        errorHistory_.erase(errorHistory_.begin());
    }
}

const std::string& PipelineStatus::getLastError() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return lastError_;
}

void PipelineStatus::addError(const std::string& stageName, const std::string& error) {
    std::string fullError = stageName + ": " + error;
    setLastError(fullError);
    setStageState(stageName, StageState::ERROR);
}

void PipelineStatus::addWarning(const std::string& stageName, const std::string& warning) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    std::string fullWarning = stageName + ": " + warning;
    warningHistory_.emplace_back(std::chrono::steady_clock::now(), fullWarning);
    
    // Keep only recent warnings
    if (warningHistory_.size() > 100) {
        warningHistory_.erase(warningHistory_.begin());
    }
}

std::vector<std::string> PipelineStatus::getRecentErrors(size_t maxErrors) const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    std::vector<std::string> errors;
    
    size_t start = errorHistory_.size() > maxErrors ? errorHistory_.size() - maxErrors : 0;
    for (size_t i = start; i < errorHistory_.size(); ++i) {
        errors.push_back(errorHistory_[i].second);
    }
    
    return errors;
}

std::vector<std::string> PipelineStatus::getRecentWarnings(size_t maxWarnings) const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    std::vector<std::string> warnings;
    
    size_t start = warningHistory_.size() > maxWarnings ? warningHistory_.size() - maxWarnings : 0;
    for (size_t i = start; i < warningHistory_.size(); ++i) {
        warnings.push_back(warningHistory_[i].second);
    }
    
    return warnings;
}

void PipelineStatus::setConfigured(bool configured) {
    configured_.store(configured);
}

bool PipelineStatus::isConfigured() const {
    return configured_.load();
}

bool PipelineStatus::isStopped() const {
    return overallState_.load() == StageState::STOPPED;
}

void PipelineStatus::setStartTime(std::chrono::steady_clock::time_point startTime) {
    startTime_ = startTime;
}

std::chrono::steady_clock::duration PipelineStatus::getUptime() const {
    return std::chrono::steady_clock::now() - startTime_;
}

bool PipelineStatus::isStageHealthy(const std::string& stageName) const {
    auto state = getStageState(stageName);
    return state == StageState::RUNNING || state == StageState::STOPPED;
}

std::vector<std::string> PipelineStatus::getUnhealthyStages() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    std::vector<std::string> unhealthy;
    
    for (const auto& [name, state] : stageStates_) {
        if (state == StageState::ERROR) {
            unhealthy.push_back(name);
        }
    }
    
    return unhealthy;
}

std::string PipelineStatus::getStatusSummary() const {
    std::ostringstream oss;
    oss << "Pipeline Status: " << stateToString(getOverallState()) 
        << ", Health: " << healthToString(getHealthState());
    
    auto unhealthy = getUnhealthyStages();
    if (!unhealthy.empty()) {
        oss << ", Unhealthy stages: ";
        for (size_t i = 0; i < unhealthy.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << unhealthy[i];
        }
    }
    
    return oss.str();
}

std::vector<std::string> PipelineStatus::getStageNames() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    std::vector<std::string> names;
    for (const auto& [name, state] : stageStates_) {
        names.push_back(name);
    }
    return names;
}

std::string PipelineStatus::stateToString(StageState state) {
    switch (state) {
        case StageState::STOPPED:  return "STOPPED";
        case StageState::STARTING: return "STARTING";
        case StageState::RUNNING:  return "RUNNING";
        case StageState::STOPPING: return "STOPPING";
        case StageState::ERROR:    return "ERROR";
        case StageState::UNKNOWN:  return "UNKNOWN";
        default:                   return "INVALID";
    }
}

std::string PipelineStatus::healthToString(HealthState health) {
    switch (health) {
        case HealthState::HEALTHY:  return "HEALTHY";
        case HealthState::WARNING:  return "WARNING";
        case HealthState::CRITICAL: return "CRITICAL";
        case HealthState::UNKNOWN:  return "UNKNOWN";
        default:                    return "INVALID";
    }
}

// =============================================================================
// MonitoredPipeline Implementation
// =============================================================================

bool MonitoredPipeline::configure(const PipelineConfig& config) {
    if (!config.isValid()) {
        status_->setLastError("Invalid pipeline configuration");
        return false;
    }
    
    config_ = config;
    status_->setConfigured(true);
    
    if (config_.enableMetrics) {
        metrics_->setTimeWindow(std::chrono::seconds(60)); // Default 1 minute window
    }
    
    return true;
}

bool MonitoredPipeline::start() {
    if (status_->isRunning()) {
        return true; // Already running
    }
    
    if (!status_->isConfigured()) {
        status_->setLastError("Pipeline not configured");
        return false;
    }
    
    status_->setOverallState(PipelineStatus::StageState::STARTING);
    status_->setStartTime(std::chrono::steady_clock::now());
    
    try {
        // Call subclass implementation
        if (!doStart()) {
            status_->setLastError("Failed to start pipeline components");
            status_->setOverallState(PipelineStatus::StageState::ERROR);
            return false;
        }
        
        // Start monitoring if enabled
        if (config_.enableHealthChecks) {
            enablePeriodicHealthCheck(true);
        }
        
        status_->setOverallState(PipelineStatus::StageState::RUNNING);
        status_->setHealthState(PipelineStatus::HealthState::HEALTHY);
        
        return true;
        
    } catch (const std::exception& e) {
        status_->setLastError("Exception during startup: " + std::string(e.what()));
        status_->setOverallState(PipelineStatus::StageState::ERROR);
        return false;
    }
}

void MonitoredPipeline::stop() {
    if (status_->isStopped()) {
        return; // Already stopped
    }
    
    status_->setOverallState(PipelineStatus::StageState::STOPPING);
    
    // Stop monitoring
    enablePeriodicHealthCheck(false);
    
    try {
        // Call subclass implementation
        doStop();
        
        status_->setOverallState(PipelineStatus::StageState::STOPPED);
        status_->setHealthState(PipelineStatus::HealthState::UNKNOWN);
        
    } catch (const std::exception& e) {
        status_->setLastError("Exception during shutdown: " + std::string(e.what()));
        status_->setOverallState(PipelineStatus::StageState::ERROR);
    }
}

bool MonitoredPipeline::restart() {
    stop();
    
    // Wait a moment for clean shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    return start();
}

bool MonitoredPipeline::performHealthCheck() {
    try {
        // Call subclass implementation
        bool healthy = doHealthCheck();
        
        if (healthy) {
            status_->setHealthState(PipelineStatus::HealthState::HEALTHY);
        } else {
            status_->setHealthState(PipelineStatus::HealthState::WARNING);
        }
        
        return healthy;
        
    } catch (const std::exception& e) {
        status_->setLastError("Health check failed: " + std::string(e.what()));
        status_->setHealthState(PipelineStatus::HealthState::CRITICAL);
        return false;
    }
}

void MonitoredPipeline::enablePeriodicHealthCheck(bool enable) {
    if (enable && !monitoringEnabled_.load()) {
        monitoringEnabled_.store(true);
        monitoringThread_ = std::make_unique<std::thread>(&MonitoredPipeline::monitoringLoop, this);
    } else if (!enable && monitoringEnabled_.load()) {
        monitoringEnabled_.store(false);
        if (monitoringThread_ && monitoringThread_->joinable()) {
            monitoringThread_->join();
        }
        monitoringThread_.reset();
    }
}

void MonitoredPipeline::monitoringLoop() {
    while (monitoringEnabled_.load()) {
        try {
            performHealthCheck();
            updateHealthState();
            
            std::this_thread::sleep_for(config_.healthCheckInterval);
            
        } catch (const std::exception& e) {
            status_->setLastError("Monitoring loop error: " + std::string(e.what()));
        }
    }
}

void MonitoredPipeline::updateHealthState() {
    // Check error rates
    double errorRate = metrics_->getErrorRate();
    if (errorRate > 0.1) { // 10% error rate threshold
        status_->setHealthState(PipelineStatus::HealthState::CRITICAL);
        return;
    } else if (errorRate > 0.05) { // 5% warning threshold
        status_->setHealthState(PipelineStatus::HealthState::WARNING);
        return;
    }
    
    // Check queue sizes
    auto queueNames = metrics_->getQueueNames();
    for (const auto& queueName : queueNames) {
        size_t currentSize = metrics_->getCurrentQueueSize(queueName);
        if (currentSize > config_.maxQueueSize * 0.9) { // 90% full
            status_->setHealthState(PipelineStatus::HealthState::WARNING);
            return;
        }
    }
    
    // Check if any stages are in error state
    auto unhealthyStages = status_->getUnhealthyStages();
    if (!unhealthyStages.empty()) {
        status_->setHealthState(PipelineStatus::HealthState::CRITICAL);
        return;
    }
    
    // All checks passed
    if (status_->isRunning()) {
        status_->setHealthState(PipelineStatus::HealthState::HEALTHY);
    }
}

} // namespace data_ingestion
} // namespace trading