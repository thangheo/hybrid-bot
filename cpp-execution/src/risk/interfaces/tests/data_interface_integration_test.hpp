//  data_interface_integration_test.hpp
  
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
#include "communication/zmq_server_client.hpp"
#include "common/exchange_types.hpp"
//share memory
// #include "communication/shm.hpp"
// Data ingestion
#include "data_ingestion/data_ingestion_service.hpp"
//zmq server for market intellgence
#include "market_intelligence/adaptive_intelligence.hpp"
#include "../../generated/cpp/proto/trading_messages.pb.h"
#include <nlohmann/json.hpp>

// Strategy engine
#include "strategy/engine/strategy_engine.hpp"
#include "strategy/config/strategy_config_loader.hpp"
//ptca
// #include "learning/ptca_integration_manager.hpp"
// #include "learning/pavlovian_credit_assignment.hpp"
// #include "risk/interfaces/market_data_interface.hpp"

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


using namespace trading;
using namespace std::chrono_literals;
using namespace trading::common;
using namespace trading::core;
using namespace trading::data_ingestion;
using namespace trading::strategy;
using namespace trading::risk;
using namespace trading::execution;

// data_interface_integration_test.hpp
 // Event bus for internal communication
    // EventBus eventBus_;

    // Statistics
    std::atomic<uint64_t> orderBookUpdatesReceived_{0};
    

    // Orderbook processing queue and worker pool
    std::deque<OrderBookUpdate> orderbookQueue_;
    std::mutex orderbookQueueMutex_;
    std::condition_variable orderbookQueueCv_;
    std::vector<std::thread> orderbookWorkers_;
    std::atomic<bool> orderbookWorkersRunning_{false};
    size_t orderbookQueueLimit_ = 20000; // hard cap (increased to handle bursts)
    size_t orderbookBatchSize_ = 40; // number of updates per batch (larger batches)
    size_t orderbookWorkerCount_ = 1; // increased worker count for faster consumption on upgraded VPS

    // Signal processing queue and worker pool
    std::deque<strategy::TradingSignal> signalQueue_;
    std::mutex signalQueueMutex_;
    std::condition_variable signalQueueCv_;
    std::vector<std::thread> signalWorkers_;
    std::atomic<bool> signalWorkersRunning_{false};
    size_t signalQueueLimit_ = 10000; // cap signals
    size_t signalBatchSize_ = 16; // number of signals per batch
    size_t signalWorkerCount_ = 1; // number of workers for signals

    // Strategy-specific orderbook processing queue (decouples strategy work from orderbook workers)
    std::deque<std::shared_ptr<OrderBook>> strategyOrderbookQueue_;
    std::mutex strategyOrderbookQueueMutex_;
    std::condition_variable strategyOrderbookQueueCv_;
    std::vector<std::thread> strategyOrderbookWorkers_;
    std::atomic<bool> strategyOrderbookWorkersRunning_{false};
    size_t strategyOrderbookQueueLimit_ = 20000;
    size_t strategyOrderbookBatchSize_ = 16;
    size_t strategyOrderbookWorkerCount_ = 2;

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
    // std::atomic<uint64_t> InternalMsg{0};
    // marketDataInterface_
    std::shared_ptr<trading::risk::MarketDataInterface> marketDataInterface_;
    // PTCA: Add PTCA-related members
    // std::unique_ptr<trading::learning::PTCAIntegrationManager> ptcaManager_;
    std::chrono::time_point<std::chrono::steady_clock> lastPriceUpdate_;
    // std::unique_ptr<DataInterface> m_dataInterface;
    
    std::shared_ptr<GenericZmqServer> zmq_server_;
    std::unique_ptr<GenericZmqClient> zmq_client_;
    //share context
    std::shared_ptr<zmq::context_t> zmq_context_;

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
    // std::atomic<size_t> orderBookUpdatesReceived_{0};
    std::atomic<size_t> signalsGenerated_{0};
    std::atomic<size_t> signalsValidated_{0};
    std::atomic<size_t> signalsApproved_{0};
    std::atomic<size_t> signalsRejected_{0};
    std::atomic<size_t> ordersExecuted_{0};
    std::atomic<size_t> riskEventsTriggered_{0};
    // Instrumentation for publish latency
    std::atomic<uint64_t> publishTimeNs_{0};
    std::atomic<uint64_t> publishCalls_{0};
    