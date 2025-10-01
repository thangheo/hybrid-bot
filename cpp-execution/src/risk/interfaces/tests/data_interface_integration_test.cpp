#include "data_interface_integration_test.hpp"
#include <iostream>

using namespace trading; 
using namespace trading::data_ingestion;
 
// dataIngestionService_;

void createTradingSystem() {
    // Initialize adaptive intelligence
    adaptiveIntel_ = std::make_unique<trading::intelligence::AdaptiveMarketIntelligence>();
    
    ZmqServerConfig serverConfig;
    //create shared context for server and client
    // use a single shared_context variable for server/client ZMQ usage
    zmq_context_ = std::make_shared<zmq::context_t>(1);
    // zmq_context_ = shared_context;

    serverConfig.pub_endpoint = SERVER_PUBLISHER_ENDPOINT; // internal endpoint for orderbook
    serverConfig.rep_endpoint = SERVER_REQUEST_ENDPOINT; // request endpoint for risk
    serverConfig.io_threads = 1;
    //use default config for now
    zmq_server_ = std::make_shared<GenericZmqServer>(serverConfig, zmq_context_);
    zmq_server_->registerRequestHandler("ping", [](const std::string& request, 
                                            const std::unordered_map<std::string, std::string>& metadata) -> std::string {
        nlohmann::json response;
        response["status"] = "pong";
        response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return response.dump();
    });
    
    ZmqClientConfig clientConfig;
    //no long use socket but use inproc
    clientConfig.pub_endpoints = {SERVER_PUBLISHER_ENDPOINT};
    clientConfig.req_endpoint = SERVER_REQUEST_ENDPOINT; // request endpoint for risk
    // clientConfig.auto_reconnect = true;
    // clientConfig.request_timeout = std::chrono::milliseconds(30000); // 30 seconds
    zmq_client_ = std::make_unique<GenericZmqClient>(clientConfig, zmq_context_);
    
    // Initialize MarketDataInterface
    // Subscriber should connect to the server's PUB endpoint; publisher endpoint
    // is the server's REQ/REP endpoint for requests. Ensure these are not swapped.
    riskConfig_.zmqSubscriberEndpoint = SERVER_PUBLISHER_ENDPOINT;
    riskConfig_.zmqPublisherEndpoint = SERVER_REQUEST_ENDPOINT;
    marketDataInterface_ = std::make_shared<trading::risk::MarketDataInterface>(riskConfig_);
    // set zmq client to marketDataInterface_
    marketDataInterface_->setZmqClient(std::move(zmq_client_));
    // Initialize position sizer with default config for now
    
}


// marketDataInterface_
bool initialize() {
    
    //du me may
    // 2. Initialize data ingestion service
    dataIngestionService_ = std::make_unique<data_ingestion::DataIngestionService>(
        "config/data_ingestion.json");
    //set comm server
    dataIngestionService_->setCommServer(zmq_server_); 
    if (!zmq_server_->isRunning()) {
        if (!zmq_server_->start()) {
            std::cerr << "Failed to start internal ZMQ server during initialization: " << zmq_server_->getLastError() << std::endl;
            return false;
        }
        // brief pause to let sockets bind
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // sleep for 2s
    std::this_thread::sleep_for(std::chrono::seconds(2));
    if (!dataIngestionService_->initialize()) {
        std::cerr << "Failed to initialize data ingestion service" << std::endl;
        return false;
    }
    // 3. Initialize market data interface
    if (!marketDataInterface_->initialize()) {
        std::cerr << "Failed to initialize MarketDataInterface" << std::endl;
        return false;
    } 
    
    // 4. Initialize strategy engine
    std::cout << "Initializing strategy engine..." << std::endl;
    strategyEngine_ = std::make_unique<strategy::StrategyEngine>(StrategyId("integrated_strategy_test_v1"));
    
    if (!strategyEngine_->initialize(strategyConfig_)) {
        std::cerr << "Failed to initialize strategy engine" << std::endl;
        return false;
    }
        
    return true;
}
  

void printStatus() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();

    

    std::cout << "=========================================" << std::endl;
} 
  
void signalHandler(int sig) {
    std::cout << "\nShutdown signal received..." << std::endl;
  
    exit(0);
}
void loop() {
    while (running_) {
        printStatus();
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

int main(int argc, char* argv[]) {
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
  





    createTradingSystem();
    // Start the full trading system
    //initialize
    if (!initialize()) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }
    
    // Add debug check
    if (marketDataInterface_) {
        auto stats = marketDataInterface_->getHealthStatus();
        std::cout << "MarketDataInterface connected: " << stats.subscriberHealthy << std::endl;
        std::cout << "Subscriber stats - received: " << stats.subscriberStats.totalReceived << std::endl;
    }

    startTime_ = std::chrono::steady_clock::now();
    running_ = true; 
    try {
        std::cout << "🚀 STARTING FULL TRADING SYSTEM 🚀" << std::endl;
        std::cout << "=====================================================" << std::endl;        

        
    } catch (const std::exception& e) {
         
        return 1;
    }


    
    return 0;
}