#include <gtest/gtest.h>
#include "models/execution_report.hpp"
#include <memory>
#include <chrono>

using namespace trading::execution;

class ExecutionReportTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a test order for use in various tests
        testOrder = std::make_shared<Order>("test-client-id");
        testOrder->setOrderId("test-order-id");
        testOrder->setExchangeId("test-exchange");
        testOrder->setTradingPair("BTC/USDT");
        testOrder->setSide(OrderSide::BUY);
        testOrder->setType(OrderType::LIMIT);
        testOrder->setQuantity(1.0);
        testOrder->setPrice(45000.0);
    }
    
    std::shared_ptr<Order> testOrder;
};

// Test basic constructor
TEST_F(ExecutionReportTest, BasicConstructor) {
    ExecutionReport report("test-order-id");
    
    EXPECT_EQ(report.orderId(), "test-order-id");
    EXPECT_EQ(report.status(), OrderState::CREATED);
    EXPECT_EQ(report.source(), ExecutionReportSource::EXCHANGE);
    EXPECT_EQ(report.fills().size(), 0);
    EXPECT_FALSE(report.isRejected());
    EXPECT_FALSE(report.isComplete());
}

// Test constructor with exchange order ID
TEST_F(ExecutionReportTest, ExchangeOrderIdConstructor) {
    ExecutionReport report("test-order-id", "test-exchange-order-id");
    
    EXPECT_EQ(report.orderId(), "test-order-id");
    EXPECT_EQ(report.exchangeOrderId(), "test-exchange-order-id");
    EXPECT_EQ(report.status(), OrderState::CREATED);
}

// Test create from order factory method
TEST_F(ExecutionReportTest, CreateFromOrder) {
    auto report = ExecutionReport::createFromOrder(testOrder);
    
    EXPECT_EQ(report->orderId(), testOrder->getOrderId());
    EXPECT_EQ(report->clientOrderId(), testOrder->getClientOrderId());
    EXPECT_EQ(report->exchangeId(), testOrder->getExchangeId());
    EXPECT_EQ(report->symbol(), testOrder->getTradingPair());
    EXPECT_EQ(report->side(), testOrder->getSide());
    EXPECT_EQ(report->type(), testOrder->getType());
    EXPECT_EQ(report->status(), testOrder->getState());
    EXPECT_EQ(report->quantity(), testOrder->getQuantity());
    EXPECT_EQ(report->filledQuantity(), testOrder->getFilledQuantity());
    EXPECT_EQ(report->price().value(), testOrder->getPrice());
    EXPECT_EQ(report->source(), ExecutionReportSource::INTERNAL);
}

// Test rejection report factory method
TEST_F(ExecutionReportTest, CreateRejectionReport) {
    std::string rejectReason = "Insufficient funds";
    auto report = ExecutionReport::createRejectionReport(testOrder, rejectReason);
    
    EXPECT_EQ(report->orderId(), testOrder->getOrderId());
    EXPECT_EQ(report->clientOrderId(), testOrder->getClientOrderId());
    EXPECT_EQ(report->status(), OrderState::REJECTED);
    EXPECT_EQ(report->rejectReason(), rejectReason);
    EXPECT_TRUE(report->isRejected());
    EXPECT_TRUE(report->isComplete());
    EXPECT_EQ(report->source(), ExecutionReportSource::INTERNAL);
}

// Test getters and setters
TEST_F(ExecutionReportTest, GettersAndSetters) {
    ExecutionReport report;
    
    report.setOrderId("test-order-id");
    EXPECT_EQ(report.orderId(), "test-order-id");
    
    report.setClientOrderId("test-client-id");
    EXPECT_EQ(report.clientOrderId(), "test-client-id");
    
    report.setExchangeOrderId("test-exchange-order-id");
    EXPECT_EQ(report.exchangeOrderId(), "test-exchange-order-id");
    
    report.setExchangeId("binance");
    EXPECT_EQ(report.exchangeId(), "binance");
    
    report.setSymbol("ETH/USDT");
    EXPECT_EQ(report.symbol(), "ETH/USDT");
    
    report.setSide(OrderSide::SELL);
    EXPECT_EQ(report.side(), OrderSide::SELL);
    
    report.setType(OrderType::MARKET);
    EXPECT_EQ(report.type(), OrderType::MARKET);
    
    report.setStatus(OrderState::OPEN);
    EXPECT_EQ(report.status(), OrderState::OPEN);
    
    report.setQuantity(2.5);
    EXPECT_EQ(report.quantity(), 2.5);
    
    report.setFilledQuantity(1.0);
    EXPECT_EQ(report.filledQuantity(), 1.0);
    EXPECT_EQ(report.remainingQuantity(), 1.5);
    
    std::optional<double> price = 3000.0;
    report.setPrice(price);
    EXPECT_EQ(report.price().value(), 3000.0);
    
    report.setAverageFillPrice(3050.0);
    EXPECT_EQ(report.averageFillPrice(), 3050.0);
    
    report.setRejectReason("Test rejection");
    EXPECT_EQ(report.rejectReason(), "Test rejection");
    
    auto now = std::chrono::system_clock::now();
    report.setCreatedAt(now);
    EXPECT_EQ(report.createdAt(), now);
    
    report.setSource(ExecutionReportSource::RECONCILIATION);
    EXPECT_EQ(report.source(), ExecutionReportSource::RECONCILIATION);
    
    report.setExtraData("test-key", "test-value");
    EXPECT_EQ(report.extraData().at("test-key"), "test-value");
    EXPECT_EQ(report.getExtraData("test-key"), "test-value");
    EXPECT_EQ(report.getExtraData("non-existent-key"), "");
}

// Test adding fills
TEST_F(ExecutionReportTest, AddFills) {
    ExecutionReport report("test-order-id");
    
    // Add a fill
    Fill fill;
    fill.fill_id = "test-fill-1";
    fill.orderId = "test-order-id";
    fill.quantity = 0.5;
    fill.price = 45000.0;
    fill.fee = 0.0025;
    fill.fee_currency = "BTC";
    fill.timestamp = std::chrono::system_clock::now();
    
    report.addFill(fill);
    
    EXPECT_EQ(report.fills().size(), 1);
    EXPECT_EQ(report.filledQuantity(), 0.5);
    
    // Add another fill
    fill.fill_id = "test-fill-2";
    fill.quantity = 0.3;
    fill.price = 45100.0;
    
    report.addFill(fill);
    
    EXPECT_EQ(report.fills().size(), 2);
    EXPECT_EQ(report.filledQuantity(), 0.8);
}

// Test applying report to order - successful case
TEST_F(ExecutionReportTest, ApplyToOrderSuccess) {
    // Create order and open it
    auto order = std::make_shared<Order>("test-client-id");
    order->setOrderId("test-order-id");
    order->setTradingPair("BTC/USDT");
    order->setSide(OrderSide::BUY);
    order->setQuantity(1.0);
    order->open();
    
    // Create report for a partial fill
    ExecutionReport report(order->getOrderId());
    report.setStatus(OrderState::PARTIALLY_FILLED);
    report.setExchangeOrderId("test-exchange-order-id");
    
    Fill fill;
    fill.fill_id = "test-fill-1";
    fill.orderId = order->getOrderId();
    fill.exchangeOrderId = "test-exchange-order-id";
    fill.quantity = 0.5;
    fill.price = 45000.0;
    fill.fee = 0.0025;
    fill.fee_currency = "BTC";
    fill.timestamp = std::chrono::system_clock::now();
    
    report.addFill(fill);
    
    // Apply the report to the order
    EXPECT_TRUE(report.applyToOrder(order));
    
    // Check that the order was updated
    EXPECT_EQ(order->getState(), OrderState::PARTIALLY_FILLED);
    EXPECT_EQ(order->getExchangeOrderId(), "test-exchange-order-id");
    EXPECT_EQ(order->getFilledQuantity(), 0.5);
    EXPECT_EQ(order->getFills().size(), 1);
    EXPECT_EQ(order->getAverageFillPrice(), 45000.0);
    
    // Create another report for a complete fill
    ExecutionReport report2(order->getOrderId());
    report2.setStatus(OrderState::FILLED);
    
    Fill fill2;
    fill2.fill_id = "test-fill-2";
    fill2.orderId = order->getOrderId();
    fill2.exchangeOrderId = "test-exchange-order-id";
    fill2.quantity = 0.5;
    fill2.price = 45100.0;
    fill2.fee = 0.0025;
    fill2.fee_currency = "BTC";
    fill2.timestamp = std::chrono::system_clock::now();
    
    report2.addFill(fill2);
    
    // Apply the second report
    EXPECT_TRUE(report2.applyToOrder(order));
    
    // Check that the order was updated
    EXPECT_EQ(order->getState(), OrderState::FILLED);
    EXPECT_EQ(order->getFilledQuantity(), 1.0);
    EXPECT_EQ(order->getFills().size(), 2);
    EXPECT_NEAR(order->getAverageFillPrice(), 45050.0, 0.001);
}

// Test applying report to order - failure case
TEST_F(ExecutionReportTest, ApplyToOrderFailure) {
    auto order = std::make_shared<Order>("test-client-id");
    order->setOrderId("test-order-id");
    
    // Create report for a different order
    ExecutionReport report("different-order-id");
    
    // Applying to the wrong order should fail
    EXPECT_FALSE(report.applyToOrder(order));
}

