/**
 * @file execution_interface.cpp
 * @brief Implementation of ExecutionInterface with direct ExecutionService integration
 * 
 * This implementation provides direct communication with ExecutionService instead of ZeroMQ.
 * Used by Risk Management System to interact with trading execution.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#include "interfaces/execution_interface.hpp"
#include "execution/execution_service.hpp"
#include "risk_config.hpp"
#include "common/logger.hpp"
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <future>
#include <thread>
#include <execution/exchange_gateway.hpp>
#include <execution/api_client_manager.hpp>
namespace trading {
namespace risk {

/**
 * Implementation of ExecutionInterface using direct ExecutionService calls
 */

class ExecutionInterfaceImpl : public ExecutionInterface {
private:
    // Direct reference to ExecutionService
    std::shared_ptr<trading::execution::ExecutionService> executionService_;
    
    // Optional ExchangeGateway for abstracted exchange access (UNCHANGED)
    std::shared_ptr<common::ExchangeGateway> exchangeGateway_;
    
    // Callback for execution reports
    std::function<void(const ExecutionReport&)> executionReportCallback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Initialization flag
    bool initialized_ = false;

    /**
     * Convert OrderRequest to execution::Order
     */
    std::shared_ptr<execution::models::Order> convertToExecutionOrder(const OrderRequest& request) {
        auto order = std::make_shared<execution::models::Order>(request.clientOrderId);

        order->setExchangeId(request.exchangeId);
        order->setTradingPair(request.tradingPair);
        order->setSide(request.side);  // Fix: Direct assignment of enum

        // Convert string type to OrderType enum
        if (request.type == "market") {
            order->setType(execution::models::OrderType::MARKET);
        } else if (request.type == "limit") {
            order->setType(execution::models::OrderType::LIMIT);
        } else if (request.type == "stop") {
            order->setType(execution::models::OrderType::STOP);
        } else if (request.type == "stop_limit") {
            order->setType(execution::models::OrderType::STOP_LIMIT);
        } else {
            order->setType(execution::models::OrderType::LIMIT); // Default
        }
        
        order->setQuantity(request.quantity);
        order->setPrice(request.price);
        
        // Convert string timeInForce to enum
        if (request.timeInForce == "GTC") {
            order->setTimeInForce(execution::models::TimeInForce::GTC);
        } else if (request.timeInForce == "IOC") {
            order->setTimeInForce(execution::models::TimeInForce::IOC);
        } else if (request.timeInForce == "FOK") {
            order->setTimeInForce(execution::models::TimeInForce::FOK);
        } else {
            order->setTimeInForce(execution::models::TimeInForce::GTC); // Default
        }
        
        order->setStrategyId(request.strategyId);
        order->setSignalId(request.signalId);
        
        // Add extra parameters
        for (const auto& [key, value] : request.params) {
            order->setExtraParam(key, value);
        }
        
        return order;
    }

    /**
     * Convert execution::models::Order to OrderRequest
     */
    OrderRequest convertFromExecutionOrder(const std::shared_ptr<execution::models::Order>& order) {
        OrderRequest request;
        
        request.orderId = order->getOrderId();
        request.clientOrderId = order->getClientOrderId();
        request.exchangeId = order->getExchangeId();
        request.tradingPair = order->getTradingPair();
        request.side = order->getSide();
    

        // Convert OrderType to string
        switch (order->getType()) {
            case execution::models::OrderType::MARKET:
                request.type = "market";
                break;
            case execution::models::OrderType::LIMIT:
                request.type = "limit";
                break;
            case execution::models::OrderType::STOP:
                request.type = "stop";
                break;
            case execution::models::OrderType::STOP_LIMIT:
                request.type = "stop_limit";
                break;
            default:
                request.type = "limit";
        }
        
        request.quantity = order->getQuantity();
        request.price = order->getPrice();
        
        // Convert TimeInForce to string
        switch (order->getTimeInForce()) {
            case execution::models::TimeInForce::GTC:
                request.timeInForce = "GTC";
                break;
            case execution::models::TimeInForce::IOC:
                request.timeInForce = "IOC";
                break;
            case execution::models::TimeInForce::FOK:
                request.timeInForce = "FOK";
                break;
            default:
                request.timeInForce = "GTC";
        }
        
        request.strategyId = order->getStrategyId();
        request.signalId = order->getSignalId();
        request.timestamp = order->getCreatedTime();
        
        // Convert extra params to the expected type
        auto extraParams = order->getExtraParams();
        for (const auto& [key, value] : extraParams) {
            request.params[key] = value;
        }
        
        return request;
    }

