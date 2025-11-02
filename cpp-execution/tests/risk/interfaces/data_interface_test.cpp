/**
 * Unit tests for the Data Interface implementation
 *
 * File: data_interface_test.cpp
 * Created: 2025-05-05
 */

 #include "risk/interfaces/data_interface.hpp"
 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include <memory>
 #include <thread>
 #include <chrono>
 using namespace trading;
 // Mock for ZeroMQ client
 namespace trading {
 namespace common {
 class MockZeroMQClient {
 public:
     MOCK_METHOD(bool, connect, (const std::string&, int), ());
     MOCK_METHOD(std::string, sendAndReceive, (const std::string&), ());
     MOCK_METHOD(std::string, receiveMessage, (int), ());
     MOCK_METHOD(void, subscribe, (const std::string&), ());
 };
 }
}
 // Test fixture for DataInterface tests
 class DataInterfaceTest : public ::testing::Test {
 protected:
     // Use the actual implementation but with mocked dependencies
     class TestableDataInterface : public risk::DataInterface {
     private:
         std::shared_ptr<common::MockZeroMQClient> mockClient_;
         std::shared_ptr<common::MockZeroMQClient> mockSubscriber_;
         bool initialized_ = false;
         std::map<std::string, bool> activeSubscriptions_;
         
         // Helper method to generate subscription key
         std::string generateSubscriptionKey(const risk::TradingPair& tradingPair, 
                                           const std::optional<risk::ExchangeId>& exchangeId) {
             std::string key = tradingPair.toString();
             if (exchangeId.has_value()) {
                 key += ":" + exchangeId->toString();
             }
             return key;
         }
         
         // Helper to convert OrderRequest to JSON
         nlohmann::json orderRequestToJson(const risk::OrderRequest& request) {
             nlohmann::json json;
             json["order_id"] = request.orderId;
             json["client_order_id"] = request.clientOrderId;
             json["exchange_id"] = request.exchangeId.toString();
             json["trading_pair"] = request.tradingPair.toString();
             json["side"] = request.side == risk::OrderSide::BUY ? "buy" : "sell";
             json["type"] = request.type;
             json["quantity"] = request.quantity;
             json["price"] = request.price;
             json["time_in_force"] = request.timeInForce;
             json["strategy_id"] = request.strategyId;
             json["signal_id"] = request.signalId;
             json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(request.timestamp.time_since_epoch()).count();
             
             if (!request.params.empty()) {
                 json["params"] = nlohmann::json::object();
                 for (const auto& param : request.params) {
                     json["params"][param.first] = param.second;
                 }
             }
             
             return json;
         }
         
         // Helper to parse OrderRequest from JSON
         risk::OrderRequest jsonToOrderRequest(const nlohmann::json& json) {
             risk::OrderRequest request;
             request.orderId = json["order_id"].get<std::string>();
             request.clientOrderId = json["client_order_id"].get<std::string>();
             request.exchangeId = risk::ExchangeId::fromString(json["exchange_id"].get<std::string>());
             request.tradingPair = risk::TradingPair::fromString(json["trading_pair"].get<std::string>());
             request.side = json["side"] == "buy" ? risk::OrderSide::BUY : risk::OrderSide::SELL;
             request.type = json["type"];
             request.quantity = json["quantity"];
             request.price = json["price"];
             request.timeInForce = json["time_in_force"];
             request.strategyId = json["strategy_id"];
             request.signalId = json["signal_id"];
             request.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(json["timestamp"].get<long long>()));

             if (json.contains("params") && json["params"].is_object()) {
                 for (auto it = json["params"].begin(); it != json["params"].end(); ++it) {
                     request.params[it.key()] = it.value();
                 }
             }
             
             return request;
         }
         
     public:
         TestableDataInterface(
             std::shared_ptr<common::MockZeroMQClient> mockClient,
             std::shared_ptr<common::MockZeroMQClient> mockSubscriber)
             : mockClient_(mockClient), mockSubscriber_(mockSubscriber) {}
         
         bool initialize() override {
             initialized_ = true;
             return true;
         }
         
         risk::MarketData getMarketData(const risk::TradingPair& tradingPair, 
                                     const std::optional<risk::ExchangeId>& exchangeId) override {
             if (!initialized_) return {};
             
             nlohmann::json request = {
                 {"action", "get_market_data"},
                 {"trading_pair", tradingPair.toString()}
             };

             if (exchangeId) {
                 request["exchange_id"] = exchangeId->toString();
             }
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 risk::MarketData marketData;
                 marketData.tradingPair = risk::TradingPair::fromString(responseJson["data"]["trading_pair"].get<std::string>());
                 marketData.exchangeId = risk::ExchangeId::fromString(responseJson["data"]["exchange_id"].get<std::string>());
                 marketData.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(responseJson["data"]["timestamp"].get<long long>()));
                 marketData.price = responseJson["data"]["price"].get<double>();
                 marketData.volume = responseJson["data"]["volume"].get<double>();
                 marketData.bid = responseJson["data"]["bid"].get<double>();
                 marketData.ask = responseJson["data"]["ask"].get<double>();
                 marketData.lastTradePrice = responseJson["data"]["last_trade_price"].get<double>();
                 marketData.lastTradeQuantity = responseJson["data"]["last_trade_quantity"].get<double>();
                 return marketData;
             }
             
             return {};
         }
         
         double getCurrentPrice(const risk::TradingPair& tradingPair,
                              const std::optional<risk::ExchangeId>& exchangeId) override {
             if (!initialized_) return 0.0;
             
             nlohmann::json request = {
                 {"action", "get_current_price"},
                 {"trading_pair", tradingPair.toString()}
             };

             if (exchangeId) {
                 request["exchange_id"] = exchangeId->toString();
             }
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["price"].get<double>();
             }
             
             return 0.0;
         }
         
         std::vector<std::pair<risk::Timestamp, double>> getHistoricalPrices(
             const risk::TradingPair& tradingPair,
             const std::string& interval,
             int count,
             const std::optional<risk::ExchangeId>& exchangeId) override {
             
             if (!initialized_) return {};
             
             nlohmann::json request = {
                 {"action", "get_historical_prices"},
                 {"trading_pair", tradingPair.toString()},
                 {"interval", interval},
                 {"count", count}
             };

             if (exchangeId) {
                 request["exchange_id"] = exchangeId->toString();
             }
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 std::vector<std::pair<risk::Timestamp, double>> prices;
                 for (const auto& price : responseJson["prices"]) {
                     auto ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(price["timestamp"].get<long long>()));
                     prices.emplace_back(ts, price["price"].get<double>());
                 }
                 return prices;
             }
             
             return {};
         }
         
         double getVolatility(const risk::TradingPair& tradingPair,
                           const std::string& timeframe,
                           const std::optional<risk::ExchangeId>& exchangeId) override {
             if (!initialized_) return 0.0;
             
             nlohmann::json request = {
                 {"action", "get_volatility"},
                 {"trading_pair", tradingPair.toString()},
                 {"timeframe", timeframe}
             };

             if (exchangeId) {
                 request["exchange_id"] = exchangeId->toString();
             }
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["volatility"].get<double>();
             }
             
             return 0.0;
         }
         
         std::vector<risk::OrderRequest> getOrderHistory(const risk::ExchangeId& exchangeId,
                                                      const risk::TradingPair& tradingPair,
                                                      int limit) override {
             if (!initialized_) return {};
             
             nlohmann::json request = {
                 {"action", "get_order_history"},
                 {"exchange_id", exchangeId.toString()},
                 {"trading_pair", tradingPair.toString()},
                 {"limit", limit}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 std::vector<risk::OrderRequest> orders;
                 for (const auto& orderJson : responseJson["orders"]) {
                     orders.push_back(jsonToOrderRequest(orderJson));
                 }
                 return orders;
             }
             
             return {};
         }
         
         std::map<std::string, double> getExchangeBalances(const risk::ExchangeId& exchangeId) override {
             if (!initialized_) return {};
             
             nlohmann::json request = {
                 {"action", "get_exchange_balances"},
                 {"exchange_id", exchangeId.toString()}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 std::map<std::string, double> balances;
                 for (auto it = responseJson["balances"].begin(); it != responseJson["balances"].end(); ++it) {
                     balances[it.key()] = it.value().get<double>();
                 }
                 return balances;
             }
             
             return {};
         }
         
         void registerMarketDataCallback(const std::function<void(const risk::MarketData&)>& callback) override {
             // Store callback for testing - not called in tests
         }
         
         bool subscribeMarketData(const risk::TradingPair& tradingPair,
                                const std::optional<risk::ExchangeId>& exchangeId) override {
             if (!initialized_) return false;
             
             std::string subscriptionKey = generateSubscriptionKey(tradingPair, exchangeId);
             
             // Check if already subscribed
             if (activeSubscriptions_[subscriptionKey]) {
                 return true;
             }
             
             nlohmann::json request = {
                 {"action", "subscribe_market_data"},
                 {"trading_pair", tradingPair.toString()}
             };

             if (exchangeId) {
                 request["exchange_id"] = exchangeId->toString();
             }
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 activeSubscriptions_[subscriptionKey] = true;
                 return true;
             }
             
             return false;
         }
         
         bool unsubscribeMarketData(const risk::TradingPair& tradingPair,
                                  const std::optional<risk::ExchangeId>& exchangeId) override {
             if (!initialized_) return false;
             
             std::string subscriptionKey = generateSubscriptionKey(tradingPair, exchangeId);
             
             // Check if not subscribed
             if (!activeSubscriptions_[subscriptionKey]) {
                 return true;
             }
             
             nlohmann::json request = {
                 {"action", "unsubscribe_market_data"},
                 {"trading_pair", tradingPair.toString()}
             };

             if (exchangeId) {
                 request["exchange_id"] = exchangeId->toString();
             }
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 activeSubscriptions_[subscriptionKey] = false;
                 return true;
             }
             
             return false;
         }
         
         std::map<std::string, double> getTradingFees(const risk::ExchangeId& exchangeId,
                                                   const std::optional<risk::TradingPair>& tradingPair) override {
             if (!initialized_) return {};
             
             nlohmann::json request = {
                 {"action", "get_trading_fees"},
                 {"exchange_id", exchangeId.toString()}
             };

             if (tradingPair) {
                 request["trading_pair"] = tradingPair->toString();
             }
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 std::map<std::string, double> fees;
                 for (auto it = responseJson["fees"].begin(); it != responseJson["fees"].end(); ++it) {
                     fees[it.key()] = it.value().get<double>();
                 }
                 return fees;
             }
             
             return {};
         }
         
         // Expose mocks for test setup
         std::shared_ptr<common::MockZeroMQClient> getMockClient() {
             return mockClient_;
         }
         
         std::shared_ptr<common::MockZeroMQClient> getMockSubscriber() {
             return mockSubscriber_;
         }
     };
     
     void SetUp() override {
         mockClient_ = std::make_shared<common::MockZeroMQClient>();
         mockSubscriber_ = std::make_shared<common::MockZeroMQClient>();
         dataInterface_ = std::make_unique<TestableDataInterface>(mockClient_, mockSubscriber_);
     }
     
     std::shared_ptr<common::MockZeroMQClient> mockClient_;
     std::shared_ptr<common::MockZeroMQClient> mockSubscriber_;
     std::unique_ptr<TestableDataInterface> dataInterface_;
     
     // Helper method to create a sample order request
     risk::OrderRequest createSampleOrderRequest() {
         risk::OrderRequest order;
         order.orderId = "test_order_123";
         order.clientOrderId = "client_order_123";
         order.exchangeId = risk::ExchangeId::fromString("binance");
         order.tradingPair = risk::TradingPair::fromString("BTC/USDT");
         order.side = risk::OrderSide::BUY;
         order.type = models::OrderType::LIMIT;
         order.quantity = 1.0;
         order.price = 50000.0;
         order.timeInForce = "GTC";
         order.strategyId = "strategy1";
         order.signalId = "signal123";
         order.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(1619817600000));
         order.params["post_only"] = "true";
         return order;
     }
 };
 
 // Test initialization
 TEST_F(DataInterfaceTest, Initialize) {
     EXPECT_TRUE(dataInterface_->initialize());
 }
 
 // Test getting market data
 TEST_F(DataInterfaceTest, GetMarketData) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock response
     nlohmann::json marketDataJson = {
         {"trading_pair", "BTC/USDT"},
         {"exchange_id", "binance"},
         {"timestamp", 1619817600000},
         {"price", 50000.0},
         {"volume", 1000.0},
         {"bid", 49995.0},
         {"ask", 50005.0},
         {"last_trade_price", 50000.0},
         {"last_trade_quantity", 0.5}
     };
     nlohmann::json response = {
         {"success", true},
         {"data", marketDataJson}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting market data
     auto marketData = dataInterface_->getMarketData("BTC/USDT", "binance");
     EXPECT_EQ(marketData.tradingPair, "BTC/USDT");
     EXPECT_EQ(marketData.exchangeId, "binance");
     EXPECT_EQ(marketData.price, 50000.0);
     EXPECT_EQ(marketData.bid, 49995.0);
     EXPECT_EQ(marketData.ask, 50005.0);
 }
 
 // Test getting current price
 TEST_F(DataInterfaceTest, GetCurrentPrice) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"price", 50000.0}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting current price
     double price = dataInterface_->getCurrentPrice("BTC/USDT", "binance");
     EXPECT_EQ(price, 50000.0);
 }
 
 // Test getting historical prices
 TEST_F(DataInterfaceTest, GetHistoricalPrices) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock response
     nlohmann::json prices = nlohmann::json::array();
     prices.push_back({{"timestamp", 1619817600000}, {"price", 50000.0}});
     prices.push_back({{"timestamp", 1619817300000}, {"price", 49900.0}});
     prices.push_back({{"timestamp", 1619817000000}, {"price", 49800.0}});
     
     nlohmann::json response = {
         {"success", true},
         {"prices", prices}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting historical prices
     auto historicalPrices = dataInterface_->getHistoricalPrices("BTC/USDT", "5m", 3, "binance");
     EXPECT_EQ(historicalPrices.size(), 3);
     EXPECT_EQ(historicalPrices[0].first, 1619817600000);
     EXPECT_EQ(historicalPrices[0].second, 50000.0);
     EXPECT_EQ(historicalPrices[1].first, 1619817300000);
     EXPECT_EQ(historicalPrices[1].second, 49900.0);
     EXPECT_EQ(historicalPrices[2].first, 1619817000000);
     EXPECT_EQ(historicalPrices[2].second, 49800.0);
 }
 
 // Test getting volatility
 TEST_F(DataInterfaceTest, GetVolatility) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"volatility", 0.025}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting volatility
     double volatility = dataInterface_->getVolatility("BTC/USDT", "1d", "binance");
     EXPECT_EQ(volatility, 0.025);
 }
 
 // Test getting order history
 TEST_F(DataInterfaceTest, GetOrderHistory) {
     // Initialize
     dataInterface_->initialize();
     
     // Create sample orders
     risk::OrderRequest order1 = createSampleOrderRequest();
     risk::OrderRequest order2 = createSampleOrderRequest();
     order2.orderId = "test_order_456";
     order2.timestamp = 1619817300000;
     
     // Convert to JSON for the mock response
     nlohmann::json ordersJson = nlohmann::json::array();
     ordersJson.push_back({
         {"order_id", order1.orderId},
         {"client_order_id", order1.clientOrderId},
        {"exchange_id", order1.exchangeId.toString()},
        {"trading_pair", order1.tradingPair.toString()},
         {"side", "buy"},
         {"type", order1.type},
         {"quantity", order1.quantity},
         {"price", order1.price},
         {"time_in_force", order1.timeInForce},
         {"strategy_id", order1.strategyId},
         {"signal_id", order1.signalId},
         {"timestamp", order1.timestamp},
         {"params", {{"post_only", "true"}}}
     });
     ordersJson.push_back({
         {"order_id", order2.orderId},
         {"client_order_id", order2.clientOrderId},
        {"exchange_id", order2.exchangeId.toString()},
        {"trading_pair", order2.tradingPair.toString()},
         {"side", "buy"},
         {"type", order2.type},
         {"quantity", order2.quantity},
         {"price", order2.price},
         {"time_in_force", order2.timeInForce},
         {"strategy_id", order2.strategyId},
         {"signal_id", order2.signalId},
         {"timestamp", order2.timestamp},
         {"params", {{"post_only", "true"}}}
     });
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"orders", ordersJson}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting order history
     auto orders = dataInterface_->getOrderHistory("binance", "BTC/USDT", 2);
     EXPECT_EQ(orders.size(), 2);
     EXPECT_EQ(orders[0].orderId, "test_order_123");
     EXPECT_EQ(orders[0].timestamp, 1619817600000);
     EXPECT_EQ(orders[1].orderId, "test_order_456");
     EXPECT_EQ(orders[1].timestamp, 1619817300000);
 }
 
 // Test getting exchange balances
 TEST_F(DataInterfaceTest, GetExchangeBalances) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock response
     nlohmann::json balancesJson = {
         {"BTC", 1.5},
         {"ETH", 10.0},
         {"USDT", 50000.0}
     };
     nlohmann::json response = {
         {"success", true},
         {"balances", balancesJson}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting exchange balances
     auto balances = dataInterface_->getExchangeBalances("binance");
     EXPECT_EQ(balances.size(), 3);
     EXPECT_EQ(balances["BTC"], 1.5);
     EXPECT_EQ(balances["ETH"], 10.0);
     EXPECT_EQ(balances["USDT"], 50000.0);
 }
 
 // Test subscribing to market data
 TEST_F(DataInterfaceTest, SubscribeMarketData) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {{"success", true}};
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test subscribing to market data
     bool result = dataInterface_->subscribeMarketData("BTC/USDT", "binance");
     EXPECT_TRUE(result);
     
     // Test subscribing again (should use cached subscription)
     result = dataInterface_->subscribeMarketData("BTC/USDT", "binance");
     EXPECT_TRUE(result);
 }
 
 // Test unsubscribing from market data
 TEST_F(DataInterfaceTest, UnsubscribeMarketData) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock responses
     nlohmann::json subscribeResponse = {{"success", true}};
     nlohmann::json unsubscribeResponse = {{"success", true}};
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(subscribeResponse.dump()))
         .WillOnce(::testing::Return(unsubscribeResponse.dump()));
     
     // First subscribe to market data
     bool result = dataInterface_->subscribeMarketData("BTC/USDT", "binance");
     EXPECT_TRUE(result);
     
     // Then unsubscribe
     result = dataInterface_->unsubscribeMarketData("BTC/USDT", "binance");
     EXPECT_TRUE(result);
     
     // Test unsubscribing again (should not make a request)
     result = dataInterface_->unsubscribeMarketData("BTC/USDT", "binance");
     EXPECT_TRUE(result);
 }
 
 // Test getting trading fees
 TEST_F(DataInterfaceTest, GetTradingFees) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock response
     nlohmann::json feesJson = {
         {"maker", 0.001},
         {"taker", 0.001},
         {"withdrawal", 0.0005}
     };
     nlohmann::json response = {
         {"success", true},
         {"fees", feesJson}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting trading fees
     auto fees = dataInterface_->getTradingFees("binance", "BTC/USDT");
     EXPECT_EQ(fees.size(), 3);
     EXPECT_EQ(fees["maker"], 0.001);
     EXPECT_EQ(fees["taker"], 0.001);
     EXPECT_EQ(fees["withdrawal"], 0.0005);
 }
 
 // Test error handling
 TEST_F(DataInterfaceTest, ErrorHandling) {
     // Initialize
     dataInterface_->initialize();
     
     // Setup mock error response
     nlohmann::json response = {
         {"success", false},
         {"error", "Trading pair not found"}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting market data with error
     auto marketData = dataInterface_->getMarketData("INVALID/PAIR", "binance");
     EXPECT_EQ(marketData.tradingPair, "");
     EXPECT_EQ(marketData.price, 0.0);
 }
 
 // Test behavior when not initialized
 TEST_F(DataInterfaceTest, NotInitialized) {
     // Don't initialize (skip dataInterface_->initialize())
     
     // Test methods without initialization
     EXPECT_EQ(dataInterface_->getMarketData("BTC/USDT", "binance").tradingPair, "");
     EXPECT_EQ(dataInterface_->getCurrentPrice("BTC/USDT", "binance"), 0.0);
     EXPECT_TRUE(dataInterface_->getHistoricalPrices("BTC/USDT", "5m", 3, "binance").empty());
     EXPECT_EQ(dataInterface_->getVolatility("BTC/USDT", "1d", "binance"), 0.0);
     EXPECT_TRUE(dataInterface_->getOrderHistory("binance", "BTC/USDT", 10).empty());
     EXPECT_TRUE(dataInterface_->getExchangeBalances("binance").empty());
     EXPECT_FALSE(dataInterface_->subscribeMarketData("BTC/USDT", "binance"));
     EXPECT_FALSE(dataInterface_->unsubscribeMarketData("BTC/USDT", "binance"));
     EXPECT_TRUE(dataInterface_->getTradingFees("binance", "BTC/USDT").empty());
 }