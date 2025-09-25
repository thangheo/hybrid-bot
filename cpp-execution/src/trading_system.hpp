/**
 * @file trading_system.hpp
 * @brief Complete trading system integration demonstration
 * 
 * This demo integrates all major components:
 * - Data Ingestion Service (order book data)
 * - Strategy Engine (signal generation)
 * - Risk Manager (validation and monitoring)
 * - Execution Engine (order placement)
 * - Monitoring (performance tracking)
 */
#pragma once

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <fstream>
#include <signal.h>
#include <unordered_map>
#include <deque>
#include <condition_variable>
#include <mutex>

// Common includes
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include "common/logger.hpp"
#include "communication/zmq_server.hpp"
#include "common/exchange_types.hpp"
// Data ingestion
#include "data_ingestion/data_ingestion_service.hpp"
//zmq server for market intellgence
// #include "communication/zmq_server.hpp"
#include "market_intelligence/adaptive_intelligence.hpp"
#include "../../generated/cpp/proto/trading_messages.pb.h"
#include <nlohmann/json.hpp>

// Strategy engine
#include "strategy/engine/strategy_engine.hpp"
#include "strategy/config/strategy_config_loader.hpp"
//ptca
#include "learning/ptca_integration_manager.hpp"
#include "learning/pavlovian_credit_assignment.hpp"
#include "risk/interfaces/market_data_interface.hpp"

// Risk management
#include "risk/risk_manager.hpp"
#include "risk/risk_config.hpp"
#include "risk/risk_types.hpp"
// Position sizer
#include "risk/position_sizer.hpp"
#include <algorithm>

// Execution engine
#include "execution/execution_service.hpp"
#include "execution/trading_signal.hpp"
#include "execution/filter/signal_dedup.hpp"
// Core types
#include "core/market_types.hpp"
#include "core/direction.hpp"
#include "core/types.hpp"
//protobuf message for internal orderbook
#include "internal_orderbook.pb.h"
// #include "../generated/cpp/internal_oderbook.grpc.pb.h"

// zmq clients
#include "communication/zmq_config.hpp"

using namespace trading;
using namespace std::chrono_literals;
using namespace trading::common;
using namespace trading::core;
using namespace trading::data_ingestion;
using namespace trading::strategy;
using namespace trading::risk;
using namespace trading::execution;

namespace trading {

class Trading_system {
public:
    Trading_system()  ;
    
    ~Trading_system() ;

    trading::ExecutionAcknowledgment handleTradingIntent(const trading::TradingIntent& intent);


    bool initialize() ;
    
    bool start() ;
    // Or run without time limit:
    void runIndefinitely() ;
    void shutdown();
    void evaluatePTCADecision(const core::Symbol& symbol);
        
    // Helper method to get current signals for a symbol (you'll need to implement this)
    std::vector<strategy::TradingSignal> getCurrentSignalsForSymbol(const core::Symbol& symbol) ;
    void reportPTCAStats() ;


    void printStatus();
    

    void generateMarketIntelligence(const OrderBookUpdate& update, std::shared_ptr<OrderBook> orderBook) ;

    // ✅ NEW: Publish orderbook data for risk module
    void publishOrderBookForRisk(const OrderBookUpdate& update);

    // ✅ Simple health check like full_bridge.cpp
    bool checkSystemHealth() ;

private:
    // Orderbook processing queue and worker pool
    std::deque<OrderBookUpdate> orderbookQueue_;
    std::mutex orderbookQueueMutex_;
    std::condition_variable orderbookQueueCv_;
    std::vector<std::thread> orderbookWorkers_;
    std::atomic<bool> orderbookWorkersRunning_{false};
    size_t orderbookQueueLimit_ = 2000; // hard cap
    size_t orderbookBatchSize_ = 20; // number of updates per batch
    size_t orderbookWorkerCount_ = 1; // start with 1 worker (tune for VPS)

    // Signal processing queue and worker pool
    std::deque<strategy::TradingSignal> signalQueue_;
    std::mutex signalQueueMutex_;
    std::condition_variable signalQueueCv_;
    std::vector<std::thread> signalWorkers_;
    std::atomic<bool> signalWorkersRunning_{false};
    size_t signalQueueLimit_ = 10000; // cap signals
    size_t signalBatchSize_ = 16; // number of signals per batch
    size_t signalWorkerCount_ = 1; // number of workers for signals

