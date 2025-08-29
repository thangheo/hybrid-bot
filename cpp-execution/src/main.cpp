#include <iostream>
#include <thread>
#include <vector>
#include <signal.h>

// #include "common/config.hpp"
#include "common/logger.hpp"
#include "common/types.hpp"
#include "data_ingestion/config.hpp"
// #include "data_ingestion/websocket_client.hpp"
// #include "data_ingestion/order_book.hpp"
// #include "strategy/analyzer.hpp"
// #include "strategy/imbalance_detector.hpp"
// #include "strategy/signal_generator.hpp"
// #include "execution/signal_processor.hpp"
// #include "execution/order_service.hpp"
// #include "risk/pre_trade_filter.hpp"

// Global flag for graceful shutdown
volatile bool running = true;

void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

int main(int argc, char* argv[]) {
    // Register signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // try {
    //     // Initialize logging
    //     common::Logging::init();
        
    //     // Load configuration
    //     auto config = common::Config::load("config/system_config.json");
    //     auto exchanges_config = common::Config::load("config/exchanges.json");
    //     auto strategy_config = common::Config::load("config/strategy_params.json");
    //     auto risk_config = common::Config::load("config/risk_limits.json");
        
    //     LOG_INFO("System starting up...");
        
    //     // Initialize components
    //     data_ingestion::OrderBook orderBook;
    //     strategy::Analyzer analyzer(strategy_config);
    //     strategy::ImbalanceDetector detector(strategy_config);
    //     strategy::SignalGenerator signalGen(strategy_config);
    //     risk::PreTradeFilter riskFilter(risk_config);
    //     execution::SignalProcessor signalProcessor;
    //     execution::OrderService orderService(exchanges_config);
        
    //     // Start WebSocket clients for each exchange
    //     std::vector<std::unique_ptr<data_ingestion::WebsocketClient>> clients;
    //     for (const auto& exchange : exchanges_config.getExchanges()) {
    //         auto client = std::make_unique<data_ingestion::WebsocketClient>(
    //             exchange.name,
    //             exchange.websocket_url
    //         );
    //         client->connect();
    //         clients.push_back(std::move(client));
    //     }
        
    //     LOG_INFO("All components initialized, entering main loop");
        
    //     // Main loop
    //     while (running) {
    //         // Process updates from clients to order book
    //         orderBook.processUpdates();
            
    //         // Analyze order book for imbalances
    //         auto metrics = analyzer.analyze(orderBook);
            
    //         // Detect significant imbalances
    //         auto imbalances = detector.detect(metrics);
            
    //         // Generate trading signals
    //         if (!imbalances.empty()) {
    //             auto signals = signalGen.generateSignals(imbalances);
                
    //             // Process signals through risk filter
    //             for (const auto& signal : signals) {
    //                 if (riskFilter.validateSignal(signal)) {
    //                     // Process valid signals
    //                     auto order = signalProcessor.processSignal(signal);
    //                     orderService.placeOrder(order);
    //                 }
    //             }
    //         }
            
    //         // Small sleep to prevent CPU hogging
    //         std::this_thread::sleep_for(std::chrono::milliseconds(1));
    //     }
        
    //     // Cleanup
    //     LOG_INFO("Shutting down gracefully...");
    //     for (auto& client : clients) {
    //         client->disconnect();
    //     }
        
    // } catch (const std::exception& e) {
    //     std::cerr << "Error: " << e.what() << std::endl;
    //     return 1;
    // }
    
    return 0;
}

// // Production
// int main() {
//     // Core components as separate objects
//     MarketDataIngestion data_feed;           // Real market data
//     RiskManager risk_engine;                 // Position/risk limits  
//     ExecutionEngine execution_engine;        // Actual order placement
//     MonitoringService monitor;               // Health/performance tracking
    
//     // Communication layer
//     TradingCommunicationServer comm_server;  // ZMQ only
    
//     // Wire components together
//     comm_server.SetDataFeed(&data_feed);
//     comm_server.SetRiskEngine(&risk_engine);
//     comm_server.SetExecutionEngine(&execution_engine);
//     comm_server.SetMonitor(&monitor);
    
//     // Start all components
//     data_feed.Start();
//     risk_engine.Start(); 
//     execution_engine.Start();
//     monitor.Start();
//     comm_server.Start();
    
//     // Main loop
//     while (!shutdown_requested) {
//         // Health checks, coordination logic
//     }
// }