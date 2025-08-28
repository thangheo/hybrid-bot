/**
 * @file test_execution_engine_integration.cpp
 * @brief Integration tests for the Execution Engine
 * 
 * Created on: May 6, 2025
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include <thread>
 #include <chrono>
 
 // Include real implementations
 #include "execution/execution_service.hpp"
 #include "execution/execution_types.hpp"
 #include "execution/order_generator.hpp"
 #include "execution/smart_order_router.hpp"
 #include "execution/order_type_selector.hpp"
 #include "execution/execution_algorithm_factory.hpp"
 #include "execution/order_lifecycle_tracker.hpp"
 #include "execution/fill_reconciliation.hpp"
 #include "execution/api_client_manager.hpp"
 
 // Include mock exchange API client
 #include "mocks/mock_exchange_api_client.hpp"
 
 using namespace trading::execution;
 using ::testing::_;
 using ::testing::Return;
 using ::testing::NiceMock;
 using ::testing::Invoke;
 
 class ExecutionEngineIntegrationTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create real components
         order_generator = std::make_shared<OrderGenerator>();
         smart_order_router = std::make_shared<SmartOrderRouter>();
         order_type_selector = std::make_shared<OrderTypeSelector>();
         execution_algorithm_factory = std::make_shared<ExecutionAlgorithmFactory>();
         order_lifecycle_tracker = std::make_shared<OrderLifecycleTracker>();
         fill_reconciliation = std::make_shared<FillReconciliation>(order_lifecycle_tracker);
         api_client_manager = std::make_shared<ApiClientManager>();
         
         // Create a mock exchange client
         mock_exchange_client = std::make_shared<NiceMock<MockExchangeApiClient>>();
         
         // Register the mock client with the manager
         api_client_manager->registerClient("binance", mock_exchange_client);
         
         // Create execution service
         execution_service = std::make_shared<ExecutionService>(
             order_generator,
             smart_order_router,
             order_type_selector,
             execution_algorithm_factory,
             order_lifecycle_tracker,
             fill_reconciliation,
             api_client_manager
         );
         
         // Configure mock exchange client behavior
         setupMockExchangeClient();
         
         // Initialize the execution service
         execution_service->initialize();
     }
     
     void TearDown() override {
         // Shutdown the service
         execution_service->shutdown();
     }
     
     void setupMockExchangeClient() {
         // Setup place order behavior
         ON_CALL(*mock_exchange_client, placeOrder(_))
             .WillByDefault(Invoke([](const Order& order) -> ExchangeApiClient::OrderResult {
                 ExchangeApiClient::OrderResult result;
                 result.success = true;
                 result.exchange_order_id = "ex_" + order.getClientOrderId();
                 result.error_message = "";
                 return result;
             }));
         
         // Setup cancel order behavior
         ON_CALL(*mock_exchange_client, cancelOrder(_))
             .WillByDefault(Invoke([](const Order& order) -> ExchangeApiClient::CancelResult {
                 ExchangeApiClient::CancelResult result;
                 result.success = true;
                 result.error_message = "";
                 return result;
             }));
         
         // Setup get order status behavior
         ON_CALL(*mock_exchange_client, getOrderStatus(_))
             .WillByDefault(Invoke([](const Order& order) -> ExchangeApiClient::OrderStatusResult {
                 ExchangeApiClient::OrderStatusResult result;
                 result.success = true;
                 result.state = order.getState();
                 result.filled_quantity = order.getFilledQuantity();
                 result.error_message = "";
                 return result;
             }));
         
         // Setup other necessary behaviors
         ON_CALL(*mock_exchange_client, initialize())
             .WillByDefault(Return(true));
         
         ON_CALL(*mock_exchange_client, getExchangeId())
             .WillByDefault(Return("binance"));
         
         ON_CALL(*mock_exchange_client, getHealthStatus())
             .WillByDefault(Return("healthy"));
     }
     
     // Helper to create a test order
     Order createTestOrder(const std::string& client_order_id = "test_order_1") {
         Order order(client_order_id);
         order.setTradingPair("BTC/USD");
         order.setSide(OrderSide::BUY);
         order.setType(OrderType::LIMIT);
         order.setQuantity(1.0);
         order.setPrice(50000.0);
         order.setTimeInForce(TimeInForce::GTC);
         order.setExchangeId("binance");
         return order;
     }
     
     // Helper to create a test signal
     TradingSignal createTestSignal() {
         TradingSignal signal;
         signal.signal_id = "test_signal_1";
         signal.trading_pair = "BTC/USD";
         signal.direction = OrderSide::BUY;
         signal.strength = 0.8;
         signal.target_size = 1.0;
         signal.signal_ttl = std::chrono::milliseconds(60000);
         signal.strategy_id = "order_imbalance";
         signal.strategy_version = "1.0";
         signal.signal_metrics["bid_ask_ratio"] = 1.2;
         return signal;
     }
     
     // Components
     std::shared_ptr<OrderGenerator> order_generator;
     std::shared_ptr<SmartOrderRouter> smart_order_router;
     std::shared_ptr<OrderTypeSelector> order_type_selector;
     std::shared_ptr<ExecutionAlgorithmFactory> execution_algorithm_factory;
     std::shared_ptr<OrderLifecycleTracker> order_lifecycle_tracker;
     std::shared_ptr<FillReconciliation> fill_reconciliation;
     std::shared_ptr<ApiClientManager> api_client_manager;
     std::shared_ptr<NiceMock<MockExchangeApiClient>> mock_exchange_client;
     
     // Service under test
     std::shared_ptr<ExecutionService> execution_service;
 };
 
 // Test the entire order lifecycle
 TEST_F(ExecutionEngineIntegrationTest, OrderLifecycle) {
     // 1. Place an order
     Order test_order = createTestOrder();
     auto place_response = execution_service->placeOrder(test_order);
     
     // Verify order was placed successfully
     EXPECT_TRUE(place_response.success);
     EXPECT_EQ(place_response.client_order_id, "test_order_1");
     EXPECT_EQ(place_response.exchange_order_id, "ex_test_order_1");
     EXPECT_EQ(place_response.order_status, "OPEN");
     
     // 2. Check order status
     auto status_response = execution_service->getOrderStatus("test_order_1");
     
     // Verify order status
     EXPECT_TRUE(status_response.found);
     EXPECT_EQ(status_response.order.getClientOrderId(), "test_order_1");
     EXPECT_EQ(status_response.order.getExchangeOrderId(), "ex_test_order_1");
     EXPECT_EQ(status_response.order.getState(), OrderState::OPEN);
     
     // 3. Get active orders
     auto active_orders = execution_service->getActiveOrders();
     
     // Verify active orders
     EXPECT_EQ(active_orders.size(), 1);
     EXPECT_EQ(active_orders[0].getClientOrderId(), "test_order_1");
     
     // 4. Simulate a fill
     // Set up the mock to produce a fill
     ExchangeApiClient::OrderStatusResult filled_result;
     filled_result.success = true;
     filled_result.state = OrderState::FILLED;
     filled_result.filled_quantity = 1.0;
     filled_result.average_price = 50000.0;
     filled_result.error_message = "";
     
     // Add a fill to the result
     Fill fill;
     fill.fill_id = "fill_1";
     fill.orderId = "test_order_1";
     fill.quantity = 1.0;
     fill.price = 50000.0;
     fill.timestamp = std::chrono::system_clock::now();
     filled_result.fills.push_back(fill);
     
     // Update the mock behavior for the next status check
     EXPECT_CALL(*mock_exchange_client, getOrderStatus(_))
         .WillOnce(Return(filled_result));
     
     // Check order status again to get the fill
     status_response = execution_service->getOrderStatus("test_order_1");
     
     // Verify order is now filled
     EXPECT_TRUE(status_response.found);
     EXPECT_EQ(status_response.order.getState(), OrderState::FILLED);
     EXPECT_EQ(status_response.order.getFilledQuantity(), 1.0);
     EXPECT_EQ(status_response.order.getFills().size(), 1);
     
     // 5. Check active orders again
     active_orders = execution_service->getActiveOrders();
     
     // Verify no active orders (order is filled)
     EXPECT_EQ(active_orders.size(), 0);
     
     // 6. Check filled orders
     auto now = std::chrono::system_clock::now();
     auto one_hour_ago = now - std::chrono::hours(1);
     auto filled_orders = execution_service->getFilledOrders(one_hour_ago, now);
     
     // Verify filled orders
     EXPECT_EQ(filled_orders.size(), 1);
     EXPECT_EQ(filled_orders[0].getClientOrderId(), "test_order_1");
     EXPECT_EQ(filled_orders[0].getState(), OrderState::FILLED);
 }
 
 // Test executing a signal
 TEST_F(ExecutionEngineIntegrationTest, ExecuteSignal) {
     // Create a test signal
     TradingSignal test_signal = createTestSignal();
     
     // Execute the signal
     auto response = execution_service->executeSignal(test_signal);
     
     // Verify signal was executed successfully
     EXPECT_TRUE(response.success);
     EXPECT_FALSE(response.client_order_id.empty());
     EXPECT_FALSE(response.exchange_order_id.empty());
     EXPECT_EQ(response.order_status, "OPEN");
     
     // Check active orders
     auto active_orders = execution_service->getActiveOrders();
     
     // Verify active orders
     EXPECT_EQ(active_orders.size(), 1);
     EXPECT_EQ(active_orders[0].getTradingPair(), "BTC/USD");
     EXPECT_EQ(active_orders[0].getSide(), OrderSide::BUY);
 }
 
 // Main function to run the tests
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }