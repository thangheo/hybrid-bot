/**
 * @file execution_service.hpp with gateway
 * @brief Modified main service interface for the execution engine
 * @brief Modified ExecutionService that uses ExchangeGateway
 * 
 * @details 
 * This is the MINIMAL modification to your existing ExecutionService.
 * Only changes:
 * 1. Added constructor that accepts ExchangeGateway
 * 2. Added private member for gateway
 * 3. All existing methods remain exactly the same
 * 
 * Created on: December 2024
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
#include "execution/balance_validator.hpp"  // NEW: Add balance validator
#include "execution/balance_validator_config.hpp"  // NEW: Add balance validator config
#include "exchange_gateway.hpp"  // NEW: Add gateway header
#include <core/market_types.hpp>

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
 * @brief ExecutionService with ExchangeGateway support
 * 
 * @details 
 * This is the same ExecutionService but with added support for ExchangeGateway.
 * All existing functionality remains identical.
 * */
class ExecutionService {
public:
    // How to handle cases where a LIMIT order is selected but no valid price is available
    enum class LimitOrderHandling {
        REVERT_TO_MARKET,    // Safely convert to MARKET (default)
        REJECT_IF_NO_PRICE   // Reject / throw if LIMIT without a price
    };

    // Configure how LIMIT orders without prices should be handled
    void setLimitOrderHandling(LimitOrderHandling handling) {
        std::lock_guard<std::mutex> lock(mutex_);
        limit_order_handling_ = handling;
    }

    LimitOrderHandling getLimitOrderHandling() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return limit_order_handling_;
    }
    /**
     * @brief NEW Constructor with ExchangeGateway
     * 
     * @param exchangeGateway The centralized exchange gateway
     * @param order_generator Order generator component
     * @param smart_order_router Smart order router component
     * @param order_type_selector Order type selector component
     * @param execution_algorithm_factory Execution algorithm factory
     * @param order_lifecycle_tracker Order lifecycle tracker
     * @param fill_reconciliation Fill reconciliation
     */
    ExecutionService(
        std::shared_ptr<execution::ExchangeGateway> exchangeGateway,   // NEW: Gateway first
        std::shared_ptr<OrderGenerator> order_generator,
        std::shared_ptr<SmartOrderRouter> smart_order_router,
        std::shared_ptr<OrderTypeSelector> order_type_selector,
        std::shared_ptr<ExecutionAlgorithmFactory> execution_algorithm_factory,
        std::shared_ptr<OrderLifecycleTracker> order_lifecycle_tracker,
        std::shared_ptr<FillReconciliation> fill_reconciliation
    );
    
    /**
     * @brief EXISTING Constructor (for backward compatibility)
     * 
     * @param order_generator Order generator component
     * @param smart_order_router Smart order router component
     * @param order_type_selector Order type selector component
     * @param execution_algorithm_factory Execution algorithm factory
     * @param order_lifecycle_tracker Order lifecycle tracker
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
    
    // ALL EXISTING METHODS REMAIN EXACTLY THE SAME - NO CHANGES!
    
    bool initialize();
    void shutdown();
    ExecutionResponse executeSignal(const TradingSignal& signal);
    std::future<ExecutionResponse> executeSignalAsync(const TradingSignal& signal);
    ExecutionResponse placeOrder(const models::Order& order);
    CancelResponse cancelOrder(const std::string& client_order_id);
    std::string orderStateToString(models::OrderState state);
    CancelResponse cancelOrderByExchangeId(const std::string& exchange_order_id, 
                                          const core::ExchangeId& exchangeId);
    std::vector<CancelResponse> cancelAllOrdersForSymbol(const core::Symbol& tradingPair,
                                                        const core::ExchangeId& exchangeId);
    OrderStatusResponse getOrderStatus(const std::string& client_order_id);
    std::vector<models::Order> getActiveOrders();
    std::vector<models::Order> getFilledOrders(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time);
    std::string getHealthStatus() const;
    
    // JSON methods remain the same
    std::string executeSignalJson(const std::string& signal_json);
    // ExecutionResponse ExecutionService::executeSignalAsync(const TradingSignal& signal);

    std::string placeOrderJson(const std::string& order_json);
    std::string cancelOrderJson(const std::string& request_json);
    std::string getOrderStatusJson(const std::string& request_json);
    std::string getActiveOrdersJson(const std::string& request_json = "{}");
    std::string getActiveOrdersByExchangeIdJson(const std::string& request_json);
// getExchangeGateway
    std::shared_ptr<execution::ExchangeGateway> getExchangeGateway();
    // hasSetGateway
    bool hasSetGateway() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return exchangeGateway_ != nullptr;
    }
    // setExchangeGateway
    void setExchangeGateway(std::shared_ptr<execution::ExchangeGateway> gateway) {
        std::lock_guard<std::mutex> lock(mutex_);
        exchangeGateway_ = gateway;
    }
    //registerExecutionCallback
    void registerExecutionCallback(const std::function<void(const ExecutionResponse&)>& callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        execution_callback_ = callback;
    }
private:
    // NEW: Callback for execution reports
    std::function<void(const ExecutionResponse&)> execution_callback_;
    // Helper methods for JSON parsing (unchanged)
    TradingSignal parseTradingSignalFromJson(const std::string& json);
    models::Order parseOrderFromJson(const std::string& json);
    std::string executionResponseToJson(const ExecutionResponse& response);
    std::string cancelResponseToJson(const CancelResponse& response);
    std::string orderStatusResponseToJson(const OrderStatusResponse& response);
    std::string activeOrdersToJson(const std::vector<models::Order>& orders);

    // NEW: ExchangeGateway member (optional)
    std::shared_ptr<execution::ExchangeGateway> exchangeGateway_;   // NEW
    
    // EXISTING: Component references (unchanged)
    std::shared_ptr<OrderGenerator> order_generator_;
    std::shared_ptr<SmartOrderRouter> smart_order_router_;
    std::shared_ptr<OrderTypeSelector> order_type_selector_;
    std::shared_ptr<ExecutionAlgorithmFactory> execution_algorithm_factory_;
    std::shared_ptr<OrderLifecycleTracker> order_lifecycle_tracker_;
    std::shared_ptr<FillReconciliation> fill_reconciliation_;
    std::shared_ptr<ApiClientManager> api_client_manager_;       // EXISTING (can be null if using gateway)
    
    // NEW: Balance validation
    std::unique_ptr<BalanceValidator> balanceValidator_;         // NEW: Balance validator
    
    // EXISTING: Service state (unchanged)
    bool is_initialized_ = false;
    bool is_running_ = false;
    // Policy for handling LIMIT orders without prices
    LimitOrderHandling limit_order_handling_ = LimitOrderHandling::REVERT_TO_MARKET;
    mutable std::mutex mutex_;
    
    // EXISTING: Helper methods (unchanged)
    models::Order createOrderFromSignal(const TradingSignal& signal);
    bool validateOrder(const models::Order& order);
    ExecutionResponse executeOrderWithAlgorithm(const models::Order& order);
    
    // NEW: Helper to get API client (abstracts gateway vs direct access)
    std::shared_ptr<ExchangeApiClient> getExchangeClient(const core::ExchangeId& exchangeId);
};

} // namespace execution
} // namespace trading
