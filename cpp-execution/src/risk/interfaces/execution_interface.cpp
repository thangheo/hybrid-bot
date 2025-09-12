/**
 * @file simplified_execution_interface.cpp
 * @brief Simplified ExecutionInterface - single source of truth
 * 
 * PRINCIPLE: ExecutionService is the ONLY way to interact with trading.
 * No direct gateway access from this interface.
 */

#include "interfaces/execution_interface.hpp"
#include "execution/execution_service.hpp"
#include "risk_config.hpp"
#include "common/logger.hpp"
#include <chrono>
#include <mutex>
#include <stdexcept>

namespace trading {
namespace risk {

class ExecutionInterfaceImpl : public ExecutionInterface {
private:
    // SINGLE source of truth - ExecutionService handles everything
    std::shared_ptr<trading::execution::ExecutionService> executionService_;
    
    // Callback for execution reports
    std::function<void(const ExecutionReport&)> executionReportCallback_;
    
    // Thread safety
    mutable std::mutex mutex_;
    bool initialized_ = false;

    // === CONVERSION HELPERS ===
    std::shared_ptr<execution::models::Order> convertToExecutionOrder(const OrderRequest& request) {
        auto order = std::make_shared<execution::models::Order>(request.clientOrderId);
        
        order->setExchangeId(request.exchangeId);
        order->setTradingPair(request.tradingPair);
        order->setSide(request.side);
        order->setType(request.type);
        order->setQuantity(request.quantity);
        order->setPrice(request.price);
        order->setStrategyId(request.strategyId);
        order->setSignalId(request.signalId);
        
        // Convert timeInForce string to enum
        if (request.timeInForce == "IOC") {
            order->setTimeInForce(execution::models::TimeInForce::IOC);
        } else if (request.timeInForce == "FOK") {
            order->setTimeInForce(execution::models::TimeInForce::FOK);
        } else {
            order->setTimeInForce(execution::models::TimeInForce::GTC);
        }
        
        return order;
    }
    
    OrderRequest convertFromExecutionOrder(const execution::models::Order& order) {
        OrderRequest request;
        request.orderId = order.getOrderId();
        request.clientOrderId = order.getClientOrderId();
        request.exchangeId = order.getExchangeId();
        request.tradingPair = order.getTradingPair();
        request.side = order.getSide();
        request.type = order.getType();
        request.quantity = order.getQuantity();
        request.price = order.getPrice();
        request.strategyId = order.getStrategyId();
        request.signalId = order.getSignalId();
        request.timestamp = order.getCreatedTime();
        
        // Convert TimeInForce enum to string
        switch (order.getTimeInForce()) {
            case execution::models::TimeInForce::IOC: request.timeInForce = "IOC"; break;
            case execution::models::TimeInForce::FOK: request.timeInForce = "FOK"; break;
            default: request.timeInForce = "GTC";
        }
        
        return request;
    }

public:
    // SINGLE constructor - simple and clear
    explicit ExecutionInterfaceImpl(std::shared_ptr<trading::execution::ExecutionService> executionService)
        : executionService_(std::move(executionService)) {
        
        if (!executionService_) {
            throw std::invalid_argument("ExecutionService cannot be null");
        }
    }

    ~ExecutionInterfaceImpl() override = default;

    // === CORE INTERFACE ===
    bool initialize() override {
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = (executionService_ != nullptr);
        return initialized_;
    }

    bool isConnected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && executionService_ != nullptr;
    }

    // === ORDER MANAGEMENT ===
    std::string submitOrder(const OrderRequest& orderRequest) override {
        if (!initialized_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return "";
        }
        
        try {
            auto order = convertToExecutionOrder(orderRequest);
            auto response = executionService_->placeOrder(*order);
            
            if (executionReportCallback_) {
                ExecutionReport report;
                report.setOrderId(response.client_order_id);
                report.setStatus(response.success ? trading::execution::models::OrderState::FILLED : trading::execution::models::OrderState::REJECTED);
                // report.setTimestamp(std::chrono::system_clock::now());
                executionReportCallback_(report);
            }
            
            return response.success ? response.client_order_id : "";
            
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to submit order: " + std::string(e.what()));
            return "";
        }
    }

