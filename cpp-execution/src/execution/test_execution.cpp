/**
 * @file execution_test_app.cpp
 * @brief Test application for order execution system
 * 
 * This application provides a comprehensive testing framework for the order
 * execution engine, including signal simulation, order lifecycle testing,
 * and performance monitoring.
 */

#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <sstream>

// Include your execution components
#include "execution/exchanges/binance_api_client.hpp"
#include "execution/execution_service.hpp"
#include "execution/models/order.hpp"
#include "execution/trading_signal.hpp"
#include "execution/execution_types.hpp"
#include "core/market_types.hpp"
#include "common/types.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace trading::execution;
using namespace trading::core;
using namespace trading;


class ExecutionTestApp {
public:
    ExecutionTestApp() 
        : running_(false),
          signal_generator_(std::random_device{}()) {
        
        // Initialize execution service components
        initializeExecutionService();
        
        // Setup test scenarios
        setupTestScenarios();
    }
    
    ~ExecutionTestApp() {
        stop();
    }
    
    bool start() {
        std::cout << "Starting Execution Test Application...\n";
        
        // Initialize the execution service
        if (!execution_service_->initialize()) {
            std::cerr << "Failed to initialize execution service\n";
            return false;
        }
        
        running_ = true;
        
        // Start test threads
        test_threads_.emplace_back(&ExecutionTestApp::signalGeneratorThread, this);
        test_threads_.emplace_back(&ExecutionTestApp::orderStatusMonitorThread, this);
        test_threads_.emplace_back(&ExecutionTestApp::performanceMonitorThread, this);
        
        std::cout << "Test application started successfully\n";
        return true;
    }
    
