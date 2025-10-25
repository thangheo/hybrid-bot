#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Allow test to access private members for white-box verification
#define private public
#include "risk/posttrade/post_trade_monitor.hpp"
#undef private

#include "risk/interfaces/data_interface.hpp"
#include "risk/interfaces/execution_interface.hpp"

using ::testing::Return;
using namespace trading::risk;

class MockExecutionInterface : public ExecutionInterface {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(std::string, submitOrder, (const OrderRequest&), (override));
    MOCK_METHOD(bool, cancelOrder, (const std::string&), (override));
    MOCK_METHOD(int, cancelAllOrders, (const core::ExchangeId&), (override));
    MOCK_METHOD(int, cancelAllOrders, (const core::ExchangeId&, const core::Symbol&), (override));
    MOCK_METHOD(std::string, getOrderStatus, (const std::string&), (override));
    MOCK_METHOD(std::vector<OrderRequest>, getOpenOrders, (const std::optional<std::string>&), (override));
    MOCK_METHOD((std::map<std::string,double>), getAccountBalances, (const core::ExchangeId&), (override));
    MOCK_METHOD((std::future<std::map<std::string,double>>), getAccountBalancesAsync, (const core::ExchangeId&), (override));
    MOCK_METHOD(bool, isExchangeAvailable, (const core::ExchangeId&), (override));
    MOCK_METHOD(std::string, getExchangeStatus, (const core::ExchangeId&), (override));
    MOCK_METHOD(void, registerExecutionReportCallback, (const std::function<void(const ExecutionReport&)>&), (override));
    MOCK_METHOD((std::map<std::string,double>), getPositions, (const core::ExchangeId&), (override));
    MOCK_METHOD((std::map<core::ExchangeId, std::map<std::string,double>>), getAllPositions, (), (override));
};

// Use existing MockDataInterface from headers or a simple stub
class SimpleMockDataInterface : public MockDataInterface {
public:
    bool isConnected() const override { return true; }
    bool initialize() override { return true; }
    double getCurrentPrice(const core::Symbol&, const std::optional<std::string>&) override { return 100.0; }
    std::vector<std::pair<Timestamp,double>> getHistoricalPrices(const core::Symbol&, const std::string&, int, const std::optional<std::string>&) override { return {}; }
    std::optional<MarketData> getMarketData(const core::Symbol&, const std::optional<std::string>&) override { return std::nullopt; }
    std::vector<Position> fetchCurrentPositions() override { return {}; }
};

TEST(PostTradeMonitorTTLBehavior, SlidingRenewalAndExpiryTriggersExit) {
    RiskConfig cfg;
    auto dataInterface = std::make_shared<SimpleMockDataInterface>();
    auto execInterface = std::make_shared<MockExecutionInterface>();

    // Create monitor with injected interfaces
    PostTradeMonitor monitor(cfg, nullptr, dataInterface, execInterface);
    ASSERT_TRUE(monitor.initialize());

    // Build a fake execution report representing a BUY fill to create a tracked position
    execution::models::ExecutionReport report;
    // set basic fields (uses public struct-like setters in this codebase)
    report.setExchangeId(core::ExchangeId::binance());
    report.setSymbol(core::Symbol("BTC/USDT"));
    report.setSide(core::Direction::BUY);

    // Add a fill to the report
    execution::models::Fill fill;
    fill.quantity = 1.0;
    fill.price = 100.0;
    report.addFill(fill);

    // Expect no immediate exit requests
    EXPECT_CALL(*execInterface, submitOrder(::testing::_)).Times(0);

    // Process the execution report to create tracked position
    monitor.processExecutionReport(report);

    // Now register a signal checker that returns true (BUY present)
    monitor.registerExternalSignalChecker([](const core::ExchangeId&, const core::Symbol&) {
        return true;
    });

    // Call enforcePositionTTL() directly (we used #define private public)
    monitor.enforcePositionTTL();

    // Inspect trackedPositions_ to ensure deadline was set in the future
    std::string key = core::ExchangeId::binance().toString() + ":" + core::Symbol("BTC/USDT").toString();
    ASSERT_TRUE(monitor.trackedPositions_.find(key) != monitor.trackedPositions_.end());
    auto tracked = monitor.trackedPositions_.at(key);
    auto now = core::now();
    EXPECT_GT(tracked.deadline, now);

    // Now simulate signal disappearance: register checker that returns false
    monitor.registerExternalSignalChecker([](const core::ExchangeId&, const core::Symbol&) {
        return false;
    });

    // Force the tracked deadline to be in the past so next enforcePositionTTL triggers exit
    {
        auto& entry = monitor.trackedPositions_.at(key);
        entry.deadline = core::now() - std::chrono::milliseconds(10);
    }

    // Expect submitOrder to be called once to exit the position
    EXPECT_CALL(*execInterface, submitOrder(::testing::_)).WillOnce(Return(std::string("exit-order-1")));

    // Run enforcement which should now call executionInterface->submitOrder
    monitor.enforcePositionTTL();
}
