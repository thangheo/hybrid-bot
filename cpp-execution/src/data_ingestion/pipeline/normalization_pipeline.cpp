/**
 * @file normalization_pipeline.cpp
 * @brief Implementation of the normalization pipeline
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#include "pipeline/normalization_pipeline.hpp"
#include "data_ingestion/protocol/adapter_factory.hpp"
#include "data_ingestion/orderbook/order_book_manager.hpp"
#include "data_ingestion/data_quality/data_quality.hpp"
#include "data_ingestion/time/synchronizer.hpp"
#include "data_ingestion/normalization/normalizer.hpp"
#include <stdexcept>

namespace trading {
namespace data_ingestion {

// =============================================================================
// NormalizationPipeline Implementation
// =============================================================================

NormalizationPipeline::NormalizationPipeline(
    std::shared_ptr<AdapterFactory> adapterFactory,
    std::shared_ptr<OrderBookManager> orderBookManager,
    std::shared_ptr<DataQualityControl> qualityControl,
    std::shared_ptr<TimeSynchronizer> synchronizer)
    : adapterFactory_(adapterFactory),
      orderBookManager_(orderBookManager),
      qualityControl_(qualityControl),
      synchronizer_(synchronizer) {
    
    if (!validateComponents()) {
        throw std::invalid_argument("Invalid components provided to pipeline");
    }
}

NormalizationPipeline::~NormalizationPipeline() {
    stop();
}

bool NormalizationPipeline::configure(const PipelineConfig& config) {
    if (!MonitoredPipeline::configure(config)) {
        return false;
    }
    
    try {
        initializeQueues();
        initializeStages();
        return true;
    } catch (const std::exception& e) {
        status_->setLastError("Pipeline configuration failed: " + std::string(e.what()));
        return false;
    }
}

bool NormalizationPipeline::doStart() {
    if (!protocolStage_ || !orderBookStage_ || !qualityStage_ || 
        !timestampStage_ 
        // || !publisherStage_
    ) {
        status_->setLastError("Pipeline stages not initialized");
        return false;
    }
    
    try {
        // Start stages in order
        protocolStage_->start();
        orderBookStage_->start();
        qualityStage_->start();
        timestampStage_->start();
        // publisherStage_->start();
        
        return true;
    } catch (const std::exception& e) {
        status_->setLastError("Failed to start pipeline stages: " + std::string(e.what()));
        return false;
    }
}

void NormalizationPipeline::doStop() {
    // Stop stages in reverse order
    // if (publisherStage_) publisherStage_->stop();
    if (timestampStage_) timestampStage_->stop();
    if (qualityStage_) qualityStage_->stop();
    if (orderBookStage_) orderBookStage_->stop();
    if (protocolStage_) protocolStage_->stop();
}

bool NormalizationPipeline::doHealthCheck() {
    // Check if all components are healthy
    bool healthy = true;
    std::vector<std::string> issues;
    
    // Check stage states
    auto stageStates = status_->getStageStates();
    for (const auto& [stageName, state] : stageStates) {
        if (state == PipelineStatus::StageState::ERROR) {
            healthy = false;
            issues.push_back("Stage " + stageName + " in error state");
        }
    }
    
    // Check error rates
    double errorRate = metrics_->getErrorRate();
    if (errorRate > 0.1) { // 10% error rate
        healthy = false;
        issues.push_back("High error rate: " + std::to_string(errorRate * 100) + "%");
    }
    
    // Check queue sizes
    auto queueNames = metrics_->getQueueNames();
    for (const auto& queueName : queueNames) {
        size_t currentSize = metrics_->getCurrentQueueSize(queueName);
        if (currentSize > config_.maxQueueSize * 0.95) { // 95% full
            healthy = false;
            issues.push_back("Queue " + queueName + " nearly full: " + std::to_string(currentSize));
        }
    }
    
    // Check throughput
    double throughput = metrics_->getThroughputPerSecond();
    if (throughput == 0.0 && status_->isRunning()) {
        // Only report zero throughput as unhealthy if we've been running for a while
        auto uptime = status_->getUptime();
        if (uptime > std::chrono::seconds(30)) {
            healthy = false;
            issues.push_back("Zero throughput detected");
        }
    }
    
    // Log issues if any
    if (!healthy && !issues.empty()) {
        std::string combinedIssues = "Health check issues: ";
        for (size_t i = 0; i < issues.size(); ++i) {
            if (i > 0) combinedIssues += "; ";
            combinedIssues += issues[i];
        }
        status_->addWarning("HealthCheck", combinedIssues);
    }
    
    return healthy;
}

void NormalizationPipeline::initializeQueues() {
    inputQueue_ = std::make_unique<ThreadSafeQueue<core::ExchangeData>>(config_.maxQueueSize);
    stage1Queue_ = std::make_unique<ThreadSafeQueue<OrderBookUpdate>>(config_.maxQueueSize);
    stage2Queue_ = std::make_unique<ThreadSafeQueue<OrderBookUpdate>>(config_.maxQueueSize);
    stage3Queue_ = std::make_unique<ThreadSafeQueue<OrderBookUpdate>>(config_.maxQueueSize);
    outputQueue_ = std::make_unique<ThreadSafeQueue<int>>(config_.maxQueueSize);
}

void NormalizationPipeline::initializeStages() {
    protocolStage_ = std::make_unique<ProtocolAdapterStage>(
        inputQueue_.get(), stage1Queue_.get(), adapterFactory_, metrics_, status_);
        
    orderBookStage_ = std::make_unique<OrderBookStage>(
        stage1Queue_.get(), stage2Queue_.get(), orderBookManager_, metrics_, status_);
        
    qualityStage_ = std::make_unique<DataQualityStage>(
        stage2Queue_.get(), stage3Queue_.get(), qualityControl_, metrics_, status_);
        
    timestampStage_ = std::make_unique<TimestampStage>(
        stage3Queue_.get(), stage3Queue_.get(), synchronizer_, metrics_, status_);
        
    // publisherStage_ = std::make_unique<PublisherStage>(
    //     stage3Queue_.get(), outputQueue_.get(), publisher_, orderBookManager_, metrics_, status_);
}

bool NormalizationPipeline::validateComponents() {
    if (!adapterFactory_) {
        status_->setLastError("AdapterFactory is null");
        return false;
    }
    
    if (!orderBookManager_) {
        status_->setLastError("OrderBookManager is null");
        return false;
    }
    
    if (!qualityControl_) {
        status_->setLastError("DataQualityControl is null");
        return false;
    }
    
    if (!synchronizer_) {
        status_->setLastError("TimeSynchronizer is null");
        return false;
    }
    
    // if (!publisher_) {
    //     status_->setLastError("Publisher is null");
    //     return false;
    // }
    
    return true;
}

core::ExchangeData NormalizationPipeline::convertMarketDataToExchangeData(
    const core::MarketDataUpdate& update) {
    
    core::ExchangeData exchangeData;
    exchangeData.exchangeId = update.exchangeId;
    exchangeData.symbol = core::Symbol::fromString(update.tradingPair);
    exchangeData.receivedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        update.timestamp.time_since_epoch()).count();
    exchangeData.isSnapshot = false; // MarketDataUpdate is typically incremental
    exchangeData.sourceType = "websocket"; // Assume websocket source
    
    // Convert data vector to string (simplified)
    exchangeData.data.assign(update.data.begin(), update.data.end());
    
    return exchangeData;
}

// =============================================================================
// Factory Functions Implementation
// =============================================================================

std::unique_ptr<NormalizationPipeline> createNormalizationPipeline(
    const PipelineConfig& config,
    std::shared_ptr<AdapterFactory> adapterFactory,
    std::shared_ptr<OrderBookManager> orderBookManager,
    std::shared_ptr<DataQualityControl> qualityControl,
    std::shared_ptr<TimeSynchronizer> synchronizer
    // std::shared_ptr<Normalizer> publisher
) {
    
    auto pipeline = std::make_unique<NormalizationPipeline>(
        adapterFactory, orderBookManager, qualityControl, synchronizer);
    
    if (!pipeline->configure(config)) {
        throw std::runtime_error("Failed to configure normalization pipeline");
    }
    
    return pipeline;
}

std::unique_ptr<NormalizationPipeline> createDefaultNormalizationPipeline(
    const PipelineConfig& config) {
    
    // Create default components (would typically be injected)
    auto adapterFactory = std::make_shared<AdapterFactory>();
    auto orderBookManager = std::make_shared<OrderBookManager>();
    auto qualityControl = std::make_shared<DataQualityControl>();
    
    // Create time synchronizer with default config
    time::TimeSynchronizer::Config syncConfig;
    syncConfig.ntpServers = {"time.cloudflare.com", "time.google.com"};
    syncConfig.syncInterval = std::chrono::seconds(300);
    syncConfig.enableExchangeCalibration = true;
    auto synchronizer = std::make_shared<TimeSynchronizer>(syncConfig);
    
    // Create a simple callback for the normalizer (just prints the order book)
    OrderBookCallback callback = [](const OrderBook& orderBook) {
        // Simple callback - could be enhanced to publish to downstream systems
        // For now, just log that we received an order book
        // std::cout << "Normalized order book for " << orderBook.getSymbol() << std::endl;
    };
    
    // auto publisher = std::make_shared<Normalizer>(callback);
    
    return createNormalizationPipeline(
        config, adapterFactory, orderBookManager, 
        qualityControl, synchronizer);
}

} // namespace data_ingestion
} // namespace trading// Crypto-specific data quality settings
