#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <algorithm>

namespace trading {
namespace strategy {

/**
 * @brief Memory-optimized correlation parameters
 */
struct OptimizedCorrelationConfig {
    // Reduced data point limits
    size_t max_data_points = 2000;        // Reduced from 5000
    size_t min_data_points = 50;          // Minimum for statistical significance
    size_t correlation_window = 500;      // Rolling window size
    
    // Memory management
    size_t max_metrics = 32;              // Reduced from 64
    size_t memory_limit_mb = 256;         // Hard memory limit
    bool enable_memory_monitoring = true;
    
    // Processing optimization
    bool use_streaming_calculation = true;
    bool enable_metric_filtering = true;   // Filter low-importance metrics
    double metric_importance_threshold = 0.01;
    
    // Correlation parameters
    double correlation_decay_rate = 0.95;
    double time_decay_factor = 0.1;
    double min_stddev_threshold = 1e-8;
    size_t max_level_depth = 5;           // Reduced from higher values
    
    // Performance tuning
    bool use_vector_pool = true;
    size_t vector_pool_size = 15;
    bool enable_parallel_calculation = false; // Disabled to save memory
    
    // Emergency handling
    bool auto_reduce_on_pressure = true;
    double memory_pressure_threshold = 0.8; // 80% of limit
    size_t emergency_data_points = 1000;
};

/**
 * @brief Runtime memory monitor for correlation calculations
 */
class CorrelationMemoryMonitor {
public:
    explicit CorrelationMemoryMonitor(size_t limit_mb) 
        : memory_limit_bytes_(limit_mb * 1024 * 1024) {}
    
    bool checkMemoryAvailable(size_t required_bytes) const {
        return getCurrentMemoryUsage() + required_bytes < memory_limit_bytes_;
    }
    
    double getMemoryPressure() const {
        return static_cast<double>(getCurrentMemoryUsage()) / memory_limit_bytes_;
    }
    
    bool isUnderPressure() const {
        return getMemoryPressure() > 0.8; // 80% threshold
    }
    
    size_t getAvailableBytes() const {
        size_t current = getCurrentMemoryUsage();
        return current < memory_limit_bytes_ ? memory_limit_bytes_ - current : 0;
    }

private:
    size_t memory_limit_bytes_;
    
    size_t getCurrentMemoryUsage() const {
        // Platform-specific implementation would go here
        // For now, return a conservative estimate
        return memory_limit_bytes_ / 2; // Assume 50% usage
    }
};

/**
 * @brief Adaptive configuration that adjusts based on memory pressure
 */
class AdaptiveCorrelationConfig {
public:
    explicit AdaptiveCorrelationConfig(const OptimizedCorrelationConfig& base_config)
        : base_config_(base_config)
        , current_config_(base_config)
        , monitor_(base_config.memory_limit_mb) {}
    
    const OptimizedCorrelationConfig& getCurrentConfig() const {
        return current_config_;
    }
    
    void updateBasedOnMemoryPressure() {
        double pressure = monitor_.getMemoryPressure();
        if (pressure > 0.9) {
            // Critical pressure - aggressive reduction
            current_config_.max_data_points = std::min(current_config_.max_data_points, 
                                                      base_config_.emergency_data_points);
            current_config_.max_metrics = std::min(current_config_.max_metrics, static_cast<size_t>(16));
            current_config_.correlation_window = std::min(current_config_.correlation_window, static_cast<size_t>(250));
        } else if (pressure > 0.7) {
        } else if (pressure > 0.7) {
            // High pressure - moderate reduction
            current_config_.max_data_points = static_cast<size_t>(base_config_.max_data_points * 0.7);
            current_config_.max_metrics = static_cast<size_t>(base_config_.max_metrics * 0.75);
        } else if (pressure < 0.4) {
            // Low pressure - can increase back towards base config
            current_config_.max_data_points = std::min(
                static_cast<size_t>(current_config_.max_data_points * 1.1),
                base_config_.max_data_points
            );
            current_config_.max_metrics = std::min(
                static_cast<size_t>(current_config_.max_metrics * 1.1),
                base_config_.max_metrics
            );
        }
    }
    
    bool shouldProcessCorrelations() const {
        return !monitor_.isUnderPressure();
    }

private:
    OptimizedCorrelationConfig base_config_;
    OptimizedCorrelationConfig current_config_;
    CorrelationMemoryMonitor monitor_;
};

} // namespace strategy
} // namespace trading

// Usage example:
/*
class MomentumCorrelation {
private:
    AdaptiveCorrelationConfig adaptive_config_;
    
public:
    void calculateCorrelations() {
        adaptive_config_.updateBasedOnMemoryPressure();
        
        if (!adaptive_config_.shouldProcessCorrelations()) {
            LOG_WARN("Skipping correlation calculation due to memory pressure");
            return;
        }
        
        const auto& config = adaptive_config_.getCurrentConfig();
        size_t max_points = config.max_data_points;
        
        // Use config.max_metrics, config.correlation_window, etc.
    }
};
*/
