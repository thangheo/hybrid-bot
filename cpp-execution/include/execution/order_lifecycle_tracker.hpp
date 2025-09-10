/**
 * @file order_lifecycle_tracker.hpp
 * @brief models::Order lifecycle tracking and management
 * 
 * @details 
 * Tracks the state and history of orders as they progress through
 * their lifecycle from creation to final status.
 * 
 * Created on: May 5, 2025
 */
#pragma once

#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <chrono>

namespace trading {
namespace execution {

// Forward declarations
class OrderLifecycleTracker;

/**
 * @brief Callback for order state changes
 */
using OrderStateChangeCallback = std::function<void(const models::Order&, models::OrderState, models::OrderState)>;

/**
 * @brief Manages the lifecycle of orders through the system
 */
class OrderLifecycleTracker {
public:
    /**
     * @brief Constructor
     */
    OrderLifecycleTracker();
    
    /**
     * @brief Destructor
     */
    ~OrderLifecycleTracker() = default;
    
    /**
     * @brief Add a new order to track
     * 
     * @param order The order to track
     * @return true if successful, false if order already exists
     */
    bool addOrder(const models::Order& order);
    // revertToPreviousState
    /**
     * @brief Revert an order to a previous state
     * 
     * @param client_order_id The client order ID
     * @param new_state The new state
     * @return true if successful, false if order not found
     */
    bool revertToPreviousState(const std::string& client_order_id, models::OrderState revert_to_state) ;

    // getHealthStatus
    /**
     * @brief Get the health status of the order lifecycle tracker
     * 
     * @return Health status as a string
     */
    std::string getHealthStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return "OrderLifecycleTracker: " + std::to_string(orders_by_client_id_.size()) + " orders tracked.";
    }
    /**
     * @brief Update an order's state
     * 
     * @param client_order_id The client order ID
     * @param new_state The new state
     * @param exchange_order_id Optional exchange order ID (if not already set)
     * @return true if successful, false if order not found
     */
    bool updateOrderState(const std::string& client_order_id, 
                         models::OrderState new_state,
                         const std::string& exchange_order_id = "",
                         const std::string& rejection_reason = "");

    //updateOrderWithExchangeId method
    void updateOrderWithExchangeId(models::Order& order, const std::string& exchange_order_id);
    std::string getClientOrderId(const std::string& exchange_order_id, const core::ExchangeId& exchangeId) const;
                        
    /**
     * @brief Record a fill for an order
     * 
     * @param client_order_id The client order ID
     * @param fill The fill information
     * @return true if successful, false if order not found
     */
    bool recordFill(const std::string& client_order_id, const models::Fill& fill);
    // getFilledOrders
    
    /**
     * @brief Get an order by client order ID
     * 
     * @param client_order_id The client order ID
     * @return The order, or nullptr if not found
     */
    std::shared_ptr<models::Order> getOrderByClientId(const std::string& client_order_id) const;
    /*
    *   GgetOrderByOrderId
    *   @param order_id The order ID
    *   @return The order, or nullptr if not found
    */
    std::shared_ptr<models::Order> getOrderByOrderId(const std::string& order_id) const;
    /**
     * @brief Get an order by exchange order ID
     * 
     * @param exchange_order_id The exchange order ID
     * @return The order, or nullptr if not found
     */
    std::shared_ptr<models::Order> getOrderByExchangeId(const std::string& exchange_order_id) const;
    
    /**
     * @brief Get all active orders
     * 
     * @return Vector of active orders
     */
    std::vector<std::shared_ptr<models::Order>> getActiveOrders() const;
    
    /**
     * @brief Get all orders in a specific state
     * 
     * @param state The state to filter by
     * @return Vector of orders in the requested state
     */
    std::vector<std::shared_ptr<models::Order>> getOrdersByState(models::OrderState state) const;
    
    /**
     * @brief Get all orders for a trading pair
     * 
     * @param symbol The trading pair
     * @return Vector of orders for the trading pair
     */
    std::vector<std::shared_ptr<models::Order>> getOrdersBySymbol(const core::Symbol& symbol) const;
    
    /**
     * @brief Register a callback for order state changes
     * 
     * @param callback The callback function
     */
    void registerStateChangeCallback(OrderStateChangeCallback callback);
    bool initialize () ;
    void shutdown();

    /**
     * @brief Check for orders that have timed out (no update for a certain period)
     * 
     * @param timeout_threshold_ms Timeout threshold in milliseconds
     * @return Vector of timed out orders
     */
    std::vector<std::shared_ptr<models::Order>> checkForTimedOutOrders(int64_t timeout_threshold_ms);
    
    /**
     * @brief Purge old orders from memory
     * 
     * @param age_threshold_ms Age threshold in milliseconds
     * @return Number of orders purged
     */
    int purgeOldOrders(int64_t age_threshold_ms);
    //protype for getFilledOrders
    std::vector<std::shared_ptr<models::Order>> getFilledOrders(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time) const;
    // getActiveOrderCount
    /**
     * @brief Get the count of active orders
     * 
     * @return Number of active orders
     */
    int getActiveOrderCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return orders_by_client_id_.size();
    }
    // getOrdersPlacedInLastHour
    /**
     * @brief Get the count of orders placed in the last hour
     * 
     * @return Number of orders placed in the last hour
     */
    int getOrdersPlacedInLastHour() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int count = 0;
        auto now = std::chrono::system_clock::now();
        for (const auto& pair : orders_by_client_id_) {
            if (pair.second->getCreatedTime() >= now - std::chrono::hours(1)) {
                count++;
            }
        }
        return count;
    }
    //getFillsInLastHour
    /**
     * @brief Get the count of fills in the last hour
     * 
     * @return Number of fills in the last hour
     */
    int getFillsInLastHour() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int count = 0;
        auto now = std::chrono::system_clock::now();
        for (const auto& pair : orders_by_client_id_) {
            for (const auto& fill : pair.second->getFills()) {
                if (fill.timestamp >= now - std::chrono::hours(1)) {
                    count++;
                }
            }
        }
        return count;
    }
    /**
     * @brief Get the health status of the order lifecycle tracker
     * 
     * @return Health status as a JSON string
     */
private:
    // Maps for quick lookup
    std::unordered_map<std::string, std::shared_ptr<models::Order>> orders_by_client_id_;
    std::unordered_map<std::string, std::string> exchange_to_client_id_map_;
    
    // Callbacks
    std::vector<OrderStateChangeCallback> state_change_callbacks_;
    // In the private section of OrderLifecycleTracker class
    std::unordered_map<std::string, models::OrderState> previous_states_;
    // Thread safety
    mutable std::mutex mutex_;
    
    // Helper methods
    void notifyStateChange(const models::Order& order, models::OrderState old_state, models::OrderState new_state);
};

} // namespace execution
} // namespace trading