/**
 * @file order_lifecycle_tracker.cpp
 * @brief Implementation of order lifecycle tracking and management
 * 
 * @details 
 * Tracks the state and history of orders as they progress through
 * their lifecycle from creation to final status.
 * 
 * Created on: May 5, 2025
 */

 #include "execution/order_lifecycle_tracker.hpp"
 #include <algorithm>
 #include <iostream>
 #include <sstream>
 
 namespace trading {
 namespace execution {
 
 OrderLifecycleTracker::OrderLifecycleTracker() {
     // Nothing to initialize
 }
 // Implementation in the .cpp file
std::string OrderLifecycleTracker::getClientOrderId(const std::string& exchange_order_id, const core::ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find order with matching exchange order ID and exchange ID
    for (const auto& [client_id, order_ptr] : orders_by_client_id_) {
        if (order_ptr->getExchangeOrderId() == exchange_order_id && 
            order_ptr->getExchangeId() == exchangeId) {
            return client_id;
        }
    }
    
    // Not found
    return "";
}

 bool OrderLifecycleTracker::addOrder(const models::Order& order) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Check if order already exists
     if (orders_by_client_id_.find(order.getClientOrderId()) != orders_by_client_id_.end()) {
         return false;  // models::Order already exists
     }
     
     // Create a shared pointer from the order
     auto order_ptr = std::make_shared<models::Order>(order);
     
     // Store the order
     orders_by_client_id_[order.getClientOrderId()] = order_ptr;
     
     // If the order has an exchange ID, map it as well
     if (!order.getExchangeOrderId().empty()) {
         exchange_to_client_id_map_[order.getExchangeOrderId()] = order.getClientOrderId();
     }
     
     return true;
 }
 bool OrderLifecycleTracker::revertToPreviousState(const std::string& client_order_id, models::OrderState revert_to_state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find the order
    auto it = orders_by_client_id_.find(client_order_id);
    if (it == orders_by_client_id_.end()) {
        return false; // Order not found
    }
    
    auto& order = it->second;
    models::OrderState current_state = order->getState();
    
    // Only allow reverting from certain problematic states
    if (current_state != models::OrderState::REJECTED && 
        current_state != models::OrderState::EXPIRED &&
        current_state != models::OrderState::PENDING_CANCEL) {
        // Could add other states that make sense to revert from
        return false; // Cannot revert from this state
    }
    
    // Check if we should use a stored previous state instead of the provided one
    auto prev_state_it = previous_states_.find(client_order_id);
    if (prev_state_it != previous_states_.end()) {
        // We have a stored previous state, use it instead of the provided one
        // This gives precedence to the actual previous state over the suggested fallback
        revert_to_state = prev_state_it->second;
        
        // Remove the previous state record since we've used it
        previous_states_.erase(prev_state_it);
    }
    
    // Verify the state to revert to is valid
    if (revert_to_state == models::OrderState::FILLED || 
        revert_to_state == models::OrderState::REJECTED ||
        revert_to_state == models::OrderState::EXPIRED) {
        return false; // Cannot revert to a terminal state
    }
    
    // Revert to the specified state
    order->setState(revert_to_state);
    
    // Notify state change
    notifyStateChange(*order, current_state, revert_to_state);
    
    return true;
}

void OrderLifecycleTracker::updateOrderWithExchangeId(models::Order& order, const std::string& exchange_order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if the order already has an exchange order ID
    if (order.getExchangeOrderId().empty()) {
        order.setExchangeOrderId(exchange_order_id);
        exchange_to_client_id_map_[exchange_order_id] = order.getClientOrderId();
    }
}

bool OrderLifecycleTracker::updateOrderState(
    const std::string& client_order_id,
    models::OrderState new_state,
    const std::string& exchange_order_id,
    const std::string& rejection_reason) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find the order
    auto it = orders_by_client_id_.find(client_order_id);
    if (it == orders_by_client_id_.end()) {
        return false; // Order not found
    }
    
    auto& order = it->second;
    
    // Record the old state
    models::OrderState old_state = order->getState();
    
    // Store the previous state if we're moving to PENDING_CANCEL
    if (new_state == models::OrderState::PENDING_CANCEL) {
        previous_states_[client_order_id] = old_state;
    }
    
    // Update exchange order ID if provided and not already set
    if (!exchange_order_id.empty() && order->getExchangeOrderId().empty()) {
        order->setExchangeOrderId(exchange_order_id);
        exchange_to_client_id_map_[exchange_order_id] = client_order_id;
    }
    
    // Update state based on the new state
    bool state_updated = false;
    switch (new_state) {
        case models::OrderState::OPEN:
            state_updated = order->open();
            break;
        case models::OrderState::FILLED:
            // This should normally be done through recordFill, but we'll support it for completeness
            if (order->getFilledQuantity() >= order->getQuantity()) {
                state_updated = true; // Already filled
            }
            break;
        case models::OrderState::CANCELED:
            state_updated = order->cancel();
            break;
        // case models::OrderState::REJECTED:
        //     state_updated = order->reject("Rejected by exchange");
        //     break;
        case models::OrderState::REJECTED:
            state_updated = order->reject(rejection_reason.empty() ? "Rejected by exchange" : rejection_reason);
            break;
        case models::OrderState::EXPIRED:
            state_updated = order->expire();
            break;
        case models::OrderState::PENDING:
        case models::OrderState::PENDING_CANCEL:
        case models::OrderState::PARTIALLY_FILLED:
            // These states are explicitly set
            if (old_state != new_state) {
                order->setState(new_state);  // Using a direct setState method
                state_updated = true;
            }
            break;
        default:
            state_updated = false;
            break;
    }
    
    // Notify state change
    if (state_updated && old_state != new_state) {
        notifyStateChange(*order, old_state, new_state);
    }
    
    return state_updated;
}
 bool OrderLifecycleTracker::recordFill(const std::string& client_order_id, const models::Fill& fill) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Find the order
     auto it = orders_by_client_id_.find(client_order_id);
     if (it == orders_by_client_id_.end()) {
         return false;  // models::Order not found
     }
     
     auto& order = it->second;
     
     // Record the old state
     models::OrderState old_state = order->getState();
     
     // Apply the fill to the order
     bool applied = order->fill(fill);
     
     // Notify state change if state changed
     if (applied && old_state != order->getState()) {
         notifyStateChange(*order, old_state, order->getState());
     }
     
     return applied;
 }
 
 std::shared_ptr<models::Order> OrderLifecycleTracker::getOrderByClientId(const std::string& client_order_id) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     auto it = orders_by_client_id_.find(client_order_id);
     if (it != orders_by_client_id_.end()) {
         return it->second;
     }
     
     return nullptr;  // models::Order not found
 }
 //getOrderByOrderId
