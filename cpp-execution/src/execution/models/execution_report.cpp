// models/execution_report.cpp
#include "models/execution_report.hpp"
#include <sstream>

namespace trading {
namespace execution {
namespace models {
// Basic constructor with order ID
// ExecutionReport(const std::string& orderId) : orderId_(orderId) {
ExecutionReport::ExecutionReport(const std::string& orderId) : orderId_(orderId) {
    createdAt_ = std::chrono::system_clock::now();
    updatedAt_ = createdAt_;
}

// Constructor with both order ID and exchange order ID
ExecutionReport::ExecutionReport(const std::string& orderId, const std::string& exchangeOrderId) 
    : orderId_(orderId), exchangeOrderId_(exchangeOrderId) {
    createdAt_ = std::chrono::system_clock::now();
    updatedAt_ = createdAt_;
}

// Create an execution report from an existing order
std::shared_ptr<ExecutionReport> ExecutionReport::createFromOrder(const std::shared_ptr<Order>& order) {
    auto report = std::make_shared<ExecutionReport>(order->getOrderId());
    
    report->setClientOrderId(order->getClientOrderId());
    report->setExchangeOrderId(order->getExchangeOrderId());
    report->setExchangeId(order->getExchangeId());
    report->setSymbol(order->getTradingPair());
    report->setSide(order->getSide());
    report->setType(order->getType());
    report->setStatus(order->getState());
    report->setQuantity(order->getQuantity());
    report->setFilledQuantity(order->getFilledQuantity());
    
    // Set price if it's a limit order
    if (order->getType() != OrderType::MARKET) {
        report->setPrice(order->getPrice());
    }
    
    report->setAverageFillPrice(order->getAverageFillPrice());
    
    // Add fills
    for (const auto& fill : order->getFills()) {
        Fill newFill;
        newFill.fill_id = fill.fill_id;
        newFill.orderId = fill.orderId;
        newFill.exchangeOrderId = order->getExchangeOrderId();
        newFill.quantity = fill.quantity;
        newFill.price = fill.price;
        newFill.fee = fill.fee;
        newFill.fee_currency = fill.fee_currency;
        newFill.timestamp = fill.timestamp;
        
        report->addFill(newFill);
    }
    
    report->setSource(ExecutionReportSource::INTERNAL);
    return report;
}

// Create a rejection report for an order
std::shared_ptr<ExecutionReport> ExecutionReport::createRejectionReport(
    const std::shared_ptr<Order>& order, 
    const std::string& reason) {
        
    auto report = std::make_shared<ExecutionReport>(order->getOrderId());
    
    report->setClientOrderId(order->getClientOrderId());
    report->setExchangeId(order->getExchangeId());
    report->setSymbol(order->getTradingPair());
    report->setSide(order->getSide());
    report->setType(order->getType());
    report->setStatus(OrderState::REJECTED);
    report->setQuantity(order->getQuantity());
    
    // Set price if it's a limit order
    if (order->getType() != OrderType::MARKET) {
        report->setPrice(order->getPrice());
    }
    
    report->setRejectReason(reason);
    report->setSource(ExecutionReportSource::INTERNAL);
    
    return report;
}

// Apply the execution report to an order
bool ExecutionReport::applyToOrder(std::shared_ptr<Order>& order) const {
    if (order->getOrderId() != orderId_ && order->getClientOrderId() != clientOrderId_) {
        return false;  // This report doesn't belong to this order
    }
    
    // Apply exchange order ID if available
    if (!exchangeOrderId_.empty() && order->getExchangeOrderId().empty()) {
        order->setExchangeOrderId(exchangeOrderId_);
    }
    
    // Handle different status updates
    switch (status_) {
        case OrderState::OPEN:
            order->open();
            break;
            
        case OrderState::PARTIALLY_FILLED:
            // Apply each fill from the report that isn't already in the order
            for (const auto& reportFill : fills_) {
                // Check if this fill is already recorded
                bool fillExists = false;
                for (const auto& orderFill : order->getFills()) {
                    if (orderFill.fill_id == reportFill.fill_id) {
                        fillExists = true;
                        break;
                    }
                }
                
                // If the fill doesn't exist, add it
                if (!fillExists) {
                    Fill newFill;
                    newFill.fill_id = reportFill.fill_id;
                    newFill.orderId = order->getOrderId();
                    newFill.quantity = reportFill.quantity;
                    newFill.price = reportFill.price;
                    newFill.fee = reportFill.fee;
                    newFill.fee_currency = reportFill.fee_currency;
                    newFill.timestamp = reportFill.timestamp;
                    
                    order->fill(newFill);
                }
            }
            break;
            
        case OrderState::FILLED:
            // Apply each fill as above
            for (const auto& reportFill : fills_) {
                bool fillExists = false;
                for (const auto& orderFill : order->getFills()) {
                    if (orderFill.fill_id == reportFill.fill_id) {
                        fillExists = true;
                        break;
                    }
                }
                
                if (!fillExists) {
                    Fill newFill;
                    newFill.fill_id = reportFill.fill_id;
                    newFill.orderId = order->getOrderId();
                    newFill.quantity = reportFill.quantity;
                    newFill.price = reportFill.price;
                    newFill.fee = reportFill.fee;
                    newFill.fee_currency = reportFill.fee_currency;
                    newFill.timestamp = reportFill.timestamp;
                    
                    order->fill(newFill);
                }
            }
            break;
            
        case OrderState::CANCELED:
            order->cancel();
            break;
            
        case OrderState::REJECTED:
            order->reject(rejectReason_);
            break;
            
        case OrderState::EXPIRED:
            order->expire();
            break;
            
        default:
            // For other states (CREATED, PENDING, PENDING_CANCEL)
            // we don't need to do anything special
            break;
    }
    
    // Copy any extra data to the order
    for (const auto& [key, value] : extraData_) {
        order->setExtraParam(key, value);
    }
    
    return true;
}

// Convert to string for logging and debugging
std::string ExecutionReport::toString() const {
    std::ostringstream oss;
    
    oss << "ExecutionReport[orderId=" << orderId_ 
        << ", clientOrderId=" << clientOrderId_ 
        << ", exchangeOrderId=" << exchangeOrderId_
        << ", exchange=" << exchangeId_.toString()
        << ", symbol=" << symbol_
        << ", side=" << (side_ == OrderSide::BUY ? "BUY" : "SELL")
        << ", type=";
    
    switch (type_) {
        case OrderType::MARKET: oss << "MARKET"; break;
        case OrderType::LIMIT: oss << "LIMIT"; break;
        case OrderType::STOP: oss << "STOP"; break;
        case OrderType::STOP_LIMIT: oss << "STOP_LIMIT"; break;
        case OrderType::ICEBERG: oss << "ICEBERG"; break;
        case OrderType::TRAILING_STOP: oss << "TRAILING_STOP"; break;
        case OrderType::FILL_OR_KILL: oss << "FILL_OR_KILL"; break;
        case OrderType::IMMEDIATE_OR_CANCEL: oss << "IMMEDIATE_OR_CANCEL"; break;
        case OrderType::POST_ONLY: oss << "POST_ONLY"; break;
    }
    
    oss << ", status=";
    switch (status_) {
        case OrderState::CREATED: oss << "CREATED"; break;
        case OrderState::PENDING: oss << "PENDING"; break;
        case OrderState::PENDING_CANCEL: oss << "PENDING_CANCEL"; break;
        case OrderState::OPEN: oss << "OPEN"; break;
        case OrderState::PARTIALLY_FILLED: oss << "PARTIALLY_FILLED"; break;
        case OrderState::FILLED: oss << "FILLED"; break;
        case OrderState::CANCELED: oss << "CANCELED"; break;
        case OrderState::REJECTED: oss << "REJECTED"; break;
        case OrderState::EXPIRED: oss << "EXPIRED"; break;
    }
    
    oss << ", quantity=" << quantity_
        << ", filledQuantity=" << filledQuantity_;
    
    if (price_.has_value()) {
        oss << ", price=" << price_.value();
    }
    
    if (averageFillPrice_ > 0) {
        oss << ", avgFillPrice=" << averageFillPrice_;
    }
    
    oss << ", fills=" << fills_.size();
    
    if (isRejected()) {
        oss << ", rejectReason='" << rejectReason_ << "'";
    }
    
    oss << ", source=";
    switch (source_) {
        case ExecutionReportSource::EXCHANGE: oss << "EXCHANGE"; break;
        case ExecutionReportSource::INTERNAL: oss << "INTERNAL"; break;
        case ExecutionReportSource::RECONCILIATION: oss << "RECONCILIATION"; break;
    }
    
    oss << "]";
    return oss.str();
}

} // namespace execution
} // namespace trading
} // namespace models