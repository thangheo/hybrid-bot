/**
 * Interface for communication with the execution engine
 *
 * File: execution_interface.hpp
 * Created: 2025-04-29
 */

#pragma once

#include "risk_types.hpp"
#include "execution/models/execution_report.hpp"
#include <risk/risk_types.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>

namespace trading {
namespace risk {

// Forward declarations
struct OrderRequest;
using ExecutionReport = execution::models::ExecutionReport;
// struct ExecutionReport;

// /**
//  * Execution report structure for risk interface
//  */
// struct ExecutionReport {
//     std::string orderId;
//     std::string clientOrderId;
//     std::string exchangeOrderId;
//     ExchangeId exchangeId;
//     std::string tradingPair;
//     OrderSide side;
//     std::string type;
//     std::string status;
//     double quantity;
//     double filledQuantity;
//     double price;
//     double averageFillPrice;
//     Timestamp timestamp;
//     double fee;
//     std::string feeCurrency;
//     std::string rejectReason;
    
//     ExecutionReport() 
//         : side(OrderSide::BUY), quantity(0.0), filledQuantity(0.0), 
//           price(0.0), averageFillPrice(0.0), fee(0.0) {}
// };

/**
 * Interface for communication with the execution engine
 */
class ExecutionInterface {
public:
    /**
     * Constructor
     */
    ExecutionInterface();
    virtual std::map<std::string, double> getAccountBalances(const core::ExchangeId& exchangeId) = 0;
    // virtual std::future<std::map<std::string, double>> getAccountBalancesAsync(const ExchangeId& exchangeId) = 0;
    /**
     * Virtual destructor
     */
    virtual ~ExecutionInterface() = default;
    
    /**
     * Initialize the execution interface
     * 
     * @return true if successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * Submit an order for execution
     * 
     * @param orderRequest The order request
     * @return Order ID if successful, empty string otherwise
     */
    virtual std::string submitOrder(const OrderRequest& orderRequest) = 0;
    // isConnected
    virtual bool isConnected() const = 0;
    /**
     * Cancel an order
     * 
     * @param orderId The order ID to cancel
     * @return true if successful, false otherwise
     */
    virtual bool cancelOrder(const std::string& orderId) = 0;
    
    /**
     * Cancel all orders for an exchange
     * 
     * @param exchangeId Exchange ID
     * @return Number of orders cancelled
     */
    virtual int cancelAllOrders(const core::ExchangeId& exchangeId) = 0;
    
    /**
     * Cancel all orders for an exchange and trading pair
     * 
     * @param exchangeId Exchange ID
     * @param tradingPair Trading pair
     * @return Number of orders cancelled
     */
    virtual int cancelAllOrders(const core::ExchangeId& exchangeId, const std::string& tradingPair) = 0;
    
    /**
     * Get order status
     * 
     * @param orderId Order ID
     * @return Order status as JSON string
     */
    virtual std::string getOrderStatus(const std::string& orderId) = 0;
    
    /**
     * Register callback for execution reports
     * 
     * @param callback Function to call when execution report is received
     */
    virtual void registerExecutionReportCallback(const std::function<void(const ExecutionReport&)>& callback) = 0;
    
    /**
     * Get open orders
     * 
     * @param exchangeId Optional exchange ID filter
     * @return Vector of open orders
     */
    virtual std::vector<OrderRequest> getOpenOrders(const std::optional<std::string>& exchangeId = std::nullopt) = 0;
    
    /**
     * Check if exchange is available
     * 
     * @param exchangeId Exchange ID
     * @return true if available, false otherwise
     */
    virtual bool isExchangeAvailable(const core::ExchangeId& exchangeId) = 0;
    
    /**
     * Get exchange status
     * 
     * @param exchangeId Exchange ID
     * @return Exchange status as JSON string
     */
    virtual std::string getExchangeStatus(const core::ExchangeId& exchangeId) = 0;
};

/**
 * Factory function to create ExecutionInterface
 * 
 * @return Unique pointer to ExecutionInterface implementation
 */
std::unique_ptr<ExecutionInterface> createExecutionInterface();

} // namespace risk
} // namespace trading