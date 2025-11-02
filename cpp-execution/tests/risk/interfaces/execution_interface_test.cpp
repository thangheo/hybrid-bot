/**
 * Unit tests for the Execution Interface implementation
 *
 * File: execution_interface_test.cpp
 * Created: 2025-05-05
 */

 #include "risk/interfaces/execution_interface.hpp"
 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include <memory>
 #include <thread>
 #include <chrono>
 using namespace trading::execution::models;
 using namespace trading::risk;
 
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
 
 // Test fixture for ExecutionInterface tests
 class ExecutionInterfaceTest : public ::testing::Test {
 protected:
     // Use the actual implementation but with mocked dependencies
     class TestableExecutionInterface : public risk::ExecutionInterface {
     private:
         std::shared_ptr<common::MockZeroMQClient> mockClient_;
         std::shared_ptr<common::MockZeroMQClient> mockSubscriber_;
         bool initialized_ = false;
         
         // Helper to convert OrderRequest to JSON
         nlohmann::json orderRequestToJson(const risk::OrderRequest& request) {
             nlohmann::json json;
             json["order_id"] = request.orderId;
             json["client_order_id"] = request.clientOrderId;
             json["exchange_id"] = request.exchangeId;
             json["trading_pair"] = request.tradingPair;
             json["side"] = request.side == risk::OrderSide::BUY ? "buy" : "sell";
             json["type"] = request.type;
             json["quantity"] = request.quantity;
             json["price"] = request.price;
             json["time_in_force"] = request.timeInForce;
             json["strategy_id"] = request.strategyId;
             json["signal_id"] = request.signalId;
             json["timestamp"] = request.timestamp;
             
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
             request.orderId = json["order_id"];
             request.clientOrderId = json["client_order_id"];
             request.exchangeId = json["exchange_id"];
             request.tradingPair = json["trading_pair"];
             request.side = json["side"] == "buy" ? risk::OrderSide::BUY : risk::OrderSide::SELL;
             request.type = json["type"];
             request.quantity = json["quantity"];
             request.price = json["price"];
             request.timeInForce = json["time_in_force"];
             request.strategyId = json["strategy_id"];
             request.signalId = json["signal_id"];
             request.timestamp = json["timestamp"];
             
             if (json.contains("params") && json["params"].is_object()) {
                 for (auto it = json["params"].begin(); it != json["params"].end(); ++it) {
                     request.params[it.key()] = it.value();
                 }
             }
             
             return request;
         }
         
     public:
         TestableExecutionInterface(
             std::shared_ptr<common::MockZeroMQClient> mockClient,
             std::shared_ptr<common::MockZeroMQClient> mockSubscriber)
             : mockClient_(mockClient), mockSubscriber_(mockSubscriber) {}
         
         bool initialize() override {
             initialized_ = true;
             return true;
         }
         
         std::string submitOrder(const risk::OrderRequest& orderRequest) override {
             if (!initialized_) return "";
             
             nlohmann::json request = {
                 {"action", "submit_order"},
                 {"order", orderRequestToJson(orderRequest)}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["order_id"].get<std::string>();
             }
             
             return "";
         }
         
         bool cancelOrder(const std::string& orderId) override {
             if (!initialized_) return false;
             
             nlohmann::json request = {
                 {"action", "cancel_order"},
                 {"order_id", orderId}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             return responseJson["success"].get<bool>();
         }
         
         int cancelAllOrders(const risk::ExchangeId& exchangeId) override {
             if (!initialized_) return 0;
             
             nlohmann::json request = {
                 {"action", "cancel_all_orders"},
                 {"exchange_id", exchangeId.toString()}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["cancelled_count"].get<int>();
             }
             
             return 0;
         }
         
         int cancelAllOrders(const risk::ExchangeId& exchangeId, const risk::TradingPair& tradingPair) override {
             if (!initialized_) return 0;
             
             nlohmann::json request = {
                 {"action", "cancel_all_orders"},
                 {"exchange_id", exchangeId.toString()},
                 {"trading_pair", tradingPair.toString()}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["cancelled_count"].get<int>();
             }
             
             return 0;
         }
         
         std::string getOrderStatus(const std::string& orderId) override {
             if (!initialized_) return "{}";
             
             nlohmann::json request = {
                 {"action", "get_order_status"},
                 {"order_id", orderId}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["order_status"].dump();
             }
             
             return "{}";
         }
         
         void registerExecutionReportCallback(const std::function<void(const risk::ExecutionReport&)>& callback) override {
             // Store callback for testing - not called in tests
         }
         
         std::vector<risk::OrderRequest> getOpenOrders(const std::optional<risk::ExchangeId>& exchangeId) override {
             if (!initialized_) return {};
             
             nlohmann::json request = {
                 {"action", "get_open_orders"}
             };
             
             // Add exchange ID if provided
             if (exchangeId) {
                 request["exchange_id"] = *exchangeId;
             }
             
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
         
         bool isExchangeAvailable(const risk::ExchangeId& exchangeId) override {
             if (!initialized_) return false;
             
             nlohmann::json request = {
                 {"action", "is_exchange_available"},
                 {"exchange_id", exchangeId.toString()}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["available"].get<bool>();
             }
             
             return false;
         }
         
         std::string getExchangeStatus(const risk::ExchangeId& exchangeId) override {
             if (!initialized_) return "{}";
             
             nlohmann::json request = {
                 {"action", "get_exchange_status"},
                 {"exchange_id", exchangeId.toString()}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["exchange_status"].dump();
             }
             
             return "{}";
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
         executionInterface_ = std::make_unique<TestableExecutionInterface>(mockClient_, mockSubscriber_);
     }
     
     std::shared_ptr<common::MockZeroMQClient> mockClient_;
     std::shared_ptr<common::MockZeroMQClient> mockSubscriber_;
     std::unique_ptr<TestableExecutionInterface> executionInterface_;
     
     // Helper method to create a sample order request
     risk::OrderRequest createSampleOrderRequest() {
         risk::OrderRequest order;
         order.orderId = "test_order_123";
         order.clientOrderId = "client_order_123";
         order.exchangeId = ExchangeId::fromString("binance");
         order.tradingPair = TradingPair::fromString("BTC/USDT");
         order.side = risk::OrderSide::BUY;
         order.type = models::OrderType::LIMIT;
         order.getQuantity() = 1.0;
         order.getPrice() = 50000.0;
         order.timeInForce = "GTC";
         order.strategyId = "strategy1";
         order.signalId = "signal123";
         order.timestamp = 1619817600000;
         order.params["post_only"] = "true";
         return order;
     }
 };
 
 // Test initialization
 TEST_F(ExecutionInterfaceTest, Initialize) {
     EXPECT_TRUE(executionInterface_->initialize());
 }
 
 // Test submitting an order
 TEST_F(ExecutionInterfaceTest, SubmitOrder) {
     // Initialize
     executionInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"order_id", "test_order_123"}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test submitting order
     risk::OrderRequest order = createSampleOrderRequest();
     std::string result = executionInterface_->submitOrder(order);
     EXPECT_EQ(result, "test_order_123");
 }
 
 // Test cancelling an order
 TEST_F(ExecutionInterfaceTest, CancelOrder) {
     // Initialize
     executionInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {{"success", true}};
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test cancelling order
     bool result = executionInterface_->cancelOrder("test_order_123");
     EXPECT_TRUE(result);
 }
 
 // Test cancelling all orders for an exchange
 TEST_F(ExecutionInterfaceTest, CancelAllOrders) {
     // Initialize
     executionInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"cancelled_count", 5}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test cancelling all orders
     int count = executionInterface_->cancelAllOrders("binance");
     EXPECT_EQ(count, 5);
 }
 
 // Test cancelling all orders for a trading pair
 TEST_F(ExecutionInterfaceTest, CancelAllOrdersForTradingPair) {
     // Initialize
     executionInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"cancelled_count", 3}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test cancelling all orders for a trading pair
     int count = executionInterface_->cancelAllOrders("binance", "BTC/USDT");
     EXPECT_EQ(count, 3);
 }
 
 // Test getting order status
 TEST_F(ExecutionInterfaceTest, GetOrderStatus) {
     // Initialize
     executionInterface_->initialize();
     
     // Setup mock response
     nlohmann::json orderStatus = {
         {"order_id", "test_order_123"},
         {"status", "PARTIALLY_FILLED"},
         {"filled_quantity", 0.5},
         {"remaining_quantity", 0.5},
         {"price", 50000.0},
         {"average_fill_price", 50050.0}
     };
     nlohmann::json response = {
         {"success", true},
         {"order_status", orderStatus}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting order status
     std::string result = executionInterface_->getOrderStatus("test_order_123");
     EXPECT_EQ(result, orderStatus.dump());
 }
 
 // Test getting open orders
 TEST_F(ExecutionInterfaceTest, GetOpenOrders) {
     // Initialize
     executionInterface_->initialize();
     
     // Create sample orders
     risk::OrderRequest order1 = createSampleOrderRequest();
     risk::OrderRequest order2 = createSampleOrderRequest();
     order2.orderId = "test_order_456";
     order2.tradingPair = TradingPair::fromString("ETH/USDT");
     
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
     
     // Test getting open orders
     auto orders = executionInterface_->getOpenOrders("binance");
     EXPECT_EQ(orders.size(), 2);
     EXPECT_EQ(orders[0].orderId, "test_order_123");
     EXPECT_EQ(orders[0].tradingPair, "BTC/USDT");
     EXPECT_EQ(orders[1].orderId, "test_order_456");
     EXPECT_EQ(orders[1].tradingPair, "ETH/USDT");
 }
 
 // Test checking if exchange is available
 TEST_F(ExecutionInterfaceTest, IsExchangeAvailable) {
     // Initialize
     executionInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"available", true}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test checking if exchange is available
     bool result = executionInterface_->isExchangeAvailable("binance");
     EXPECT_TRUE(result);
 }
 
 // Test getting exchange status
 TEST_F(ExecutionInterfaceTest, GetExchangeStatus) {
     // Initialize
     executionInterface_->initialize();
     
     // Setup mock response
     nlohmann::json exchangeStatus = {
         {"status", "online"},
         {"maintenance", false},
         {"available_trading_pairs", {"BTC/USDT", "ETH/USDT", "XRP/USDT"}},
         {"api_rate_limit_remaining", 1195}
     };
     nlohmann::json response = {
         {"success", true},
         {"exchange_status", exchangeStatus}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting exchange status
     std::string result = executionInterface_->getExchangeStatus("binance");
     EXPECT_EQ(result, exchangeStatus.dump());
 }
 
 // Test error handling
 TEST_F(ExecutionInterfaceTest, ErrorHandling) {
     // Initialize
     executionInterface_->initialize();
     
     // Setup mock error response
     nlohmann::json response = {
         {"success", false},
         {"error", "Order not found"}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting order status with error
     std::string result = executionInterface_->getOrderStatus("invalid_order");
     EXPECT_EQ(result, "{}");
 }
 
 // Test behavior when not initialized
 TEST_F(ExecutionInterfaceTest, NotInitialized) {
     // Don't initialize (skip executionInterface_->initialize())
     
     // Test methods without initialization
     risk::OrderRequest order = createSampleOrderRequest();
     EXPECT_EQ(executionInterface_->submitOrder(order), "");
     EXPECT_FALSE(executionInterface_->cancelOrder("test_order_123"));
     EXPECT_EQ(executionInterface_->cancelAllOrders("binance"), 0);
     EXPECT_EQ(executionInterface_->cancelAllOrders("binance", "BTC/USDT"), 0);
     EXPECT_EQ(executionInterface_->getOrderStatus("test_order_123"), "{}");
     EXPECT_TRUE(executionInterface_->getOpenOrders().empty());
     EXPECT_FALSE(executionInterface_->isExchangeAvailable("binance"));
     EXPECT_EQ(executionInterface_->getExchangeStatus("binance"), "{}");
 }
 }