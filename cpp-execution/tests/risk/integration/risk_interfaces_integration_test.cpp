/**
 * Integration test for Risk Management System interfaces
 *
 * File: risk_interfaces_integration_test.cpp
 * Created: 2025-05-05
 */

 #include "interfaces/strategy_interface.hpp"
 #include "interfaces/execution_interface.hpp"
 #include "interfaces/data_interface.hpp"
 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include <memory>
 #include <thread>
 #include <chrono>
 
 namespace common {
 class MockZeroMQClient {
 public:
     MOCK_METHOD(bool, connect, (const std::string&, int), ());
     MOCK_METHOD(std::string, sendAndReceive, (const std::string&), ());
     MOCK_METHOD(std::string, receiveMessage, (int), ());
     MOCK_METHOD(void, subscribe, (const std::string&), ());
 };
 }
 
 class RiskInterfacesIntegrationTest : public ::testing::Test {
 protected:
     // Simplified test implementations of the interfaces
     class TestStrategyInterface : public risk::StrategyInterface {
     private:
         std::shared_ptr<common::MockZeroMQClient> mockClient_;
         bool initialized_ = false;
     public:
         TestStrategyInterface(std::shared_ptr<common::MockZeroMQClient> mockClient)
             : mockClient_(mockClient) {}
         
         bool initialize() override { initialized_ = true; return true; }
         bool sendRiskFeedback(const risk::StrategyId& strategyId, const std::string& feedback) override { 
             return initialized_; 
         }
         std::string getStrategyParameters(const risk::StrategyId& strategyId) override { 
             return initialized_ ? R"({"risk_threshold": 0.5, "max_position_size": 1.0})" : "{}"; 
         }
         bool updateStrategyParameters(const risk::StrategyId& strategyId, const std::string& parameters) override {
             return initialized_;
         }
         bool notifyRiskEvent(const risk::StrategyId& strategyId, const risk::RiskEvent& event) override {
             return initialized_;
         }
         std::vector<risk::StrategyId> getActiveStrategies() override {
             return initialized_ ? std::vector<risk::StrategyId>{"strategy1", "strategy2"} : std::vector<risk::StrategyId>{};
         }
         bool isStrategyActive(const risk::StrategyId& strategyId) override {
             return initialized_ && (strategyId == "strategy1" || strategyId == "strategy2");
         }
         void registerStrategyEventCallback(const std::function<void(const risk::StrategyId&, const std::string&)>& callback) override {}
     };
 
     class TestExecutionInterface : public risk::ExecutionInterface {
     private:
         std::shared_ptr<common::MockZeroMQClient> mockClient_;
         bool initialized_ = false;
     public:
         TestExecutionInterface(std::shared_ptr<common::MockZeroMQClient> mockClient)
             : mockClient_(mockClient) {}
         
         bool initialize() override { initialized_ = true; return true; }
         std::string submitOrder(const risk::OrderRequest& orderRequest) override {
             return initialized_ ? "order_123" : "";
         }
         bool cancelOrder(const std::string& orderId) override {
             return initialized_ && (orderId == "order_123");
         }
         int cancelAllOrders(const risk::ExchangeId& exchangeId) override {
             return initialized_ ? 2 : 0;
         }
         int cancelAllOrders(const risk::ExchangeId& exchangeId, const risk::TradingPair& tradingPair) override {
             return initialized_ ? 1 : 0;
         }
         std::string getOrderStatus(const std::string& orderId) override {
             return initialized_ ? R"({"status": "OPEN", "filled_quantity": 0})" : "{}";
         }
         void registerExecutionReportCallback(const std::function<void(const risk::ExecutionReport&)>& callback) override {}
         std::vector<risk::OrderRequest> getOpenOrders(const std::optional<risk::ExchangeId>& exchangeId) override {
             if (!initialized_) return {};
             
             risk::OrderRequest order;
             order.orderId = "order_123";
             order.exchangeId = "binance";
             order.tradingPair = "BTC/USDT";
             
             return {order};
         }
         bool isExchangeAvailable(const risk::ExchangeId& exchangeId) override {
             return initialized_ && (exchangeId == "binance");
         }
         std::string getExchangeStatus(const risk::ExchangeId& exchangeId) override {
             return initialized_ ? R"({"status": "online"})" : "{}";
         }
     };
 
     class TestDataInterface : public risk::DataInterface {
     private:
         std::shared_ptr<common::MockZeroMQClient> mockClient_;
         bool initialized_ = false;
     public:
         TestDataInterface(std::shared_ptr<common::MockZeroMQClient> mockClient)
             : mockClient_(mockClient) {}
         
         bool initialize() override { initialized_ = true; return true; }
         risk::MarketData getMarketData(const risk::TradingPair& tradingPair, 
                                     const std::optional<risk::ExchangeId>& exchangeId) override {
             if (!initialized_) return {};
             
             risk::MarketData data;
             data.tradingPair = tradingPair;
             data.exchangeId = exchangeId ? *exchangeId : "binance";
             data.price = 50000.0;
             data.bid = 49995.0;
             data.ask = 50005.0;
             
             return data;
         }
         double getCurrentPrice(const risk::TradingPair& tradingPair,
                              const std::optional<risk::ExchangeId>& exchangeId) override {
             return initialized_ ? 50000.0 : 0.0;
         }
         std::vector<std::pair<risk::Timestamp, double>> getHistoricalPrices(
             const risk::TradingPair& tradingPair,
             const std::string& interval,
             int count,
             const std::optional<risk::ExchangeId>& exchangeId) override {
             
             if (!initialized_) return {};
             
             std::vector<std::pair<risk::Timestamp, double>> prices;
             for (int i = 0; i < count; i++) {
                 prices.emplace_back(1619817600000 - i * 300000, 50000.0 - i * 100.0);
             }
             
             return prices;
         }
         double getVolatility(const risk::TradingPair& tradingPair,
                           const std::string& timeframe,
                           const std::optional<risk::ExchangeId>& exchangeId) override {
             return initialized_ ? 0.025 : 0.0;
         }
         std::vector<risk::OrderRequest> getOrderHistory(const risk::ExchangeId& exchangeId,
                                                      const risk::TradingPair& tradingPair,
                                                      int limit) override {
             if (!initialized_) return {};
             
             std::vector<risk::OrderRequest> orders;
             for (int i = 0; i < limit && i < 3; i++) {
                 risk::OrderRequest order;
                 order.orderId = "hist_order_" + std::to_string(i);
                 order.exchangeId = exchangeId;
                 order.tradingPair = tradingPair;
                 
                 orders.push_back(order);
             }
             
             return orders;
         }
         std::map<std::string, double> getExchangeBalances(const risk::ExchangeId& exchangeId) override {
             if (!initialized_) return {};
             
             return {
                 {"BTC", 1.5},
                 {"USDT", 50000.0}
             };
         }
         void registerMarketDataCallback(const std::function<void(const risk::MarketData&)>& callback) override {}
         bool subscribeMarketData(const risk::TradingPair& tradingPair,
                                const std::optional<risk::ExchangeId>& exchangeId) override {
             return initialized_;
         }
         bool unsubscribeMarketData(const risk::TradingPair& tradingPair,
                                  const std::optional<risk::ExchangeId>& exchangeId) override {
             return initialized_;
         }
         std::map<std::string, double> getTradingFees(const risk::ExchangeId& exchangeId,
                                                   const std::optional<risk::TradingPair>& tradingPair) override {
             if (!initialized_) return {};
             
             return {
                 {"maker", 0.001},
                 {"taker", 0.001}
             };
         }
     };
 
     void SetUp() override {
         mockStrategyClient_ = std::make_shared<common::MockZeroMQClient>();
         mockExecutionClient_ = std::make_shared<common::MockZeroMQClient>();
         mockDataClient_ = std::make_shared<common::MockZeroMQClient>();
         
         strategyInterface_ = std::make_unique<TestStrategyInterface>(mockStrategyClient_);
         executionInterface_ = std::make_unique<TestExecutionInterface>(mockExecutionClient_);
         dataInterface_ = std::make_unique<TestDataInterface>(mockDataClient_);
     }
     
     std::shared_ptr<common::MockZeroMQClient> mockStrategyClient_;
     std::shared_ptr<common::MockZeroMQClient> mockExecutionClient_;
     std::shared_ptr<common::MockZeroMQClient> mockDataClient_;
     
     std::unique_ptr<TestStrategyInterface> strategyInterface_;
     std::unique_ptr<TestExecutionInterface> executionInterface_;
     std::unique_ptr<TestDataInterface> dataInterface_;
     
     // Helper method to create a sample order request
     risk::OrderRequest createSampleOrderRequest() {
         risk::OrderRequest order;
         order.orderId = "test_order_123";
         order.clientOrderId = "client_order_123";
         order.exchangeId = "binance";
         order.tradingPair = "BTC/USDT";
         order.side = risk::OrderSide::BUY;
         order.type = "LIMIT";
         order.quantity = 1.0;
         order.price = 50000.0;
         order.timeInForce = "GTC";
         order.strategyId = "strategy1";
         order.signalId = "signal123";
         order.timestamp = 1619817600000;
         return order;
     }
 
     // Helper to create a risk event
     risk::RiskEvent createRiskEvent(const std::string& type, const std::string& severity,
                                 const std::string& message, const std::string& data) {
         risk::RiskEvent event;
         event.type = type;
         event.severity = severity;
         event.message = message;
         event.data = data;
         return event;
     }
 };
 
 // Test initialization of all interfaces
 TEST_F(RiskInterfacesIntegrationTest, InitializeAllInterfaces) {
     EXPECT_TRUE(strategyInterface_->initialize());
     EXPECT_TRUE(executionInterface_->initialize());
     EXPECT_TRUE(dataInterface_->initialize());
 }
 
 // Test a complete risk check and order submission workflow
 TEST_F(RiskInterfacesIntegrationTest, CompleteRiskWorkflow) {
     // Initialize all interfaces
     strategyInterface_->initialize();
     executionInterface_->initialize();
     dataInterface_->initialize();
     
     // 1. Get strategy parameters to check risk limits
     std::string strategyParams = strategyInterface_->getStrategyParameters("strategy1");
     EXPECT_FALSE(strategyParams.empty());
     
     // 2. Get market data to check current price
     auto marketData = dataInterface_->getMarketData("BTC/USDT", "binance");
     EXPECT_GT(marketData.price, 0.0);
     
     // 3. Get exchange balances to check available funds
     auto balances = dataInterface_->getExchangeBalances("binance");
     EXPECT_GT(balances["USDT"], 0.0);
     
     // 4. Check volatility to adjust risk
     double volatility = dataInterface_->getVolatility("BTC/USDT", "1d", "binance");
     EXPECT_GT(volatility, 0.0);
     
     // 5. Get current open orders to check exposure
     auto openOrders = executionInterface_->getOpenOrders("binance");
     
     // 6. Create and submit an order
     risk::OrderRequest order = createSampleOrderRequest();
     std::string orderId = executionInterface_->submitOrder(order);
     EXPECT_FALSE(orderId.empty());
     
     // 7. Check order status
     std::string orderStatus = executionInterface_->getOrderStatus(orderId);
     EXPECT_FALSE(orderStatus.empty());
     
     // 8. Notify strategy about risk event
     risk::RiskEvent event = createRiskEvent("exposure_limit", "warning", 
                                         "Approaching exposure limit", 
                                         R"({"current": 0.8, "limit": 1.0})");
     EXPECT_TRUE(strategyInterface_->notifyRiskEvent("strategy1", event));
     
     // 9. Update strategy parameters to reduce risk
     EXPECT_TRUE(strategyInterface_->updateStrategyParameters("strategy1", 
                                                         R"({"risk_threshold": 0.4, "max_position_size": 0.8})"));
     
     // 10. Cancel the order
     EXPECT_TRUE(executionInterface_->cancelOrder(orderId));
 }
 
 // Test handling market data for risk monitoring
 TEST_F(RiskInterfacesIntegrationTest, MarketDataMonitoring) {
     // Initialize all interfaces
     strategyInterface_->initialize();
     executionInterface_->initialize();
     dataInterface_->initialize();
     
     // 1. Subscribe to market data for monitoring
     EXPECT_TRUE(dataInterface_->subscribeMarketData("BTC/USDT", "binance"));
     
     // 2. Get historical prices to calculate risk metrics
     auto historicalPrices = dataInterface_->getHistoricalPrices("BTC/USDT", "5m", 5, "binance");
     EXPECT_EQ(historicalPrices.size(), 5);
     
     // 3. Get trading fees to calculate costs
     auto fees = dataInterface_->getTradingFees("binance", "BTC/USDT");
     EXPECT_GT(fees["maker"], 0.0);
     
     // 4. Check exchange status is healthy
     std::string exchangeStatus = executionInterface_->getExchangeStatus("binance");
     EXPECT_FALSE(exchangeStatus.empty());
     
     // 5. Unsubscribe from market data when done
     EXPECT_TRUE(dataInterface_->unsubscribeMarketData("BTC/USDT", "binance"));
 }
 
 // Test emergency risk response
 TEST_F(RiskInterfacesIntegrationTest, EmergencyRiskResponse) {
     // Initialize all interfaces
     strategyInterface_->initialize();
     executionInterface_->initialize();
     dataInterface_->initialize();
     
     // 1. Detect extreme volatility
     double volatility = dataInterface_->getVolatility("BTC/USDT", "5m", "binance");
     EXPECT_GT(volatility, 0.0);
     
     // 2. Check if strategy is active
     EXPECT_TRUE(strategyInterface_->isStrategyActive("strategy1"));
     
     // 3. Create critical risk event
     risk::RiskEvent event = createRiskEvent("extreme_volatility", "critical", 
                                         "Market experiencing extreme volatility", 
                                         R"({"volatility": 0.15, "threshold": 0.05})");
     
     // 4. Notify strategy of critical event
     EXPECT_TRUE(strategyInterface_->notifyRiskEvent("strategy1", event));
     
     // 5. Cancel all orders for emergency response
     int cancelledOrders = executionInterface_->cancelAllOrders("binance", "BTC/USDT");
     EXPECT_GT(cancelledOrders, 0);
     
     // 6. Check balances after emergency action
     auto balances = dataInterface_->getExchangeBalances("binance");
     EXPECT_GT(balances["USDT"], 0.0);
 }
 
 // Run all tests
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }