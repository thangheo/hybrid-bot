#include "interfaces/execution_interface.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace trading::risk;

int main() {
    // Create execution interface
    auto executionInterface = createExecutionInterface();
    
    // Initialize
    if (!executionInterface->initialize()) {
        std::cerr << "Failed to initialize execution interface" << std::endl;
        return 1;
    }
    
    // Register callback for execution reports
    executionInterface->registerExecutionReportCallback([](const ExecutionReport& report) {
        std::cout << "Received execution report for order " << report.orderId << std::endl;
        std::cout << "  Status: " << report.status << std::endl;
        std::cout << "  Filled: " << report.filledQuantity << "/" << report.quantity << std::endl;
    });
    
    // Create an order request
    OrderRequest orderRequest;
    orderRequest.clientOrderId = "test_order_001";
    orderRequest.exchangeId = "binance";
    orderRequest.tradingPair = "BTC/USDT";
    orderRequest.side = OrderSide::BUY;
    orderRequest.type = "limit";
    orderRequest.quantity = 0.001;
    orderRequest.price = 50000.0;
    orderRequest.timeInForce = "GTC";
    orderRequest.strategyId = "test_strategy";
    orderRequest.signalId = "signal_001";
    orderRequest.timestamp = std::chrono::system_clock::now();
    
    // Submit the order
    std::string orderId = executionInterface->submitOrder(orderRequest);
    if (!orderId.empty()) {
        std::cout << "Order submitted with ID: " << orderId << std::endl;
    } else {
        std::cerr << "Failed to submit order" << std::endl;
    }
    
    // Wait for a while to receive execution reports
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // Cancel the order if needed
    if (!orderId.empty()) {
        if (executionInterface->cancelOrder(orderId)) {
            std::cout << "Order cancelled successfully" << std::endl;
        } else {
            std::cerr << "Failed to cancel order" << std::endl;
        }
    }
    
    return 0;
}