    /**
     * Convert execution response to ExecutionReport
     */
    ExecutionReport convertToExecutionReport(const auto& response) {
        ExecutionReport report;
        
        // Fill in basic fields - adjust based on actual response structure
        if constexpr (requires { response.orderId; }) {
            report.orderId = response.orderId;
        }
        if constexpr (requires { response.clientOrderId; }) {
            report.clientOrderId = response.clientOrderId;
        }
        if constexpr (requires { response.exchangeId; }) {
            report.exchangeId = response.exchangeId;
        }
        if constexpr (requires { response.tradingPair; }) {
            report.tradingPair = response.tradingPair;
        }
        if constexpr (requires { response.side; }) {
            report.side = response.side;
        }
        if constexpr (requires { response.quantity; }) {
            report.quantity = response.quantity;
        }
        if constexpr (requires { response.price; }) {
            report.price = response.price;
        }
        if constexpr (requires { response.success; }) {
            report.status = response.success ? "FILLED" : "REJECTED";
        } else {
            report.status = "UNKNOWN";
        }
        
        report.timestamp = std::chrono::system_clock::now();
        
        return report;
    }

public:
    /**
     * Constructor
     */
    explicit ExecutionInterfaceImpl(
        std::shared_ptr<trading::execution::ExecutionService> executionService,
        std::shared_ptr<common::ExchangeGateway> exchangeGateway = nullptr)
        : executionService_(std::move(executionService))
        , exchangeGateway_(std::move(exchangeGateway)) {
        
        if (!executionService_) {
            throw std::invalid_argument("ExecutionService cannot be null");
        }
        
        // If no gateway provided, try to extract it from ExecutionService
        if (!exchangeGateway_ && executionService_) {
            // This assumes your ExecutionService has a method to get the gateway
            // You'll need to add this method to ExecutionService if it doesn't exist
            exchangeGateway_ = executionService_->getExchangeGateway();
        }
    }
    /**
     * EXISTING Constructor (for backward compatibility)
     * Keeps your old code working unchanged
     */
    explicit ExecutionInterfaceImpl(
        std::shared_ptr<trading::execution::ExecutionService> executionService)
        : executionService_(std::move(executionService))
        , exchangeGateway_(nullptr) {
        
        if (!executionService_) {
            throw std::invalid_argument("ExecutionService cannot be null");
        }
    }
     /**
     * Alternative Constructor: Gateway-first approach
     * For when you want to use gateway directly for some operations
     */
    explicit ExecutionInterfaceImpl(
        std::shared_ptr<common::ExchangeGateway> exchangeGateway)
        : executionService_(nullptr)
        , exchangeGateway_(std::move(exchangeGateway)) {
        
        if (!exchangeGateway_) {
            throw std::invalid_argument("ExchangeGateway cannot be null");
        }
        
        // Extract ExecutionService from the gateway's ApiClientManager if needed
        // This depends on your specific architecture
    }
    

    /**
     * Destructor
     */
    ~ExecutionInterfaceImpl() override = default;
    //getter for exchangeGateway (for testing/migration purposes)
    std::shared_ptr<common::ExchangeGateway> getExchangeGateway() const {
        return exchangeGateway_;
    }

    // Add getter for execution service (unchanged)
    std::shared_ptr<trading::execution::ExecutionService> getExecutionService() const {
        return executionService_;
    }

    // === Core Interface Methods ===

    bool initialize() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (initialized_) {
            return true;
        }
        
