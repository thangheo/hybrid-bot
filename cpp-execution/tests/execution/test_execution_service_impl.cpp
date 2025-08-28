/**
 * @file test_execution_service_impl.cpp
 * @brief Unit tests for the gRPC ExecutionServiceImpl class
 * 
 * Created on: May 6, 2025
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include "execution/grpc/execution_service_impl.hpp"
 #include "mocks/mock_execution_service.hpp"
 
 using namespace trading::execution;
 using namespace trading::execution::grpc;
 using ::testing::_;
 using ::testing::Return;
 using ::testing::ReturnRef;
 using ::testing::NiceMock;
 using ::testing::DoAll;
 using ::testing::SetArgReferee;
 
 class ExecutionServiceImplTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create mock execution service
         mock_execution_service = std::make_shared<NiceMock<MockExecutionService>>();
         
         // Create gRPC service implementation under test
         grpc_service = std::make_unique<ExecutionServiceImpl>(mock_execution_service);
     }
 
     // Mock execution service
     std::shared_ptr<MockExecutionService> mock_execution_service;
     
     // gRPC service under test
     std::unique_ptr<ExecutionServiceImpl> grpc_service;
     
     // Helper to create a test proto trading signal
     ::trading::proto::TradingSignal createTestProtoSignal() {
         ::trading::proto::TradingSignal signal;
         signal.set_signal_id("test_signal_1");
         signal.set_trading_pair("BTC/USD");
         signal.set_direction("buy");
         signal.set_strength(0.8);
         signal.set_target_size(1.0);
         signal.set_signal_ttl_ms(60000);
         signal.set_strategy_id("order_imbalance");
         signal.set_strategy_version("1.0");
         (*signal.mutable_signal_metrics())["bid_ask_ratio"] = 1.2;
         return signal;
     }
     
     // Helper to create a test proto order request
     ::trading::proto::OrderRequest createTestProtoOrderRequest() {
         ::trading::proto::OrderRequest request;
         request.set_client_order_id("test_order_1");
         request.set_exchange_id("binance");
         request.set_trading_pair("BTC/USD");
         request.set_side("buy");
         request.set_order_type("limit");
         request.set_quantity(1.0);
         request.set_price(50000.0);
         request.set_time_in_force("GTC");
         return request;
     }
 };
 
 // Test ExecuteSignal method
 TEST_F(ExecutionServiceImplTest, ExecuteSignal) {
     // Create test signal
     auto proto_signal = createTestProtoSignal();
     
     // Configure mock behavior
     ExecutionResponse mock_response;
     mock_response.success = true;
     mock_response.client_order_id = "test_order_1";
     mock_response.exchange_order_id = "ex_order_123";
     mock_response.order_status = "OPEN";
     mock_response.timestamp = std::chrono::system_clock::now();
     
     EXPECT_CALL(*mock_execution_service, executeSignal(_))
         .WillOnce(Return(mock_response));
     
     // Execute the test
     ::grpc::ServerContext context;
     ::trading::proto::ExecutionResponse response;
     auto status = grpc_service->ExecuteSignal(&context, &proto_signal, &response);
     
     // Verify response
     EXPECT_TRUE(status.ok());
     EXPECT_TRUE(response.success());
     EXPECT_EQ(response.client_order_id(), "test_order_1");
     EXPECT_EQ(response.exchange_order_id(), "ex_order_123");
     EXPECT_EQ(response.status(), "OPEN");
 }
 
 // Test PlaceOrder method
 TEST_F(ExecutionServiceImplTest, PlaceOrder) {
     // Create test order request
     auto proto_order = createTestProtoOrderRequest();
     
     // Configure mock behavior
     ExecutionResponse mock_response;
     mock_response.success = true;
     mock_response.client_order_id = "test_order_1";
     mock_response.exchange_order_id = "ex_order_123";
     mock_response.order_status = "OPEN";
     mock_response.timestamp = std::chrono::system_clock::now();
     
     EXPECT_CALL(*mock_execution_service, placeOrder(_))
         .WillOnce(Return(mock_response));
     
     // Execute the test
     ::grpc::ServerContext context;
     ::trading::proto::ExecutionResponse response;
     auto status = grpc_service->PlaceOrder(&context, &proto_order, &response);
     
     // Verify response
     EXPECT_TRUE(status.ok());
     EXPECT_TRUE(response.success());
     EXPECT_EQ(response.client_order_id(), "test_order_1");
     EXPECT_EQ(response.exchange_order_id(), "ex_order_123");
     EXPECT_EQ(response.status(), "OPEN");
 }
 
 // Test CancelOrder method by client order ID
 TEST_F(ExecutionServiceImplTest, CancelOrderByClientId) {
     // Create cancel request
     ::trading::proto::CancelRequest request;
     request.set_client_order_id("test_order_1");
     
     // Configure mock behavior
     CancelResponse mock_response;
     mock_response.success = true;
     mock_response.client_order_id = "test_order_1";
     mock_response.exchange_order_id = "ex_order_123";
     mock_response.timestamp = std::chrono::system_clock::now();
     
     EXPECT_CALL(*mock_execution_service, cancelOrder("test_order_1"))
         .WillOnce(Return(mock_response));
     
     // Execute the test
     ::grpc::ServerContext context;
     ::trading::proto::CancelResponse response;
     auto status = grpc_service->CancelOrder(&context, &request, &response);
     
     // Verify response
     EXPECT_TRUE(status.ok());
     EXPECT_TRUE(response.success());
     EXPECT_EQ(response.client_order_id(), "test_order_1");
     EXPECT_EQ(response.exchange_order_id(), "ex_order_123");
 }
 
 // Test CancelOrder method by exchange order ID
 TEST_F(ExecutionServiceImplTest, CancelOrderByExchangeId) {
     // Create cancel request
     ::trading::proto::CancelRequest request;
     request.set_exchange_order_id("ex_order_123");
     request.set_exchange_id("binance");
     
     // Configure mock behavior
     CancelResponse mock_response;
     mock_response.success = true;
     mock_response.client_order_id = "test_order_1";
     mock_response.exchange_order_id = "ex_order_123";
     mock_response.timestamp = std::chrono::system_clock::now();
     
     EXPECT_CALL(*mock_execution_service, cancelOrderByExchangeId("ex_order_123", "binance"))
         .WillOnce(Return(mock_response));
     
     // Execute the test
     ::grpc::ServerContext context;
     ::trading::proto::CancelResponse response;
     auto status = grpc_service->CancelOrder(&context, &request, &response);
     
     // Verify response
     EXPECT_TRUE(status.ok());
     EXPECT_TRUE(response.success());
     EXPECT_EQ(response.client_order_id(), "test_order_1");
     EXPECT_EQ(response.exchange_order_id(), "ex_order_123");
 }
 
 // Test GetOrderStatus method
 TEST_F(ExecutionServiceImplTest, GetOrderStatus) {
     // Create status request
     ::trading::proto::StatusRequest request;
     request.set_client_order_id("test_order_1");
     
     // Create a test order for the response
     Order test_order("test_order_1");
     test_order.setExchangeId("binance");
     test_order.setExchangeOrderId("ex_order_123");
     test_order.setTradingPair("BTC/USD");
     test_order.setSide(OrderSide::BUY);
     test_order.setType(OrderType::LIMIT);
     test_order.setQuantity(1.0);
     test_order.setPrice(50000.0);
     test_order.open(); // Set state to OPEN
     
     // Configure mock behavior
     OrderStatusResponse mock_response;
     mock_response.found = true;
     mock_response.order = test_order;
     
     EXPECT_CALL(*mock_execution_service, getOrderStatus("test_order_1"))
         .WillOnce(Return(mock_response));
     
     // Execute the test
     ::grpc::ServerContext context;
     ::trading::proto::OrderStatusResponse response;
     auto status = grpc_service->GetOrderStatus(&context, &request, &response);
     
     // Verify response
     EXPECT_TRUE(status.ok());
     EXPECT_TRUE(response.found());
     EXPECT_TRUE(response.error_message().empty());
     EXPECT_EQ(response.order().client_order_id(), "test_order_1");
     EXPECT_EQ(response.order().exchange_order_id(), "ex_order_123");
     EXPECT_EQ(response.order().status(), "open");
 }
 
 // Test GetOrderStatus method - order not found
 TEST_F(ExecutionServiceImplTest, GetOrderStatusNotFound) {
     // Create status request
     ::trading::proto::StatusRequest request;
     request.set_client_order_id("non_existent_order");
     
     // Configure mock behavior
     OrderStatusResponse mock_response;
     mock_response.found = false;
     mock_response.error_message = "Order not found";
     
     EXPECT_CALL(*mock_execution_service, getOrderStatus("non_existent_order"))
         .WillOnce(Return(mock_response));
     
     // Execute the test
     ::grpc::ServerContext context;
     ::trading::proto::OrderStatusResponse response;
     auto status = grpc_service->GetOrderStatus(&context, &request, &response);
     
     // Verify response
     EXPECT_TRUE(status.ok());
     EXPECT_FALSE(response.found());
     EXPECT_EQ(response.error_message(), "Order not found");
 }
 
 // Test GetHealthStatus method
 TEST_F(ExecutionServiceImplTest, GetHealthStatus) {
     // Create health request
     ::trading::proto::HealthRequest request;
     
     // Configure mock behavior
     std::string health_json = R"({
         "service": "execution_service",
         "initialized": true,
         "running": true,
         "components": {
             "api_client_manager": "healthy",
             "order_lifecycle_tracker": "healthy",
             "fill_reconciliation": "healthy"
         }
     })";
     
     EXPECT_CALL(*mock_execution_service, getHealthStatus())
         .WillOnce(Return(health_json));
     
     // Execute the test
     ::grpc::ServerContext context;
     ::trading::proto::HealthResponse response;
     auto status = grpc_service->GetHealthStatus(&context, &request, &response);
     
     // Verify response
     EXPECT_TRUE(status.ok());
     EXPECT_TRUE(response.is_healthy());
     EXPECT_EQ(response.status(), health_json);
 }
 
 // Add more tests for remaining methods...
 
 // Main function to run the tests
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }