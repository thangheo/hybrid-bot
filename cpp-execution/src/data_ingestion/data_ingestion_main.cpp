// main.cpp
#include "data_ingestion/data_ingestion_service.hpp"
#include <signal.h>
#include <common/logger.hpp>
#include <iostream>

std::unique_ptr<trading::data_ingestion::DataIngestionService> service;

void signalHandler(int signal) {
    if (service) {
        service->stop();
    }
}

int main() {
    signal(SIGINT, signalHandler);
    
    service = std::make_unique<trading::data_ingestion::DataIngestionService>("config/test_config.json");
    
    // Set callbacks
    service->setStatusCallback([](const std::string& component, const std::string& status) {
        std::cout << "[" << component << "] " << status << std::endl;
    });
    
    service->setOrderBookCallback([](const auto& update) {
        // Process order book updates for strategy
    });
    
    // Initialize and start
    if (!service->initialize()) {
        std::cerr << "Failed to initialize service" << std::endl;
        return 1;
    }
    
    std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT", "BNBUSDT"};
    if (!service->start(symbols)) {
        std::cerr << "Failed to start service" << std::endl;
        return 1;
    }
    
    // Keep running
    while (service->isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}