    void stop() {
        if (!running_) return;
        
        std::cout << "Stopping test application...\n";
        running_ = false;
        
        // Join all threads
        for (auto& thread : test_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        // Shutdown execution service
        execution_service_->shutdown();
        
        std::cout << "Test application stopped\n";
    }
    void initializeExecutionService() {
        // Create the authentication service first
        auto authService = std::make_shared<AuthenticationService>();
        
        // Create API client manager with auth service
        auto api_client_manager = std::make_shared<ApiClientManager>(authService);
        
        // Set up credentials and clients (your existing code)
        //get from environment .env file
        auto credentials = ExchangeCredentials{
            std::getenv("BINANCE_API_KEY"),
            std::getenv("BINANCE_API_SECRET"),
            "", 
            true
        };
        //debug api
        std::cout << "API KEY: " << std::getenv("BINANCE_API_KEY") << std::endl;
        std::cout << "API SECRET: " << std::getenv("BINANCE_API_SECRET") << std::endl;
        authService->setCredentials(core::ExchangeId::binance(), credentials);
        api_client_manager->addClient(core::ExchangeId::binance(),
            std::make_shared<BinanceApiClient>(credentials, 
            std::make_shared<RateLimitController>(core::ExchangeId::binance())));

        // Create other components
        auto order_generator = std::make_shared<OrderGenerator>();
        auto smart_order_router = std::make_shared<SmartOrderRouter>();
        auto order_type_selector = std::make_shared<OrderTypeSelector>();
        auto execution_algorithm_factory = std::make_shared<ExecutionAlgorithmFactory>();
        auto order_lifecycle_tracker = std::make_shared<OrderLifecycleTracker>();
        auto fill_reconciliation = std::make_shared<FillReconciliation>();

        // **CRITICAL: Initialize the smart order router with exchange information**
        std::vector<ExchangeInfo> exchanges;
        ExchangeInfo binanceInfo;
        binanceInfo.exchangeId = core::ExchangeId::binance();
        binanceInfo.trading_fee = 0.001;
        binanceInfo.liquidityProfileScore = 0.9;
        binanceInfo.latency_ms = 50;
        binanceInfo.success_rate = 0.99;
        exchanges.push_back(binanceInfo);
        
        // After initializing the router, add this debug check:
        smart_order_router->initialize(exchanges);

        // Debug: Test if the exchange supports our trading pairs
        std::cout << "Testing exchange support for BTC/USDT: " << 
            (binanceInfo.supports_trading_pair(core::Symbol("BTC/USDT")) ? "YES" : "NO") << std::endl;

        std::vector<std::string> available = smart_order_router->getAvailableExchanges(core::Symbol("BTC/USDT"));
        std::cout << "Available exchanges for BTC/USDT: " << available.size() << std::endl;
        for (const auto& ex : available) {
            std::cout << "  - " << ex << std::endl;
        }
        // Initialize the execution service
        execution_service_ = std::make_shared<ExecutionService>(
            order_generator,
            smart_order_router,
            order_type_selector,
            execution_algorithm_factory,
            order_lifecycle_tracker,
            fill_reconciliation,
            api_client_manager
        );
        // Enforce strict handling: reject LIMIT orders that don't carry a price
        execution_service_->setLimitOrderHandling(ExecutionService::LimitOrderHandling::REJECT_IF_NO_PRICE);
    }
     void runInteractiveMode() {
        std::cout << "\n=== Interactive Test Mode ===\n";
        std::cout << "Commands:\n";
        std::cout << "  1. signal - Generate test signal\n";
        std::cout << "  2. market - Place market order\n";
        std::cout << "  3. limit - Place limit order\n";
        std::cout << "  4. status - Show order status\n";
        std::cout << "  5. cancel - Cancel order\n";
        std::cout << "  6. health - Show system health\n";
        std::cout << "  7. stats - Show test statistics\n";
        std::cout << "  q. quit\n\n";
        
        std::string input;
        while (std::getline(std::cin, input) && input != "q") {
            processInteractiveCommand(input);
            std::cout << "\nEnter command: ";
        }
    }
    
private:
    std::shared_ptr<ExecutionService> execution_service_;
    std::atomic<bool> running_;
    std::vector<std::thread> test_threads_;
    std::mt19937 signal_generator_;
    // execution_service_
    // Test statistics
    std::atomic<int> signals_generated_{0};
    std::atomic<int> orders_placed_{0};
    std::atomic<int> orders_filled_{0};
    std::atomic<int> orders_cancelled_{0};
    std::atomic<int> orders_rejected_{0};
    
    // Test order tracking
    std::vector<std::string> active_order_ids_;
    std::mutex active_orders_mutex_;
     
    // Add this new method to set up test exchange clients:
    void setupTestScenarios() {
        std::cout << "Setting up test scenarios...\n";
        // Test scenarios will be configured here
    }
    void signalGeneratorThread() {
        std::cout << "Signal generator thread started\n";
        
        // Tạo 4 orders: 2 BUY + 2 SELL
        std::vector<std::pair<Direction, std::string>> orders_to_place = {
            {Direction::BUY, "Buy order 1"},
            // {Direction::BUY, "Buy order 2"}, 
            {Direction::SELL, "Sell order 1"},
            // {Direction::SELL, "Sell order 2"}
        };
        
        int orders_completed = 0;
        
        for (const auto& [direction, description] : orders_to_place) {
            if (!running_) break;
            
            TradingSignal signal;
            signal.id = (generateSignalId());
            signal.setTradingPair(core::Symbol("ETHUSDT"));
            signal.setDirection(direction);
            signal.setStrength(0.8);
            signal.setTargetSize(0.0025); // ~$15 worth
            signal.setTtl(std::chrono::milliseconds(10000));
            signal.setStrategyId(StrategyId("test_strategy"));
            // signal.setStrategyVersion("1.0");

            // QUAN TRỌNG: Dùng market order để tránh giá cố định
            // signal.signalMetrics["order_type"] = "MARKET";
                // Add ETH price metrics
                //if buy price =4280.0;, if sell price = 4350
            //get current bids ask
            
            if(direction == Direction::BUY){
                signal.signalMetrics["bid_price"] = 4280.0;
                signal.signalMetrics["ask_price"] = 4311.0;

            }
            else{
                signal.signalMetrics["bid_price"] = 4320.0;
                signal.signalMetrics["ask_price"] = 4331.0;

            }
            signal.signalMetrics["spread"] = 1.0;
            signal.signalMetrics["volatility"] = 0.02;
            signal.signalMetrics["volume"] = 1000.0;
            signal.signalMetrics["liquidity_score"] = 0.9;
            signal.signalMetrics["market_impact_estimate"] = 0.001;

            auto response = execution_service_->executeSignal(signal);
            
            signals_generated_++;
            orders_completed++;
            
            if (response.success) {
                orders_placed_++;
                std::cout << description << " SUCCESS: " << response.client_order_id << std::endl;
            } else {
                orders_rejected_++;
                std::cout << description << " FAILED: " << response.error_message << std::endl;
            }
            
            // Chờ 3 giây giữa các orders
            if (orders_completed < 4) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }
        
        std::cout << "Completed all test orders. Total: " << orders_completed << std::endl;
        running_ = false;
    }
    
    
    void orderStatusMonitorThread() {
        std::cout << "Order status monitor thread started\n";
        
        while (running_) {
            std::vector<std::string> orders_to_check;
            
            // Get copy of active orders
            {
                std::lock_guard<std::mutex> lock(active_orders_mutex_);
                orders_to_check = active_order_ids_;
            }
            
            // Check status of each active order
            for (const auto& order_id : orders_to_check) {
                auto status_response = execution_service_->getOrderStatus(order_id);
                
                if (status_response.found && status_response.order) {
                    auto order = status_response.order;
                    auto state = order->getState();
                    
                    // Update statistics based on order state
                    if (state == models::OrderState::FILLED) {
                        orders_filled_++;
                        removeFromActiveOrders(order_id);
                    } else if (state == models::OrderState::CANCELED) {
                        orders_cancelled_++;
                        removeFromActiveOrders(order_id);
                    } else if (state == models::OrderState::REJECTED) {
                        orders_rejected_++;
                        removeFromActiveOrders(order_id);
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
    void performanceMonitorThread() {
        std::cout << "Performance monitor thread started\n";
        
        auto last_report = std::chrono::steady_clock::now();
        const auto report_interval = std::chrono::seconds(30);
        
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            
            if (now - last_report >= report_interval) {
                printPerformanceReport();
                last_report = now;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    TradingSignal generateRandomSignal(
        const std::vector<std::string>& pairs,
        std::uniform_real_distribution<>& strength_dist,
        std::uniform_real_distribution<>& size_dist,
        std::uniform_int_distribution<>& side_dist) {
        
        // Use Binance-format symbols instead of generic format
        std::vector<std::string> binance_pairs = {
            "BTCUSDT", "ETHUSDT", "ADAUSDT", "DOTUSDT"  // No slashes
        };
        
        std::uniform_int_distribution<> pair_dist(0, binance_pairs.size() - 1);
        std::string trading_pair = binance_pairs[pair_dist(signal_generator_)];
        
        TradingSignal signal;
        signal.id = generateSignalId();
        signal.setTradingPair(trading_pair);  // Now uses correct format
        signal.setDirection(side_dist(signal_generator_) == 0 ? Direction::BUY : Direction::SELL);
        signal.setStrength(strength_dist(signal_generator_));
        signal.setTargetSize(size_dist(signal_generator_));
        signal.setTtl(std::chrono::milliseconds(10000));
        signal.setStrategyId(StrategyId("test_strategy"));
        // signal.setStrategyVersion("1.0");
        
        // Rest of your existing code...
        return signal;
    }
    
    std::string generateSignalId() {
        static std::atomic<int> signal_counter{1};
        std::stringstream ss;
        ss << "SIG-" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()
           << "-" << signal_counter++;
        return ss.str();
    }
    
    void removeFromActiveOrders(const std::string& order_id) {
        std::lock_guard<std::mutex> lock(active_orders_mutex_);
        active_order_ids_.erase(
            std::remove(active_order_ids_.begin(), active_order_ids_.end(), order_id),
            active_order_ids_.end());
    }
    
    void printPerformanceReport() {
        std::cout << "\n=== Performance Report ===\n";
        std::cout << "Signals Generated: " << signals_generated_.load() << "\n";
        std::cout << "Orders Placed: " << orders_placed_.load() << "\n";
        std::cout << "Orders Filled: " << orders_filled_.load() << "\n";
        std::cout << "Orders Cancelled: " << orders_cancelled_.load() << "\n";
        std::cout << "Orders Rejected: " << orders_rejected_.load() << "\n";
        
        {
            std::lock_guard<std::mutex> lock(active_orders_mutex_);
            std::cout << "Active Orders: " << active_order_ids_.size() << "\n";
        }
        
        // Calculate success rate
        int total_completed = orders_filled_ + orders_cancelled_ + orders_rejected_;
        if (total_completed > 0) {
            double success_rate = (double)orders_filled_ / total_completed * 100.0;
            std::cout << "Fill Rate: " << std::fixed << std::setprecision(2) 
                      << success_rate << "%\n";
        }
        
        std::cout << "========================\n\n";
    }
    
    void processInteractiveCommand(const std::string& input) {
        if (input == "1" || input == "signal") {
            std::cout << "Generating test signal...\n";
            
            std::uniform_real_distribution<> strength_dist(0.3, 1.0);
            std::uniform_real_distribution<> size_dist(0.001, 1.0);
            std::uniform_int_distribution<> side_dist(0, 1);
            std::vector<std::string> pairs = {"BTC/USDT"};
            
            auto signal = generateRandomSignal(pairs, strength_dist, size_dist, side_dist);
            auto response = execution_service_->executeSignal(signal);
            
            std::cout << "Signal executed - Success: " << response.success 
                      << ", Order ID: " << response.client_order_id << "\n";
            if (!response.success) {
                std::cout << "Error: " << response.error_message << "\n";
            }
        }
        else if (input == "2" || input == "market") {
            std::cout << "Creating market order...\n";

            auto order = models::Order::createMarketOrder(core::Symbol::fromString("BTC/USDT"), Direction::BUY, 0.001);
            auto response = execution_service_->placeOrder(*order);
            
            std::cout << "Market order placed - Success: " << response.success 
                      << ", Order ID: " << response.client_order_id << "\n";
        }
        else if (input == "3" || input == "limit") {
            std::cout << "Creating limit order...\n";

            auto order = models::Order::createLimitOrder(core::Symbol::fromString("BTC/USDT"), Direction::BUY, 0.001, 99900.0);
            auto response = execution_service_->placeOrder(*order);
            
            std::cout << "Limit order placed - Success: " << response.success 
                      << ", Order ID: " << response.client_order_id << "\n";
        }
        else if (input == "4" || input == "status") {
            std::cout << "Enter order ID to check: ";
            std::string order_id;
            std::getline(std::cin, order_id);
            
            auto status_response = execution_service_->getOrderStatus(order_id);
            if (status_response.found) {
                printOrderStatus(status_response.order);
            } else {
                std::cout << "Order not found: " << status_response.error_message << "\n";
            }
        }
        else if (input == "5" || input == "cancel") {
            std::cout << "Enter order ID to cancel: ";
            std::string order_id;
            std::getline(std::cin, order_id);
            
            auto cancel_response = execution_service_->cancelOrder(order_id);
            std::cout << "Cancel request - Success: " << cancel_response.success;
            if (!cancel_response.success) {
                std::cout << ", Error: " << cancel_response.error_message;
            }
            std::cout << "\n";
        }
        else if (input == "6" || input == "health") {
            std::string health_status = execution_service_->getHealthStatus();
            std::cout << "System Health:\n" << health_status << "\n";
        }
        else if (input == "7" || input == "stats") {
            printPerformanceReport();
        }
        else {
            std::cout << "Unknown command: " << input << "\n";
        }
    }
    
    void printOrderStatus(const std::shared_ptr<models::Order>& order) {
        if (!order) {
            std::cout << "Invalid order\n";
            return;
        }
        
        std::cout << "\n=== Order Status ===\n";
        std::cout << "Order ID: " << order->getOrderId() << "\n";
        std::cout << "Client Order ID: " << order->getClientOrderId() << "\n";
        std::cout << "Exchange Order ID: " << order->getExchangeOrderId() << "\n";
        std::cout << "Trading Pair: " << order->getTradingPair() << "\n";
        std::cout << "Side: " << (order->getSide() == Direction::BUY ? "BUY" : "SELL") << "\n";
        std::cout << "Type: " << orderTypeToString(order->getType()) << "\n";
        std::cout << "Quantity: " << order->getQuantity() << "\n";
        std::cout << "Price: " << order->getPrice() << "\n";
        std::cout << "Filled Quantity: " << order->getFilledQuantity() << "\n";
        std::cout << "Remaining Quantity: " << order->getRemainingQuantity() << "\n";
        std::cout << "State: " << orderStateToString(order->getState()) << "\n";
        std::cout << "Strategy ID: " << order->getStrategyId() << "\n";
        std::cout << "===================\n";
    }
    
    std::string orderTypeToString(models::OrderType type) {
        switch (type) {
            case models::OrderType::MARKET: return "MARKET";
            case models::OrderType::LIMIT: return "LIMIT";
            case models::OrderType::STOP: return "STOP";
            case models::OrderType::STOP_LIMIT: return "STOP_LIMIT";
            case models::OrderType::ICEBERG: return "ICEBERG";
            case models::OrderType::TRAILING_STOP: return "TRAILING_STOP";
            case models::OrderType::FILL_OR_KILL: return "FILL_OR_KILL";
            case models::OrderType::IMMEDIATE_OR_CANCEL: return "IMMEDIATE_OR_CANCEL";
            case models::OrderType::POST_ONLY: return "POST_ONLY";
            default: return "UNKNOWN";
        }
    }
    
    std::string orderStateToString(models::OrderState state) {
        switch (state) {
            case models::OrderState::CREATED: return "CREATED";
            case models::OrderState::PENDING: return "PENDING";
            case models::OrderState::PENDING_CANCEL: return "PENDING_CANCEL";
            case models::OrderState::OPEN: return "OPEN";
            case models::OrderState::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
            case models::OrderState::FILLED: return "FILLED";
            case models::OrderState::CANCELED: return "CANCELED";
            case models::OrderState::REJECTED: return "REJECTED";
            case models::OrderState::EXPIRED: return "EXPIRED";
            default: return "UNKNOWN";
        }
    }
};

// ================================================================
// MAIN FUNCTION
// ================================================================

int main(int argc, char* argv[]) {
    std::cout << "Order Execution System Test Application\n";
    std::cout << "======================================\n\n";
    
    ExecutionTestApp app;
    
    // Start the application
    if (!app.start()) {
        std::cerr << "Failed to start test application\n";
        return 1;
    }
    
    // Check for command line arguments
    if (argc > 1 && std::string(argv[1]) == "--interactive") {
        app.runInteractiveMode();
    } else {
        std::cout << "Running in automatic test mode...\n";
        std::cout << "Press Enter to stop and switch to interactive mode, or Ctrl+C to exit\n";
        
        std::cin.get();
        std::cout << "\nSwitching to interactive mode...\n";
        app.runInteractiveMode();
    }
    
    return 0;
}