        try {
            // Verify ExecutionService is available
            if (!executionService_) {
                LOG_INFO("ERROR", "ExecutionService is not available");
                return false;
            }
            
            // Register for execution reports if callback is set
            if (executionReportCallback_) {
                // Note: This assumes ExecutionService has a method to register callbacks
                // executionService_->registerExecutionReportCallback(executionReportCallback_);
            }
            
            initialized_ = true;
            LOG_INFO("INFO", "ExecutionInterface initialized successfully with direct ExecutionService");
            return true;
            
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to initialize ExecutionInterface: " + std::string(e.what()));
            return false;
        }
    }

    bool isConnected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && executionService_ != nullptr;
    }

    // === Order Management ===
    /** 
     * 
    exchange_order->setOrderId(order.getOrderId());
    exchange_order->setOrderId(order.getClientOrderId());
    exchange_order->setTradingPair(order.getTradingPair());
    exchange_order->setSide(order.getSide());
    exchange_order->setType(order.getType());
    exchange_order->setQuantity(order.getQuantity());
    exchange_order->setPrice(order.getPrice());
    exchange_order->setStopPrice(order.getStopPrice());
    exchange_order->setTimeInForce(order.getTimeInForce());
    exchange_order->setExpiryTime(order.getExpiryTime());
    exchange_order->setStrategyId(order.getStrategyId());
    exchange_order->setSignalId(order.getSignalId());
    exchange_order->setExchangeId(exchangeId);
    exchange_order->setExchangeOrderId(order.getExchangeOrderId());
    exchange_order->setState(order.getState());
    **/
//    ExecutionResult ExecutionInterfaceImpl::submitOrder(const OrderRequest& request) {
//     try {
//         // Convert request to order (your existing logic)
//         auto order = createOrderFromRequest(request);
        
//         if (exchangeGateway_) {
//             // NEW: Use gateway
//             auto future = exchangeGateway_->placeOrder(order);
//             auto report = future.get();
            
//             // Trigger callback if set
//             if (executionReportCallback_) {
//                 executionReportCallback_(convertToExecutionReport(report));
//             }
            
//             return convertToExecutionResult(report);
//         } else {
//             // EXISTING: Use ExecutionService
//             auto signal = convertOrderToSignal(order);  // Your existing logic
//             auto response = executionService_->executeSignal(signal);
            
//             // Trigger callback if set (your existing logic)
//             if (executionReportCallback_) {
//                 executionReportCallback_(convertToExecutionReport(response));
//             }
            
//             return convertToExecutionResult(response);
//         }
        
