/**
 * @file execution_interface.hpp
 * @brief Interface for communication with the execution engine (Direct Integration)
 * 
 * This interface provides direct communication with ExecutionService instead of ZeroMQ.
 * Used by Risk Management System to interact with trading execution.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

#include "risk_types.hpp"
#include "core/market_types.hpp"
#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <future>

// Forward declaration
namespace trading {
namespace execution {
class ExecutionService;
}
}

namespace trading {
namespace risk {

// Forward declaration
class RiskConfig;

/**
 * Structure representing an order request from Risk Management
 */
// struct OrderRequest {
//     std::string orderId;                          // Internal order ID
//     std::string clientOrderId;                    // Client-specified order ID
//     core::ExchangeId exchangeId;                  // Target exchange
//     core::Symbol tradingPair;                     // Trading pair
//     execution::models::OrderSide side;                    // Buy or sell
//     std::string type;                             // Order type as string
//     double quantity;                              // Order quantity
//     double price;                                 // Order price (for limit orders)
//     std::string timeInForce;                      // Time in force
//     std::string strategyId;                       // Strategy identifier
//     std::string signalId;                         // Signal identifier
//     core::Timestamp timestamp;                    // Order creation timestamp
//     std::map<std::string, std::string> params;   // Additional parameters
    
//     OrderRequest() 
//         : quantity(0.0), price(0.0), 
//           timestamp(std::chrono::system_clock::now()) {}
// };

/**
 * Type alias for ExecutionReport from execution module
 */
using ExecutionReport = execution::models::ExecutionReport;

/**
 * Interface for communicating with the execution engine
 * 
 * This interface provides methods for:
 * - Order management (submit, cancel, status)
 * - Account balance queries
 * - Exchange status monitoring
 * - Execution report callbacks
 */
class ExecutionInterface {
public:
    virtual ~ExecutionInterface() = default;

    /**
     * Initialize the execution interface
     * @return true if initialization was successful
     */
    virtual bool initialize() = 0;

    /**
     * Check if the interface is connected and operational
     * @return true if connected
     */
    virtual bool isConnected() const = 0;

    // === Order Management ===

    /**
     * Submit a new order to the execution engine
     * @param orderRequest Order details
     * @return Order ID if successful, empty string if failed
     */
    virtual std::string submitOrder(const OrderRequest& orderRequest) = 0;

    /**
     * Cancel an existing order
     * @param orderId Order ID to cancel
     * @return true if cancellation request was successful
     */
    virtual bool cancelOrder(const std::string& orderId) = 0;

    /**
     * Cancel all orders for a specific exchange
     * @param exchangeId Exchange identifier
     * @return Number of orders cancelled
     */
    virtual int cancelAllOrders(const core::ExchangeId& exchangeId) = 0;

    /**
     * Cancel all orders for a specific trading pair on an exchange
     * @param exchangeId Exchange identifier
     * @param tradingPair Trading pair
     * @return Number of orders cancelled
     */
    virtual int cancelAllOrders(const core::ExchangeId& exchangeId, 
                               const core::Symbol& tradingPair) = 0;

    /**
     * Get the current status of an order
     * @param orderId Order ID to query
     * @return JSON string with order status details
     */
    virtual std::string getOrderStatus(const std::string& orderId) = 0;

    /**
     * Get all open orders
     * @param exchangeId Optional exchange filter
     * @return Vector of open order requests
     */
    virtual std::vector<OrderRequest> getOpenOrders(
        const std::optional<std::string>& exchangeId = std::nullopt) = 0;

    // === Account & Balance Management ===

    /**
     * Get account balances for a specific exchange
     * @param exchangeId Exchange identifier
     * @return Map of asset symbol to balance amount
     */
    virtual std::map<std::string, double> getAccountBalances(
        const core::ExchangeId& exchangeId) = 0;

    /**
     * Get account balances for a specific exchange (async version)
     * @param exchangeId Exchange identifier
     * @return Future containing map of asset symbol to balance amount
     */
    virtual std::future<std::map<std::string, double>> getAccountBalancesAsync(
        const core::ExchangeId& exchangeId) = 0;

    // === Exchange Status ===

    /**
     * Check if an exchange is available for trading
     * @param exchangeId Exchange identifier
     * @return true if exchange is available
     */
    virtual bool isExchangeAvailable(const core::ExchangeId& exchangeId) = 0;

    /**
     * Get detailed status information for an exchange
     * @param exchangeId Exchange identifier
     * @return JSON string with exchange status
     */
    virtual std::string getExchangeStatus(const core::ExchangeId& exchangeId) = 0;

    // === Execution Reports ===

    /**
     * Register a callback for execution reports
     * @param callback Function to call when execution reports are received
     */
    virtual void registerExecutionReportCallback(
        const std::function<void(const ExecutionReport&)>& callback) = 0;

    // === Position Management ===

    /**
     * Get current positions for a specific exchange
     * @param exchangeId Exchange identifier
     * @return Map of trading pair to position size
     */
    virtual std::map<std::string, double> getPositions(
        const core::ExchangeId& exchangeId) = 0;

    /**
     * Get current positions across all exchanges
     * @return Map of exchange to (trading pair to position size)
     */
    virtual std::map<core::ExchangeId, std::map<std::string, double>> getAllPositions() = 0;

protected:
    /**
     * Protected constructor - use factory function to create instances
     */
    ExecutionInterface() = default;
};

/**
 * Factory function to create ExecutionInterface implementation
 * @param config Risk configuration
 * @param executionService Shared pointer to ExecutionService
 * @return Unique pointer to ExecutionInterface implementation
 */
std::unique_ptr<ExecutionInterface> createExecutionInterface(
    const RiskConfig& config);

} // namespace risk
} // namespace trading