    // Execution task queue and worker pool (avoid spawning detached threads)
    std::deque<execution::TradingSignal> execQueue_;
    std::mutex execQueueMutex_;
    std::condition_variable execQueueCv_;
    std::vector<std::thread> execWorkers_;
    std::atomic<bool> execWorkersRunning_{false};
    size_t execQueueLimit_ = 5000;
    size_t execWorkerCount_ = 1; // tune for VPS

    //market intel
    std::atomic<uint64_t> messagesProcessed_{0};
    std::atomic<uint64_t> intelligenceGenerated_{0};
    // marketDataInterface_
    std::shared_ptr<trading::risk::MarketDataInterface> marketDataInterface_;
    // PTCA: Add PTCA-related members
    std::unique_ptr<trading::learning::PTCAIntegrationManager> ptcaManager_;
    std::chrono::time_point<std::chrono::steady_clock> lastPriceUpdate_;
    // std::unique_ptr<DataInterface> m_dataInterface;
    
    std::shared_ptr<GenericZmqServer> zmq_server_;
    std::unique_ptr<GenericZmqClient> zmq_client_;

    std::unique_ptr<trading::intelligence::AdaptiveMarketIntelligence> adaptiveIntel_;
    std::mutex intelligenceMutex_;

    // Market intelligence tracking
    std::unordered_map<std::string, trading::MarketIntelligence> latestIntelligence_;
    // Thread-safe helper maps for intelligence throttling and debug counts
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastIntelTime_;
    std::unordered_map<std::string, int> intelDebugCounts_;
    std::mutex intelMapsMutex_;

    // Core components
    std::unique_ptr<data_ingestion::DataIngestionService> dataIngestionService_;
    std::unique_ptr<strategy::StrategyEngine> strategyEngine_;
    std::shared_ptr<risk::RiskManager> riskManager_;
    std::shared_ptr<execution::ExecutionService> executionService_;
    // Position sizing helper (Kelly / volatility adjusted)
    std::unique_ptr<trading::risk::PositionSizer> positionSizer_;
    // gateway
    std::shared_ptr<common::ExchangeGateway> exchangeGateway_;

    // Configurations
    risk::RiskConfig riskConfig_;
    strategy::StrategyConfig strategyConfig_;
    // System state
    std::atomic<bool> running_;
    std::atomic<bool> systemHealthy_;
    std::chrono::steady_clock::time_point startTime_;
    
    // Monitoring threads
    std::vector<std::thread> monitoringThreads_;
    
    // Statistics
    std::atomic<size_t> orderBookUpdatesReceived_{0};
    std::atomic<size_t> signalsGenerated_{0};
    std::atomic<size_t> signalsValidated_{0};
    std::atomic<size_t> signalsApproved_{0};
    std::atomic<size_t> signalsRejected_{0};
    std::atomic<size_t> ordersExecuted_{0};
    std::atomic<size_t> riskEventsTriggered_{0};
    // Instrumentation for publish latency
    std::atomic<uint64_t> publishTimeNs_{0};
    std::atomic<uint64_t> publishCalls_{0};
    
    int calculateCompletionDelay(const trading::TradingIntent& intent, 
                                const trading::MarketIntelligence& intel,
                                bool hasIntel) ;

    void setupConfigurations() ;
        
    void createStrategyConfig() ;
    void onMarketPriceUpdate(const core::Symbol& symbol, double price) ;
        // Add this to your Trading_system class:
    void setupPTCAConnections() ;
    
    void onExecutionResponse(const execution::ExecutionResponse& response) ;
    
    //create ptca config
    void createPTCAConfig() ;
    void createRiskConfig();
    
    void setupStatistics();
    
    void setupExecutionEngine() ;
    // Orderbook worker pool control
    void startOrderbookWorkerPool() ;
    void stopOrderbookWorkerPool() ;

    void orderbookWorkerLoop(size_t workerId) ;
    // Signal worker pool control
    void startSignalWorkerPool();

    void stopSignalWorkerPool();

    void signalWorkerLoop(size_t workerId);
    // Execution worker pool control
    void startExecWorkerPool() ;
    void stopExecWorkerPool() ;

    void execWorkerLoop(size_t workerId);

    void setupComponentConnections() ;
    
    void realExecution(const execution::TradingSignal& signal) ;

};

} // namespace trading