//     } catch (const std::exception& e) {
//         return ExecutionResult{false, e.what()};
//     }
// }
    /**
     * Submit a new order to the execution engine
     * @param orderRequest Order details
     * @return Order ID if successful, empty string if failed
     */
    std::string submitOrder(const OrderRequest& orderRequest) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return "";
        }
        
        try {
            auto order = convertToExecutionOrder(orderRequest);
            if(exchangeGateway_){
                // Use the gateway if available
                auto future = exchangeGateway_->placeOrder(*order);
                auto report = future.get();
                
                // Trigger callback if set
                if (executionReportCallback_) {
                    executionReportCallback_(convertToExecutionReport(report));
                }
                
                return report.orderId();
            } else {
                // Use ExecutionService directly
                auto response = executionService_->placeOrder(*order);
                auto success = response.success;
                // Trigger callback if set
                if (executionReportCallback_) {
                    executionReportCallback_(convertToExecutionReport(response));
                }
                
                return response.success ? response.client_order_id : "";
            }
            
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to submit order: " + std::string(e.what()));
            return "";
        }
    }

    bool cancelOrder(const std::string& orderId) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return false;
        }
        
        try {
            auto response = executionService_->cancelOrder(orderId);
            return response.success;
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to cancel order: " + std::string(e.what()));
            return false;
        }
    }

    int cancelAllOrders(const core::ExchangeId& exchangeId) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return 0;
        }
        
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
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return "{}";
        }
        
        try {
            return executionService_->getOrderStatusJson(orderId);
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get order status: " + std::string(e.what()));
            return "{}";
        }
    }

    std::vector<OrderRequest> getOpenOrders(const std::optional<std::string>& exchangeId) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return {};
        }
        
        try {
            // Get active orders from ExecutionService
            auto orders = executionService_->getActiveOrders();
            
            // Filter by exchangeId if provided
            std::vector<execution::models::Order> filteredOrders;
            if (exchangeId.has_value()) {
                std::copy_if(orders.begin(), orders.end(), std::back_inserter(filteredOrders),
                    [&exchangeId](const execution::models::Order& order) {
                        return order.getExchangeId().toString() == *exchangeId;
                    });
            } else {
                filteredOrders = orders;
            }
            
            // Convert to OrderRequest format
            std::vector<OrderRequest> requests;
            requests.reserve(filteredOrders.size());
            
            for (const auto& order : filteredOrders) {
                // Create shared_ptr for convertFromExecutionOrder
                auto orderPtr = std::make_shared<execution::models::Order>(order);
                requests.push_back(convertFromExecutionOrder(orderPtr));
            }
            
            return requests;
            
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get open orders: " + std::string(e.what()));
            return {};
        }
    }

    // === Account & Balance Management ===

    std::map<std::string, double> getAccountBalances(const core::ExchangeId& exchangeId) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return {};
        }
        
        try {
            // Call ExecutionService directly
            std::cout << "Not implemented! Getting account balances for exchange: " << exchangeId.toString() << std::endl;
            // return executionService_->getAccountBalances(exchangeId);
            return {}; // Placeholder
            
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get account balances: " + std::string(e.what()));
            return {};
        }
    }

    std::future<std::map<std::string, double>> getAccountBalancesAsync(
        const core::ExchangeId& exchangeId) override {
        
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            
            // Return a future with empty result
            std::promise<std::map<std::string, double>> promise;
            promise.set_value({});
            return promise.get_future();
        }
        
        try {
            if (exchangeGateway_) {
                // Use the gateway if available
                auto future_unordered = exchangeGateway_->getAccountBalancesAsync(exchangeId);
                
                // Create a promise/future pair for the converted result
                auto promise = std::make_shared<std::promise<std::map<std::string, double>>>();
                auto future_result = promise->get_future();
                
                // Launch async task to convert unordered_map to map
                std::thread([promise, future_unordered = std::move(future_unordered)]() mutable {
                    try {
                        auto unordered_result = future_unordered.get();
                        std::map<std::string, double> map_result(unordered_result.begin(), unordered_result.end());
                        promise->set_value(std::move(map_result));
                    } catch (...) {
                        promise->set_exception(std::current_exception());
                    }
                }).detach();
                
                return future_result;
            }
            else {
                // Fallback to ExecutionService (if it had async support)
                // Here we simulate async by using std::async
                return std::async(std::launch::async, [this, exchangeId]() {
                    return this->getAccountBalances(exchangeId);
                });
            }
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get account balances async: " + std::string(e.what()));

            // Return a future with empty result
            std::promise<std::map<std::string, double>> promise;
            promise.set_value({});
            return promise.get_future();
        }
    }
    //         // Get the future with unordered_map and convert to map
    //         auto future_unordered = exchangeGateway_->getAccountBalancesAsync(exchangeId);
            
    //         // Create a promise/future pair for the converted result
    //         auto promise = std::make_shared<std::promise<std::map<std::string, double>>>();
    //         auto future_result = promise->get_future();
            
    //         // Launch async task to convert unordered_map to map
    //         std::thread([promise, future_unordered = std::move(future_unordered)]() mutable {
    //             try {
    //                 auto unordered_result = future_unordered.get();
    //                 std::map<std::string, double> map_result(unordered_result.begin(), unordered_result.end());
    //                 promise->set_value(std::move(map_result));
    //             } catch (...) {
    //                 promise->set_exception(std::current_exception());
    //             }
    //         }).detach();
            
    //         return future_result;
    //     } catch (const std::exception& e) {
    //         LOG_INFO("ERROR", "Failed to get account balances async: " + std::string(e.what()));
            
    //         // Return a future with empty result
    //         std::promise<std::map<std::string, double>> promise;
    //         promise.set_value({});
    //         return promise.get_future();
    //     }
    // }

    // === Exchange Status ===

    bool isExchangeAvailable(const core::ExchangeId& exchangeId) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return false;
        }
        
        try {

            // return executionService_->isExchangeAvailable(exchangeId);
            if(exchangeGateway_){
                return true; // hardcoded for now
            }else{
                std::cout << "Not implemented! Checking exchange availability for: " << exchangeId.toString() << std::endl;
                return false; // Placeholder
            }
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to check exchange availability: " + std::string(e.what()));
            return false;
        }
    }

    std::string getExchangeStatus(const core::ExchangeId& exchangeId) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return "{}";
        }
        
        try {
            // return executionService_->getExchangeStatusJson(exchangeId);
            if(exchangeGateway_){
                return "{}"; // hardcoded for now
            }else{
                std::cout << "Not implemented! Getting exchange status for: " << exchangeId.toString() << std::endl;
                return "{}"; // Placeholder
            }
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get exchange status: " + std::string(e.what()));
            return "{}";
        }
    }

    // === Execution Reports ===

    void registerExecutionReportCallback(
        const std::function<void(const ExecutionReport&)>& callback) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        executionReportCallback_ = callback;
        
        // If already initialized, register with ExecutionService
        if (initialized_ && executionService_) {
            // Note: This assumes ExecutionService has a method to register callbacks
            // executionService_->registerExecutionReportCallback(callback);
        }
    }

    // === Position Management ===
    // convertBalancesToPositions
    std::map<std::string, double> convertBalancesToPositions(const std::unordered_map<std::string, double>& balances) {
        // Placeholder conversion logic
        std::map<std::string, double> positions;
        for (const auto& [asset, amount] : balances) {
            positions[asset] = amount; // Simple 1:1 mapping for example
        }
        return positions;
    }
    std::map<std::string, double> getPositions(const core::ExchangeId& exchangeId) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return {};
        }
        
        try {
            // return executionService_->getPositions(exchangeId);
            if (exchangeGateway_) {
            // NEW: Get balances through gateway (cached)
            auto balancesFuture = exchangeGateway_->getAccountBalances(exchangeId);
            auto balances = balancesFuture.get();
            return convertBalancesToPositions(balances);
            } else {
                // EXISTING: Fallback to ExecutionService (if it had position support)
                std::cout << "Not implemented! Getting positions for exchange: " << exchangeId.toString() << std::endl;
                return {}; // Placeholder
            }
        }  catch (const std::exception& e) {
                LOG_INFO("ERROR", "Failed to get positions: " + std::string(e.what()));
                throw; // rethrow for caller to handle
        }
    }

    

    std::map<std::string, double> getPositions(const core::ExchangeId& exchangeId) override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return {};
        }

        try {
            // return executionService_->getPositions(exchangeId);
            if (exchangeGateway_) {
                // NEW: Get balances through gateway (cached)
                auto balancesFuture = exchangeGateway_->getAccountBalances(exchangeId);
                auto balances = balancesFuture.get();
                return convertBalancesToPositions(balances);
            } else {
                // EXISTING: Fallback to ExecutionService (if it had position support)
                std::cout << "Not implemented! Getting positions for exchange: " << exchangeId.toString() << std::endl;
                return {}; // Placeholder
            }
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get positions: " + std::string(e.what()));
            return {};
        }
    }

    std::map<core::ExchangeId, std::map<std::string, double>> getAllPositions() override {
        if (!initialized_ || !executionService_) {
            LOG_INFO("ERROR", "ExecutionInterface not initialized");
            return {};
        }
        
        try {
            std::map<core::ExchangeId, std::map<std::string, double>> allPositions;
            if (exchangeGateway_) {
                // NEW: Get balances for all exchanges through gateway
                auto supportedExchanges = core::ExchangeId::getAllSupported();
                for (const auto& exchangeId : supportedExchanges) {
                    auto balancesFuture = exchangeGateway_->getAccountBalances(exchangeId);
                    auto balances = balancesFuture.get();
                    allPositions[exchangeId] = convertBalancesToPositions(balances);
                }
            } else {
                // EXISTING: Fallback to ExecutionService (if it had position support)
                std::cout << "Not implemented! Getting all positions." << std::endl;
                return {}; // Placeholder
            }
            return allPositions;
        } catch (const std::exception& e) {
            LOG_INFO("ERROR", "Failed to get all positions: " + std::string(e.what()));
            return {};
        }
    }
};

// === Factory Function ===

std::unique_ptr<ExecutionInterface> createExecutionInterface(
    const RiskConfig& config,
    std::shared_ptr<trading::execution::ExecutionService> executionService) {
    
    // Suppress unused parameter warning for config
    (void)config;
    
    return std::make_unique<ExecutionInterfaceImpl>(std::move(executionService));
}

} // namespace risk
} // namespace trading