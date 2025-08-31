/**
 * @file pipeline_all.hpp
 * @brief Complete pipeline system includes
 * 
 * Convenience header that includes all pipeline-related components
 * for easy integration into the trading system.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

// Core monitoring components
#include "pipeline_monitoring.hpp"
#include "normalization_pipeline.hpp"

// Required dependencies (these would need to be implemented separately)
#include "data_ingestion/protocol/adapter_factory.hpp"
#include "data_ingestion/orderbook/order_book_manager.hpp"
#include "data_ingestion/data_quality/data_quality.hpp"
#include "data_ingestion/time/synchronizer.hpp"
#include "data_ingestion/normalization/normalizer.hpp"

// Core types
#include "core/market_types.hpp"
#include "common/exchange_types.hpp"

namespace trading {
namespace data_ingestion {

/**
 * Convenience factory for creating a complete pipeline system
 */
class PipelineFactory {
public:
    /**
     * Create a production-ready pipeline with all components
     */
    static std::unique_ptr<NormalizationPipeline> createProductionPipeline(
        const PipelineConfig& config = PipelineConfig{});
    
    /**
     * Create a development/testing pipeline with mock components
     */
    static std::unique_ptr<NormalizationPipeline> createTestPipeline(
        const PipelineConfig& config = PipelineConfig{});
    
    /**
     * Create pipeline with custom components
     */
    static std::unique_ptr<NormalizationPipeline> createCustomPipeline(
        const PipelineConfig& config,
        std::shared_ptr<AdapterFactory> adapterFactory,
        std::shared_ptr<OrderBookManager> orderBookManager,
        std::shared_ptr<DataQualityControl> qualityControl,
        std::shared_ptr<TimeSynchronizer> synchronizer,
        std::shared_ptr<Normalizer> publisher);
};

/**
 * Pipeline configuration presets
 */
struct PipelinePresets {
    /**
     * High-performance configuration for production trading
     */
    static PipelineConfig highPerformance() {
        PipelineConfig config;
        config.maxQueueSize = 50000;
        config.threadPoolSize = 8;
        config.enableMetrics = true;
        config.metricsInterval = std::chrono::milliseconds(1000);
        config.enableDetailedLogging = false;
        config.enableHealthChecks = true;
        config.healthCheckInterval = std::chrono::seconds(5);
        config.enableQueueBackpressure = true;
        config.queueTimeoutMs = std::chrono::milliseconds(100);
        config.maxRetryAttempts = 1;
        config.retryDelayMs = std::chrono::milliseconds(10);
        config.stopOnCriticalError = true;
        config.enableBatching = true;
        config.batchSize = 50;
        config.batchTimeoutMs = std::chrono::milliseconds(5);
        return config;
    }
    
    /**
     * Development configuration with detailed monitoring
     */
    static PipelineConfig development() {
        PipelineConfig config;
        config.maxQueueSize = 1000;
        config.threadPoolSize = 4;
        config.enableMetrics = true;
        config.metricsInterval = std::chrono::milliseconds(5000);
        config.enableDetailedLogging = true;
        config.enableHealthChecks = true;
        config.healthCheckInterval = std::chrono::seconds(10);
        config.enableQueueBackpressure = true;
        config.queueTimeoutMs = std::chrono::milliseconds(1000);
        config.maxRetryAttempts = 3;
        config.retryDelayMs = std::chrono::milliseconds(100);
        config.stopOnCriticalError = false;
        config.enableBatching = false;
        return config;
    }
    
    /**
     * Testing configuration with conservative settings
     */
    static PipelineConfig testing() {
        PipelineConfig config;
        config.maxQueueSize = 100;
        config.threadPoolSize = 2;
        config.enableMetrics = true;
        config.metricsInterval = std::chrono::milliseconds(1000);
        config.enableDetailedLogging = true;
        config.enableHealthChecks = true;
        config.healthCheckInterval = std::chrono::seconds(5);
        config.enableQueueBackpressure = false;
        config.queueTimeoutMs = std::chrono::milliseconds(500);
        config.maxRetryAttempts = 1;
        config.retryDelayMs = std::chrono::milliseconds(50);
        config.stopOnCriticalError = true;
        config.enableBatching = false;
        return config;
    }
};

/**
 * Pipeline utilities for common operations
 */
class PipelineUtils {
public:
    /**
     * Create a comprehensive status report
     */
    static std::string generateStatusReport(
        std::shared_ptr<PipelineMetrics> metrics,
        std::shared_ptr<PipelineStatus> status);
    
    /**
     * Export metrics in Prometheus format
     */
    static std::string exportPrometheusMetrics(
        std::shared_ptr<PipelineMetrics> metrics,
        const std::string& instanceName = "pipeline");
    
    /**
     * Check if pipeline performance meets SLA requirements
     */
    static bool checkSLA(
        std::shared_ptr<PipelineMetrics> metrics,
        double maxErrorRate = 0.01,           // 1% max error rate
        double minThroughput = 100.0,         // 100 msgs/sec minimum
        double maxAverageLatency = 1000.0);   // 1ms max average latency
    
    /**
     * Generate performance recommendations based on current metrics
     */
    static std::vector<std::string> generatePerformanceRecommendations(
        std::shared_ptr<PipelineMetrics> metrics,
        const PipelineConfig& config);
};

} // namespace data_ingestion
} // namespace trading