    bool cancelOrder(const std::string& orderId) override {
        if (!initialized_) return false;
        
        try {
            auto response = executionService_->cancelOrder(orderId);
            return response.success;
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to cancel order: " + std::string(e.what()));
            return false;
        }
    }

    int cancelAllOrders(const core::ExchangeId& exchangeId) override {
        if (!initialized_) return 0;
        
        try {
            auto openOrders = executionService_->getActiveOrders();
            int cancelCount = 0;
            
            for (const auto& order : openOrders) {
                if (order.getExchangeId().toString() == exchangeId.toString()) {
                    auto response = executionService_->cancelOrder(order.getOrderId());
                    if (response.success) {
                        cancelCount++;
                    }
                }
            }
            return cancelCount;
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to cancel all orders: " + std::string(e.what()));
            return 0;
        }
    }

    std::string getOrderStatus(const std::string& orderId) override {
        if (!initialized_) return "{}";
        
        try {
            return executionService_->getOrderStatusJson(orderId);
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get order status: " + std::string(e.what()));
            return "{}";
        }
    }

    std::vector<OrderRequest> getOpenOrders(const std::optional<std::string>& exchangeId) override {
        if (!initialized_) return {};
        
        try {
            auto orders = executionService_->getActiveOrders();
            std::vector<OrderRequest> requests;
            
            for (const auto& order : orders) {
                if (!exchangeId.has_value() || order.getExchangeId().toString() == *exchangeId) {
                    requests.push_back(convertFromExecutionOrder(order));
                }
            }
            
            return requests;
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get open orders: " + std::string(e.what()));
            return {};
        }
    }

    // === ACCOUNT & BALANCE - Delegate to ExecutionService ===
    std::map<std::string, double> getAccountBalances(const core::ExchangeId& exchangeId) override {
        if (!initialized_) return {};
        
        try {
            // Get gateway from ExecutionService and use it
            auto gateway = executionService_->getExchangeGateway();
            if (!gateway) {
                LOG_INFO("ERROR", "ExecutionService has no gateway");
                return {};
            }
            
            auto future = gateway->getAccountBalances(exchangeId);
            auto balances = future.get();
            
            // Convert unordered_map to map
            return std::map<std::string, double>(balances.begin(), balances.end());
            
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get account balances: " + std::string(e.what()));
            return {};
        }
    }

    std::future<std::map<std::string, double>> getAccountBalancesAsync(
        const core::ExchangeId& exchangeId) override {
        
        return std::async(std::launch::async, [this, exchangeId]() {
            return getAccountBalances(exchangeId);
        });
    }
    // Add this method to ExecutionInterfaceImpl class
    int cancelAllOrders(const core::ExchangeId& exchangeId, 
                       const core::Symbol& tradingPair) override {
        if (!initialized_) return 0;
        
        try {
            auto openOrders = executionService_->getActiveOrders();
            int cancelCount = 0;
            
            for (const auto& order : openOrders) {
                // Filter by both exchangeId AND tradingPair
                if (order.getExchangeId().toString() == exchangeId.toString() &&
                    order.getTradingPair().toString() == tradingPair.toString()) {
                    
                    auto response = executionService_->cancelOrder(order.getOrderId());
                    if (response.success) {
                        cancelCount++;
                    }
                }
            }
            return cancelCount;
            
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to cancel orders for " + 
                     exchangeId.toString() + "/" + tradingPair.toString() + 
                     ": " + std::string(e.what()));
            return 0;
        }
    }
    
    // === EXCHANGE STATUS - Delegate to ExecutionService ===
    bool isExchangeAvailable(const core::ExchangeId& exchangeId) override {
        if (!initialized_) return false;
        
        try {
            auto gateway = executionService_->getExchangeGateway();
            return gateway && gateway->isHealthy();
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to check exchange availability: " + std::string(e.what()));
            return false;
        }
    }

    std::string getExchangeStatus(const core::ExchangeId& exchangeId) override {
        if (!initialized_) return "{}";
        
        try {
            auto gateway = executionService_->getExchangeGateway();
            if (!gateway) return "{}";
            
            return gateway->getStatus();
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get exchange status: " + std::string(e.what()));
            return "{}";
        }
    }

    // === POSITION MANAGEMENT - Delegate to ExecutionService ===
    std::map<std::string, double> getPositions(const core::ExchangeId& exchangeId) override {
        // For now, positions = balances (simple approach)
        return getAccountBalances(exchangeId);
    }

    std::map<core::ExchangeId, std::map<std::string, double>> getAllPositions() override {
        if (!initialized_) return {};
        
        try {
            std::map<core::ExchangeId, std::map<std::string, double>> allPositions;
            auto supportedExchanges = core::ExchangeId::getAllSupported();
            
            for (const auto& exchangeId : supportedExchanges) {
                auto positions = getPositions(exchangeId);
                if (!positions.empty()) {
                    allPositions[exchangeId] = positions;
                }
            }
            
            return allPositions;
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get all positions: " + std::string(e.what()));
            return {};
        }
    }

    // === EXECUTION REPORTS ===
    void registerExecutionReportCallback(
        const std::function<void(const ExecutionReport&)>& callback) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        executionReportCallback_ = callback;
    }
};
std::unique_ptr<ExecutionInterface> createExecutionInterface(const RiskConfig& config) {
    (void)config;
    
    try {
        // Create all required dependencies for ExecutionService
        auto apiManager = std::make_shared<execution::ApiClientManager>();
        auto gateway = std::make_shared<ExchangeGateway>(apiManager);
        
        // Create ExecutionService dependencies
        auto orderGenerator = std::make_shared<trading::execution::OrderGenerator>();
        auto smartRouter = std::make_shared<trading::execution::SmartOrderRouter>();
        // order_generator
        auto order_type_selector = std::make_shared<trading::execution::OrderTypeSelector>();
        auto execution_algorithm_factory = std::make_shared<trading::execution::ExecutionAlgorithmFactory>();
        auto order_lifecycle_tracker = std::make_shared<trading::execution::OrderLifecycleTracker>();
        auto fill_reconciliation = std::make_shared<trading::execution::FillReconciliation>();  
        // Add other required dependencies based on ExecutionService constructor
    //     ExecutionService(
    //     std::shared_ptr<common::ExchangeGateway> exchangeGateway,   // NEW: Gateway first
    //     std::shared_ptr<OrderGenerator> order_generator,
    //     std::shared_ptr<SmartOrderRouter> smart_order_router,
    //     std::shared_ptr<OrderTypeSelector> order_type_selector,
    //     std::shared_ptr<ExecutionAlgorithmFactory> execution_algorithm_factory,
    //     std::shared_ptr<OrderLifecycleTracker> order_lifecycle_tracker,
    //     std::shared_ptr<FillReconciliation> fill_reconciliation
    // );
        // create constructors for other dependencies as needed
        // Create ExecutionService with proper constructor
        auto executionService = std::make_shared<trading::execution::ExecutionService>(
            gateway,
            orderGenerator,
            smartRouter,
            order_type_selector,
            execution_algorithm_factory,
            order_lifecycle_tracker,
            fill_reconciliation
            // Add other parameters as needed
        );
        
        // Set gateway if ExecutionService has a setter method
        if (executionService->hasSetGateway()) {
            executionService->setExchangeGateway(gateway);
        }
        
        return std::make_unique<ExecutionInterfaceImpl>(executionService);
        
    } catch (const std::exception& e) {
        LOG_INFO("ERROR", "Failed to create ExecutionInterface: " + std::string(e.what()));
        return nullptr;
    }
}

} // namespace risk  
} // namespace trading