// Test applying report with extra data
TEST_F(ExecutionReportTest, ApplyExtraData) {
    auto order = std::make_shared<Order>("test-client-id");
    order->setOrderId("test-order-id");
    
    ExecutionReport report(order->getOrderId());
    report.setExtraData("test-key-1", "test-value-1");
    report.setExtraData("test-key-2", "test-value-2");
    
    EXPECT_TRUE(report.applyToOrder(order));
    
    // Check that extra data was copied to the order
    EXPECT_EQ(order->getExtraParams().at("test-key-1"), "test-value-1");
    EXPECT_EQ(order->getExtraParams().at("test-key-2"), "test-value-2");
}

// Test applying reports with different state transitions
TEST_F(ExecutionReportTest, ApplyStateTransitions) {
    // Test cancellation
    {
        auto order = std::make_shared<Order>("client-id-1");
        order->setOrderId("order-id-1");
        order->open();
        
        ExecutionReport report(order->getOrderId());
        report.setStatus(OrderState::CANCELED);
        
        EXPECT_TRUE(report.applyToOrder(order));
        EXPECT_EQ(order->getState(), OrderState::CANCELED);
    }
    
    // Test rejection
    {
        auto order = std::make_shared<Order>("client-id-2");
        order->setOrderId("order-id-2");
        
        ExecutionReport report(order->getOrderId());
        report.setStatus(OrderState::REJECTED);
        report.setRejectReason("Price outside allowed range");
        
        EXPECT_TRUE(report.applyToOrder(order));
        EXPECT_EQ(order->getState(), OrderState::REJECTED);
    }
    
    // Test expiration
    {
        auto order = std::make_shared<Order>("client-id-3");
        order->setOrderId("order-id-3");
        order->open();
        
        ExecutionReport report(order->getOrderId());
        report.setStatus(OrderState::EXPIRED);
        
        EXPECT_TRUE(report.applyToOrder(order));
        EXPECT_EQ(order->getState(), OrderState::EXPIRED);
    }
}

// Test toString method
TEST_F(ExecutionReportTest, ToString) {
    ExecutionReport report("test-order-id");
    report.setClientOrderId("test-client-id");
    report.setExchangeOrderId("test-exchange-order-id");
    report.setExchangeId("binance");
    report.setSymbol("BTC/USDT");
    report.setSide(OrderSide::BUY);
    report.setType(OrderType::LIMIT);
    report.setStatus(OrderState::PARTIALLY_FILLED);
    report.setQuantity(1.0);
    report.setFilledQuantity(0.5);
    report.setPrice(45000.0);
    report.setAverageFillPrice(45100.0);
    
    std::string reportStr = report.toString();
    
    EXPECT_TRUE(reportStr.find("test-order-id") != std::string::npos);
    EXPECT_TRUE(reportStr.find("test-client-id") != std::string::npos);
    EXPECT_TRUE(reportStr.find("test-exchange-order-id") != std::string::npos);
    EXPECT_TRUE(reportStr.find("binance") != std::string::npos);
    EXPECT_TRUE(reportStr.find("BTC/USDT") != std::string::npos);
    EXPECT_TRUE(reportStr.find("BUY") != std::string::npos);
    EXPECT_TRUE(reportStr.find("LIMIT") != std::string::npos);
    EXPECT_TRUE(reportStr.find("PARTIALLY_FILLED") != std::string::npos);
    EXPECT_TRUE(reportStr.find("45000") != std::string::npos);
    EXPECT_TRUE(reportStr.find("45100") != std::string::npos);
}

// Test isRejected and isComplete methods
TEST_F(ExecutionReportTest, StatusChecks) {
    // Rejected report
    {
        ExecutionReport report;
        report.setStatus(OrderState::REJECTED);
        
        EXPECT_TRUE(report.isRejected());
        EXPECT_TRUE(report.isComplete());
    }
    
    // Filled report
    {
        ExecutionReport report;
        report.setStatus(OrderState::FILLED);
        
        EXPECT_FALSE(report.isRejected());
        EXPECT_TRUE(report.isComplete());
    }
    
    // Partially filled report
    {
        ExecutionReport report;
        report.setStatus(OrderState::PARTIALLY_FILLED);
        
        EXPECT_FALSE(report.isRejected());
        EXPECT_FALSE(report.isComplete());
    }
    
    // Canceled report
    {
        ExecutionReport report;
        report.setStatus(OrderState::CANCELED);
        
        EXPECT_FALSE(report.isRejected());
        EXPECT_FALSE(report.isComplete());
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}