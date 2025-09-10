/**
 * @file execution_service.hpp
 * @brief Main service interface for the execution engine
 * 
 * @details 
 * Provides the main service interface for the execution engine,
 * coordinating between different components to execute trading operations.
 * 
 * Created on: May 5, 2025
 */
#pragma once

#include "execution/models/order.hpp"
#include "execution/order_generator.hpp"
#include "execution/smart_order_router.hpp"
#include "execution/order_type_selector.hpp"
#include "execution/execution_algorithm_factory.hpp"
#include "execution/execution_forward_decl.hpp"
#include <core/market_types.hpp>

#include "execution/order_lifecycle_tracker.hpp"
#include "execution/fill_reconciliation.hpp"
#include "execution/api_client_manager.hpp"
#include <core/market_types.hpp>
// #include "execution/execution_types.hpp"

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <future>

namespace trading {
namespace execution {
    using namespace core;
/**
 * @brief ExecutionService is the main service interface for the execution engine
 * 
 * @details 
 * This class coordinates
 * between different components to execute trading operations,
 * manage orders, and handle fills.
 * It provides methods for placing, canceling, and querying orders
 * 
 * as well as executing trading signals.
 * 
 * */
class ExecutionService {
public:
    /**
     * @brief Constructor
     * 
     * @param order_generator models::Order generator component
     * @param smart_order_router Smart order router component
     * @param order_type_selector models::Order type selector component
     * @param execution_algorithm_factory Execution algorithm factory
     * @param order_lifecycle_tracker models::Order lifecycle tracker
     * @param fill_reconciliation Fill reconciliation
     * @param api_client_manager API client manager
     */
    ExecutionService(
        std::shared_ptr<OrderGenerator> order_generator,
        std::shared_ptr<SmartOrderRouter> smart_order_router,
        std::shared_ptr<OrderTypeSelector> order_type_selector,
        std::shared_ptr<ExecutionAlgorithmFactory> execution_algorithm_factory,
        std::shared_ptr<OrderLifecycleTracker> order_lifecycle_tracker,
        std::shared_ptr<FillReconciliation> fill_reconciliation,
        std::shared_ptr<ApiClientManager> api_client_manager
    );
    
    /**
     * @brief Destructor
     */
    ~ExecutionService();
    
    /**
     * @brief Initialize the service
     * 
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Shutdown the service
     */
    void shutdown();
    
    /**
     * @brief Execute a trading signal
     * 
     * @param signal The trading signal to execute
     * @return ExecutionResponse containing execution status and details
     */
    ExecutionResponse executeSignal(const TradingSignal& signal);
    
    /**
     * @brief Execute a trading signal asynchronously
     * 
     * @param signal The trading signal to execute
     * @return std::future<ExecutionResponse> Future for the execution response
     */
    std::future<ExecutionResponse> executeSignalAsync(const TradingSignal& signal);
    
    /**
     * @brief Place an order directly
     * 
     * @param order The order to place
     * @return ExecutionResponse containing execution status and details
     */
    ExecutionResponse placeOrder(const models::Order& order);
    
    /**
     * @brief Cancel an order
     * 
     * @param client_order_id The client order ID to cancel
     * @return CancelResponse containing cancellation status and details
     */
    CancelResponse cancelOrder(const std::string& client_order_id);
    std::string orderStateToString(models::OrderState state) ;

    /**
     * @brief Cancel an order by exchange order ID
     * 
     * @param exchange_order_id The exchange order ID to cancel
     * @param exchangeId The exchange ID
     * @return CancelResponse containing cancellation status and details
     */
    CancelResponse cancelOrderByExchangeId(const std::string& exchange_order_id, 
                                          const core::ExchangeId& exchangeId);
    
    /**
     * @brief Cancel all orders for a trading pair
     * 
     * @param tradingPair The trading pair
     * @param exchangeId Optional exchange ID to filter by
     * @return Vector of CancelResponse for each order
     */
    std::vector<CancelResponse> cancelAllOrdersForSymbol(const core::Symbol& tradingPair,
                                                        const core::ExchangeId& exchangeId);
    
    /**
     * @brief Get order status
     * 
     * @param client_order_id The client order ID
     * @return OrderStatusResponse containing order status and details
     */
    OrderStatusResponse getOrderStatus(const std::string& client_order_id);
    
    /**
     * @brief Get all active orders
     * 
     * @return Vector of active orders
     */
    std::vector<models::Order> getActiveOrders();
    
    /**
     * @brief Get all filled orders within a time window
     * 
     * @param start_time Start time
     * @param end_time End time
     * @return Vector of filled orders
     */
    std::vector<models::Order> getFilledOrders(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time);
    
    /**
     * @brief Get health status of execution service
     * 
     * @return Health status as a JSON string
     */
    std::string getHealthStatus() const;
    //===========================================
    // Add these to the public section of ExecutionService class
    /**
     * @brief Execute a trading signal from JSON
     * 
     * @param signal_json JSON string containing the trading signal
     * @return JSON string containing the execution response
     */
    std::string executeSignalJson(const std::string& signal_json);

    /**
     * @brief Place an order from JSON
     * 
     * @param order_json JSON string containing the order
     * @return JSON string containing the execution response
     */
    std::string placeOrderJson(const std::string& order_json);

    /**
     * @brief Cancel an order from JSON
     * 
     * @param request_json JSON string containing the cancel request
     * @return JSON string containing the cancel response
     */
    std::string cancelOrderJson(const std::string& request_json);

    /**
     * @brief Get order status as JSON
     * 
     * @param request_json JSON string containing the status request
     * @return JSON string containing the order status response
     */
    std::string getOrderStatusJson(const std::string& request_json);

    /**
     * @brief Get active orders as JSON
     * 
     * @param request_json JSON string containing any filters (optional)
     * @return JSON string containing the active orders
     */
    std::string getActiveOrdersJson(const std::string& request_json = "{}");

    private:
    // Add these helper methods to private section
    TradingSignal parseTradingSignalFromJson(const std::string& json);
    models::Order parseOrderFromJson(const std::string& json);
    std::string executionResponseToJson(const ExecutionResponse& response);
    std::string cancelResponseToJson(const CancelResponse& response);
    std::string orderStatusResponseToJson(const OrderStatusResponse& response);
    std::string activeOrdersToJson(const std::vector<models::Order>& orders);
private:
    // Component references
    std::shared_ptr<OrderGenerator> order_generator_;
    std::shared_ptr<SmartOrderRouter> smart_order_router_;
    std::shared_ptr<OrderTypeSelector> order_type_selector_;
    std::shared_ptr<ExecutionAlgorithmFactory> execution_algorithm_factory_;
    std::shared_ptr<OrderLifecycleTracker> order_lifecycle_tracker_;
    std::shared_ptr<FillReconciliation> fill_reconciliation_;
    std::shared_ptr<ApiClientManager> api_client_manager_;
    
    // Service state
    bool is_initialized_ = false;
    bool is_running_ = false;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Helper methods
    models::Order createOrderFromSignal(const TradingSignal& signal);
    bool validateOrder(const models::Order& order);
    ExecutionResponse executeOrderWithAlgorithm(const models::Order& order);
};

} // namespace execution
} // namespace trading