std::shared_ptr<models::Order> OrderLifecycleTracker::getOrderByOrderId(const std::string& order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Iterate through all orders to find the one with the given order ID
    for (const auto& pair : orders_by_client_id_) {
        if (pair.second->getOrderId() == order_id) {
            return pair.second;
        }
    }
    
    return nullptr;  // models::Order not found
}
 std::shared_ptr<models::Order> OrderLifecycleTracker::getOrderByExchangeId(const std::string& exchange_order_id) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Find the client order ID
     auto it = exchange_to_client_id_map_.find(exchange_order_id);
     if (it == exchange_to_client_id_map_.end()) {
         return nullptr;  // Exchange order ID not found
     }
     
     // Find the order
     auto order_it = orders_by_client_id_.find(it->second);
     if (order_it == orders_by_client_id_.end()) {
         return nullptr;  // models::Order not found (shouldn't happen)
     }
     
     return order_it->second;
 }
 
 std::vector<std::shared_ptr<models::Order>> OrderLifecycleTracker::getActiveOrders() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<std::shared_ptr<models::Order>> active_orders;
     
     for (const auto& pair : orders_by_client_id_) {
         if (pair.second->isActive()) {
             active_orders.push_back(pair.second);
         }
     }
     
     return active_orders;
 }
 
 std::vector<std::shared_ptr<models::Order>> OrderLifecycleTracker::getOrdersByState(models::OrderState state) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<std::shared_ptr<models::Order>> matching_orders;
     
     for (const auto& pair : orders_by_client_id_) {
         if (pair.second->getState() == state) {
             matching_orders.push_back(pair.second);
         }
     }
     
     return matching_orders;
 }
 
 std::vector<std::shared_ptr<models::Order>> OrderLifecycleTracker::getOrdersBySymbol(const core::Symbol& symbol) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<std::shared_ptr<models::Order>> matching_orders;
     
     for (const auto& pair : orders_by_client_id_) {
         if (pair.second->getTradingPair() == symbol) {
             matching_orders.push_back(pair.second);
         }
     }
     
     return matching_orders;
 }
 
 void OrderLifecycleTracker::registerStateChangeCallback(OrderStateChangeCallback callback) {
     std::lock_guard<std::mutex> lock(mutex_);
     state_change_callbacks_.push_back(callback);
 }
 bool OrderLifecycleTracker::initialize() {
    // Initialization logic if needed
    // For now, we just ensure the mutex is initialized
    std::lock_guard<std::mutex> lock(mutex_);
    // No specific initialization needed for the data structures
    return true;  // Initialization successful
 }
 void OrderLifecycleTracker::shutdown() {
     std::lock_guard<std::mutex> lock(mutex_);
     orders_by_client_id_.clear();
     exchange_to_client_id_map_.clear();
     state_change_callbacks_.clear();
 }
 std::vector<std::shared_ptr<models::Order>> OrderLifecycleTracker::checkForTimedOutOrders(int64_t timeout_threshold_ms) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::vector<std::shared_ptr<models::Order>> timed_out_orders;
     auto now = std::chrono::system_clock::now();
     
     for (const auto& pair : orders_by_client_id_) {
         auto& order = pair.second;
         
         // Only check active orders that aren't in a terminal state
         if (!order->isActive()) {
             continue;
         }
         
         // Check if order has timed out
         auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
             now - order->getUpdatedTime()).count();
             
         if (elapsed > timeout_threshold_ms) {
             timed_out_orders.push_back(order);
         }
     }
     
     return timed_out_orders;
 }
 std::vector<std::shared_ptr<models::Order>> OrderLifecycleTracker::getFilledOrders(
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<models::Order>> filled_orders;
    for (const auto& pair : orders_by_client_id_) {
        const auto& order = pair.second;
        // Check if the order is filled and within the time range
        if (order->getState() == models::OrderState::FILLED &&
            order->getCreatedTime() >= start_time && order->getCreatedTime() <= end_time) {
            filled_orders.push_back(order);
        }
    }
    return filled_orders;
 }

 int OrderLifecycleTracker::purgeOldOrders(int64_t age_threshold_ms) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     int purged_count = 0;
     auto now = std::chrono::system_clock::now();
     std::vector<std::string> to_remove;
     
     // Find orders to purge
     for (const auto& pair : orders_by_client_id_) {
         auto& order = pair.second;
         
         // Only purge orders in terminal states
         if (order->isActive()) {
             continue;
         }
         
         // Check if order is old enough to purge
         auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
             now - order->getUpdatedTime()).count();
             
         if (elapsed > age_threshold_ms) {
             to_remove.push_back(pair.first);
             
             // Also remove from exchange_to_client_id_map_
             if (!order->getExchangeOrderId().empty()) {
                 exchange_to_client_id_map_.erase(order->getExchangeOrderId());
             }
             
             purged_count++;
         }
     }
     
     // Remove the orders
     for (const auto& client_id : to_remove) {
         orders_by_client_id_.erase(client_id);
     }
     
     return purged_count;
 }
 
 void OrderLifecycleTracker::notifyStateChange(const models::Order& order, models::OrderState old_state, models::OrderState new_state) {
     // Call all registered callbacks
     for (const auto& callback : state_change_callbacks_) {
         callback(order, old_state, new_state);
     }
 }
 
 } // namespace execution
 } // namespace trading