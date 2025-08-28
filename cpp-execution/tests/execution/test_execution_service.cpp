/**
 * @file test_execution_service.cpp
 * @brief Unit tests for the ExecutionService class
 * 
 * Created on: May 6, 2025
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include "execution/execution_service.hpp"
 #include "execution/execution_types.hpp"
 // Include mock headers for dependencies
 #include "mocks/mock_order_generator.hpp"
 #include "mocks/mock_smart_order_router.hpp"
 #include "mocks/mock_order_type_selector.hpp"
 #include "mocks/mock_execution_algorithm_factory.hpp"
 #include "mocks/mock_order_lifecycle_tracker.hpp"
 #include "mocks/mock_fill_reconciliation.hpp"
 #include "mocks/mock_api_client_manager.hpp"
 #include "mocks/mock_exchange_api_client.hpp"
 
 using namespace trading::execution;
 using ::testing::_;
 using ::testing::Return;
 using ::testing::ReturnRef;
 using ::testing::NiceMock;
 using ::testing::AtLeast;
 using ::testing::DoAll;
 using ::testing::SetArgReferee;
 
 class ExecutionServiceTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create mock dependencies
         order_generator = std::make_shared<NiceMock<MockOrderGenerator>>();
         smart_order_router = std::make_shared<NiceMock<MockSmartOrderRouter>>();
         order_type_selector = std::make_shared<NiceMock<MockOrderTypeSelector>>();
         execution_algorithm_factory = std::make_shared<NiceMock<MockExecutionAlgorithmFactory>>();
         order_lifecycle_tracker = std::make_shared<NiceMock<MockOrderLifecycleTracker>>();
         fill_reconciliation = std::make_shared<NiceMock<MockFillReconciliation>>();
         api_client_manager = std::make_shared<NiceMock<MockApiClientManager>>();
         
         // Create exchange API client mock
         exchange_client = std::make_shared<NiceMock<MockExchangeApiClient>>();
         
         // Create service under test
         service = std::make_shared<ExecutionService>(
             order_generator,
             smart_order_router,
             order_type_selector,
             execution_algorithm_factory,
             order_lifecycle_tracker,
             fill_reconciliation,
             api_client_manager
         );
         
         // Setup default behaviors
         ON_CALL(*api_client_manager, getClient(_))
             .WillByDefault(Return(exchange_client));
         
         ON_CALL(*api_client_manager, initialize())
             .WillByDefault(Return(true));
             
         ON_CALL(*order_lifecycle_tracker, initialize())
             .WillByDefault(Return(true));
             
         ON_CALL(*fill_reconciliation, initialize())
             .WillByDefault(Return(true));
     }
 
     // Mock dependencies
     std::shared_ptr<MockOrderGenerator> order_generator;
     std::shared_ptr<MockSmartOrderRouter> smart_order_router;
     std::shared_ptr<MockOrderTypeSelector> order_type_selector;
     std::shared_ptr<MockExecutionAlgorithmFactory> execution_algorithm_factory;
     std::shared_ptr<MockOrderLifecycleTracker> order_lifecycle_tracker;
     std::shared_ptr<MockFillReconciliation> fill_reconciliation;
     std::shared_ptr<MockApiClientManager> api_client_manager;
     std::shared_ptr<MockExchangeApiClient> exchange_client;
     
     // Service under test
     std::shared_ptr<ExecutionService> service;
     
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
 };
 
 // Test initialization
 TEST_F(ExecutionServiceTest, Initialize) {
     // Expect component initializations to be called
     EXPECT_CALL(*api_client_manager, initialize()).Times(1);
     EXPECT_CALL(*order_lifecycle_tracker, initialize()).Times(1);
     EXPECT_CALL(*fill_reconciliation, initialize()).Times(1);
     EXPECT_CALL(*fill_reconciliation, start()).Times(1);
     
     // Initialize the service
     bool result = service->initialize();
     
     // Verify result
     EXPECT_TRUE(result);
 }
 
 // Test initialization failure
 TEST_F(ExecutionServiceTest, InitializeFailure) {
     // Make API client manager initialization fail
     EXPECT_CALL(*api_client_manager, initialize())
         .WillOnce(Return(false));
     
     // Expect fill reconciliation not to be started
     EXPECT_CALL(*fill_reconciliation, start()).Times(0);
     
     // Initialize the service
     bool result = service->initialize();
     
     // Verify result
     EXPECT_FALSE(result);
 }
 
 // Test shutdown
 TEST_F(ExecutionServiceTest, Shutdown) {
     // Initialize first
     service->initialize();
     
     // Expect component shutdowns to be called
     EXPECT_CALL(*fill_reconciliation, stop()).Times(1);
     EXPECT_CALL(*api_client_manager, shutdown()).Times(1);
     EXPECT_CALL(*order_lifecycle_tracker, shutdown()).Times(1);
     
     // Shutdown the service
     service->shutdown();
 }
 
 // Test place order success
 TEST_F(ExecutionServiceTest, PlaceOrderSuccess) {
     // Initialize service
     service->initialize();
     
     // Create test order
     Order test_order = createTestOrder();
     
     // Configure mock behavior
     // Exchange client will return success
     ExchangeApiClient::OrderResult success_result;
     success_result.success = true;
     success_result.exchange_order_id = "ex_order_123";
     success_result.error_message = "";
     
     EXPECT_CALL(*exchange_client, placeOrder(_))
         .WillOnce(Return(success_result));
     
     // Lifecycle tracker expectations
     EXPECT_CALL(*order_lifecycle_tracker, trackOrder(_)).Times(1);
     EXPECT_CALL(*order_lifecycle_tracker, updateOrderWithExchangeId(_, "ex_order_123")).Times(1);
     EXPECT_CALL(*order_lifecycle_tracker, updateOrderState(_, OrderState::OPEN, _)).Times(1);
     
     // Execute the test
     ExecutionResponse response = service->placeOrder(test_order);
     
     // Verify response
     EXPECT_TRUE(response.success);
     EXPECT_EQ(response.client_order_id, "test_order_1");
     EXPECT_EQ(response.exchange_order_id, "ex_order_123");
     EXPECT_EQ(response.order_status, "OPEN");
     EXPECT_TRUE(response.error_message.empty());
 }
 
 // Test place order failure
 TEST_F(ExecutionServiceTest, PlaceOrderFailure) {
     // Initialize service
     service->initialize();
     
     // Create test order
     Order test_order = createTestOrder();
     
     // Configure mock behavior
     // Exchange client will return failure
     ExchangeApiClient::OrderResult failure_result;
     failure_result.success = false;
     failure_result.exchange_order_id = "";
     failure_result.error_message = "Insufficient funds";
     
     EXPECT_CALL(*exchange_client, placeOrder(_))
         .WillOnce(Return(failure_result));
     
     // Lifecycle tracker expectations
     EXPECT_CALL(*order_lifecycle_tracker, trackOrder(_)).Times(1);
     EXPECT_CALL(*order_lifecycle_tracker, updateOrderState(_, OrderState::REJECTED, "Insufficient funds")).Times(1);
     
     // Execute the test
     ExecutionResponse response = service->placeOrder(test_order);
     
     // Verify response
     EXPECT_FALSE(response.success);
     EXPECT_EQ(response.client_order_id, "test_order_1");
     EXPECT_EQ(response.exchange_order_id, "");
     EXPECT_EQ(response.order_status, "REJECTED");
     EXPECT_EQ(response.error_message, "Insufficient funds");
 }
 
 // Test execute signal
 TEST_F(ExecutionServiceTest, ExecuteSignal) {
     // Initialize service
     service->initialize();
     
     // Create test signal
     TradingSignal test_signal = createTestSignal();
     
     // Create test order that will be generated from the signal
     Order test_order = createTestOrder();
     
     // Configure mock behavior
     EXPECT_CALL(*order_generator, generateOrder(_))
         .WillOnce(Return(test_order));
     
     EXPECT_CALL(*smart_order_router, selectExchange(_, _))
         .WillOnce(Return("binance"));
     
     EXPECT_CALL(*order_type_selector, selectOrderType(_, _))
         .WillOnce(Return(OrderType::LIMIT));
     
     // Exchange client will return success
     ExchangeApiClient::OrderResult success_result;
     success_result.success = true;
     success_result.exchange_order_id = "ex_order_123";
     success_result.error_message = "";
     
     EXPECT_CALL(*exchange_client, placeOrder(_))
         .WillOnce(Return(success_result));
     
     // Execute the test
     ExecutionResponse response = service->executeSignal(test_signal);
     
     // Verify response
     EXPECT_TRUE(response.success);
     EXPECT_EQ(response.exchange_order_id, "ex_order_123");
     EXPECT_EQ(response.order_status, "OPEN");
 }
 
 // Test cancel order
 TEST_F(ExecutionServiceTest, CancelOrder) {
     // Initialize service
     service->initialize();
     
     // Create test order
     Order test_order = createTestOrder();
     test_order.setExchangeOrderId("ex_order_123");
     test_order.open(); // Set state to OPEN
     
     // Configure mock behavior
     std::optional<Order> opt_order = test_order;
     
     EXPECT_CALL(*order_lifecycle_tracker, getOrder("test_order_1"))
         .WillOnce(Return(opt_order));
     
     // Exchange client will return success
     ExchangeApiClient::CancelResult success_result;
     success_result.success = true;
     success_result.error_message = "";
     
     EXPECT_CALL(*exchange_client, cancelOrder(_))
         .WillOnce(Return(success_result));
     
     // State transitions
     EXPECT_CALL(*order_lifecycle_tracker, updateOrderState("test_order_1", OrderState::PENDING_CANCEL)).Times(1);
     EXPECT_CALL(*order_lifecycle_tracker, updateOrderState("test_order_1", OrderState::CANCELED)).Times(1);
     
     // Execute the test
     CancelResponse response = service->cancelOrder("test_order_1");
     
     // Verify response
     EXPECT_TRUE(response.success);
     EXPECT_EQ(response.client_order_id, "test_order_1");
     EXPECT_EQ(response.exchange_order_id, "ex_order_123");
     EXPECT_TRUE(response.error_message.empty());
 }
 
 // Test cancel order - already filled
 TEST_F(ExecutionServiceTest, CancelOrderAlreadyFilled) {
     // Initialize service
     service->initialize();
     
     // Create test order that's already filled
     Order test_order = createTestOrder();
     test_order.setExchangeOrderId("ex_order_123");
     
     // Set state to FILLED (not cancellable)
     Fill fill;
     fill.fill_id = "fill_1";
     fill.orderId = "test_order_1";
     fill.quantity = 1.0;
     fill.price = 50000.0;
     fill.timestamp = std::chrono::system_clock::now();
     test_order.fill(fill);
     
     // Configure mock behavior
     std::optional<Order> opt_order = test_order;
     
     EXPECT_CALL(*order_lifecycle_tracker, getOrder("test_order_1"))
         .WillOnce(Return(opt_order));
     
     // Exchange client should not be called
     EXPECT_CALL(*exchange_client, cancelOrder(_)).Times(0);
     
     // Execute the test
     CancelResponse response = service->cancelOrder("test_order_1");
     
     // Verify response
     EXPECT_FALSE(response.success);
     EXPECT_EQ(response.client_order_id, "test_order_1");
     EXPECT_EQ(response.exchange_order_id, "ex_order_123");
     EXPECT_FALSE(response.error_message.empty());
 }
 
 // Test get order status
 TEST_F(ExecutionServiceTest, GetOrderStatus) {
     // Initialize service
     service->initialize();
     
     // Create test order
     Order test_order = createTestOrder();
     test_order.setExchangeOrderId("ex_order_123");
     test_order.open(); // Set state to OPEN
     
     // Configure mock behavior
     std::optional<Order> opt_order = test_order;
     
     EXPECT_CALL(*order_lifecycle_tracker, getOrder("test_order_1"))
         .WillOnce(Return(opt_order))
         .WillOnce(Return(opt_order));
     
     // For active orders, expect a status refresh
     EXPECT_CALL(*exchange_client, getOrderStatus(_)).Times(1);
     
     // Execute the test
     OrderStatusResponse response = service->getOrderStatus("test_order_1");
     
     // Verify response
     EXPECT_TRUE(response.found);
     EXPECT_EQ(response.order.getClientOrderId(), "test_order_1");
     EXPECT_EQ(response.order.getExchangeOrderId(), "ex_order_123");
     EXPECT_TRUE(response.error_message.empty());
 }
 
 // Test get order status - order not found
 TEST_F(ExecutionServiceTest, GetOrderStatusNotFound) {
     // Initialize service
     service->initialize();
     
     // Configure mock behavior
     std::optional<Order> empty_opt;
     
     EXPECT_CALL(*order_lifecycle_tracker, getOrder("non_existent_order"))
         .WillOnce(Return(empty_opt));
     
     // Execute the test
     OrderStatusResponse response = service->getOrderStatus("non_existent_order");
     
     // Verify response
     EXPECT_FALSE(response.found);
     EXPECT_EQ(response.error_message, "Order not found");
 }
 
 // Test get active orders
 TEST_F(ExecutionServiceTest, GetActiveOrders) {
     // Initialize service
     service->initialize();
     
     // Create test orders
     std::vector<Order> active_orders;
     active_orders.push_back(createTestOrder("order_1"));
     active_orders.push_back(createTestOrder("order_2"));
     
     // Configure mock behavior
     EXPECT_CALL(*order_lifecycle_tracker, getActiveOrders())
         .WillOnce(Return(active_orders));
     
     // Execute the test
     auto result = service->getActiveOrders();
     
     // Verify response
     EXPECT_EQ(result.size(), 2);
     EXPECT_EQ(result[0].getClientOrderId(), "order_1");
     EXPECT_EQ(result[1].getClientOrderId(), "order_2");
 }
 
 // Add more tests for the remaining functionality...
 
 // Main function to run the tests
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }