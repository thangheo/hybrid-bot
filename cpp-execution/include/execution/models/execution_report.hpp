// models/execution_report.hpp
#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include "order.hpp"

namespace trading {
namespace execution {
namespace models {
/**
 * Represents the source of an execution report
 */
enum class ExecutionReportSource {
    EXCHANGE,       // Direct from exchange
    INTERNAL,       // Generated internally
    RECONCILIATION  // Generated during reconciliation
};

/**
 * ExecutionReport encapsulates information about an order execution event
 */
class ExecutionReport {
public:
    // Constructors
    ExecutionReport() = default;
    explicit ExecutionReport(const std::string& orderId);
    ExecutionReport(const std::string& orderId, const std::string& exchangeOrderId);
    
    // Factory methods
    static std::shared_ptr<ExecutionReport> createFromOrder(const std::shared_ptr<Order>& order);
    static std::shared_ptr<ExecutionReport> createRejectionReport(
        const std::shared_ptr<Order>& order, 
        const std::string& reason);
    
    // Getters (using modern naming convention)
    const std::string& orderId() const { return orderId_; }
    const std::string& clientOrderId() const { return clientOrderId_; }
    const std::string& exchangeOrderId() const { return exchangeOrderId_; }
    const core::ExchangeId& exchangeId() const { return exchangeId_; }
    const core::Symbol& symbol() const { return symbol_; }
    OrderSide side() const { return side_; }
    OrderType type() const { return type_; }
    OrderState status() const { return status_; }
    double quantity() const { return quantity_; }
    double filledQuantity() const { return filledQuantity_; }
    double remainingQuantity() const { return quantity_ - filledQuantity_; }
    const std::optional<double>& price() const { return price_; }
    double averageFillPrice() const { return averageFillPrice_; }
    const std::string& rejectReason() const { return rejectReason_; }
    const std::vector<Fill>& fills() const { return fills_; }
    std::chrono::system_clock::time_point createdAt() const { return createdAt_; }
    std::chrono::system_clock::time_point updatedAt() const { return updatedAt_; }
    ExecutionReportSource source() const { return source_; }
    const std::unordered_map<std::string, std::string>& extraData() const { return extraData_; }

    // Setters
    void setOrderId(const std::string& orderId) { orderId_ = orderId; }
    void setClientOrderId(const std::string& clientOrderId) { clientOrderId_ = clientOrderId; }
    void setExchangeOrderId(const std::string& exchangeOrderId) { exchangeOrderId_ = exchangeOrderId; }
    void setExchangeId(const core::ExchangeId& exchangeId) { exchangeId_ = exchangeId; }
    void setSymbol(const core::Symbol& symbol) { symbol_ = symbol; }
    void setSide(OrderSide side) { side_ = side; }
    void setType(OrderType type) { type_ = type; }
    void setStatus(OrderState status) { status_ = status; updateTimestamp(); }
    void setQuantity(double quantity) { quantity_ = quantity; updateTimestamp(); }
    void setFilledQuantity(double filledQuantity) { filledQuantity_ = filledQuantity; updateTimestamp(); }
    void setPrice(const std::optional<double>& price) { price_ = price; updateTimestamp(); }
    void setAverageFillPrice(double averageFillPrice) { averageFillPrice_ = averageFillPrice; updateTimestamp(); }
    void setRejectReason(const std::string& reason) { rejectReason_ = reason; updateTimestamp(); }
    void setCreatedAt(const std::chrono::system_clock::time_point& timestamp) { createdAt_ = timestamp; }
    void setSource(ExecutionReportSource source) { source_ = source; }
    
    // Utility methods
    void addFill(const Fill& fill) { 
        fills_.push_back(fill); 
        updateFilledQuantity();
        updateTimestamp();
    }
    
    bool isRejected() const { return status_ == OrderState::REJECTED; }
    bool isComplete() const { return status_ == OrderState::FILLED || status_ == OrderState::REJECTED; }
    std::string toString() const;
    
    // Apply this report to an order
    bool applyToOrder(std::shared_ptr<Order>& order) const;
    
    // Add or get extra data
    void setExtraData(const std::string& key, const std::string& value) {
        extraData_[key] = value;
        updateTimestamp();
    }
    
    std::string getExtraData(const std::string& key) const {
        auto it = extraData_.find(key);
        return (it != extraData_.end()) ? it->second : "";
    }

private:
    void updateTimestamp() { updatedAt_ = std::chrono::system_clock::now(); }
    void updateFilledQuantity() {
        double total = 0.0;
        for (const auto& fill : fills_) {
            total += fill.quantity;
        }
        filledQuantity_ = total;
    }

    std::string orderId_;                // Internal order ID
    std::string clientOrderId_;          // Client-specified ID
    std::string exchangeOrderId_;        // Exchange-assigned ID
    core::ExchangeId exchangeId_;             // Exchange where order is placed
    std::string symbol_;                 // Trading pair
    OrderSide side_ = OrderSide::BUY;    // Buy or sell
    OrderType type_ = OrderType::MARKET; // Order type
    OrderState status_ = OrderState::CREATED; // Current order status
    double quantity_ = 0.0;              // Original order quantity
    double filledQuantity_ = 0.0;        // Quantity that has been filled
    std::optional<double> price_;        // Limit price (if applicable)
    double averageFillPrice_ = 0.0;      // Average fill price for executed quantity
    std::chrono::system_clock::time_point createdAt_ = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point updatedAt_ = std::chrono::system_clock::now();
    std::vector<Fill> fills_;            // Individual fills
    std::string rejectReason_;           // Reason for rejection (if applicable)
    ExecutionReportSource source_ = ExecutionReportSource::EXCHANGE;
    
    // Additional data from exchange
    std::unordered_map<std::string, std::string> extraData_;
};

// Type aliases for convenience
using ExecutionReportPtr = std::shared_ptr<ExecutionReport>;
using ExecutionReportConstPtr = std::shared_ptr<const ExecutionReport>;

} // namespace execution
} // namespace trading
} // namespace models