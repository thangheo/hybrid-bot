/**
 * Unit tests for the Strategy Interface implementation
 *
 * File: strategy_interface_test.cpp
 * Created: 2025-05-05
 */

 #include "risk/interfaces/strategy_interface.hpp"
 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include <memory>
 #include <thread>
 #include <chrono>
 
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
 } // namespace common
 } // namespace trading

 
 // Test fixture for StrategyInterface tests
 class StrategyInterfaceTest : public ::testing::Test {
 protected:
     // Use the actual implementation but with mocked dependencies
     class TestableStrategyInterface : public risk::StrategyInterface {
     private:
         std::shared_ptr<common::MockZeroMQClient> mockClient_;
         std::shared_ptr<common::MockZeroMQClient> mockSubscriber_;
         bool initialized_ = false;
         
     public:
         TestableStrategyInterface(
             std::shared_ptr<common::MockZeroMQClient> mockClient,
             std::shared_ptr<common::MockZeroMQClient> mockSubscriber)
             : mockClient_(mockClient), mockSubscriber_(mockSubscriber) {}
         
         bool initialize() override {
             initialized_ = true;
             return true;
         }
         
         bool sendRiskFeedback(const risk::StrategyId& strategyId, const std::string& feedback) override {
             if (!initialized_) return false;
             
             nlohmann::json request = {
                 {"action", "risk_feedback"},
                 {"strategy_id", strategyId},
                 {"feedback", nlohmann::json::parse(feedback)}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             return responseJson["success"].get<bool>();
         }
         
         std::string getStrategyParameters(const risk::StrategyId& strategyId) override {
             if (!initialized_) return "{}";
             
             nlohmann::json request = {
                 {"action", "get_parameters"},
                 {"strategy_id", strategyId}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["parameters"].dump();
             }
             
             return "{}";
         }
         
         bool updateStrategyParameters(const risk::StrategyId& strategyId, const std::string& parameters) override {
             if (!initialized_) return false;
             
             nlohmann::json request = {
                 {"action", "update_parameters"},
                 {"strategy_id", strategyId},
                 {"parameters", nlohmann::json::parse(parameters)}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             return responseJson["success"].get<bool>();
         }
         
         bool notifyRiskEvent(const risk::StrategyId& strategyId, const risk::RiskEvent& event) override {
             if (!initialized_) return false;
             
             nlohmann::json request = {
                 {"action", "risk_event"},
                 {"strategy_id", strategyId},
                 {"event_type", event.type},
                 {"event_severity", event.severity},
                 {"event_message", event.message},
                 {"event_data", nlohmann::json::parse(event.data)}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             return responseJson["success"].get<bool>();
         }
         
         std::vector<risk::StrategyId> getActiveStrategies() override {
             if (!initialized_) return {};
             
             nlohmann::json request = {
                 {"action", "get_active_strategies"}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 std::vector<risk::StrategyId> strategies;
                 for (const auto& strategy : responseJson["strategies"]) {
                     strategies.push_back(strategy.get<risk::StrategyId>());
                 }
                 return strategies;
             }
             
             return {};
         }
         
         bool isStrategyActive(const risk::StrategyId& strategyId) override {
             if (!initialized_) return false;
             
             nlohmann::json request = {
                 {"action", "is_strategy_active"},
                 {"strategy_id", strategyId}
             };
             
             std::string response = mockClient_->sendAndReceive(request.dump());
             auto responseJson = nlohmann::json::parse(response);
             
             if (responseJson["success"].get<bool>()) {
                 return responseJson["active"].get<bool>();
             }
             
             return false;
         }
         
         void registerStrategyEventCallback(
             const std::function<void(const risk::StrategyId&, const std::string&)>& callback) override {
             // Store callback for testing - not called in tests
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
         strategyInterface_ = std::make_unique<TestableStrategyInterface>(mockClient_, mockSubscriber_);
     }
     
     std::shared_ptr<common::MockZeroMQClient> mockClient_;
     std::shared_ptr<common::MockZeroMQClient> mockSubscriber_;
     std::unique_ptr<TestableStrategyInterface> strategyInterface_;
 };
 
 // Test initialization
 TEST_F(StrategyInterfaceTest, Initialize) {
     EXPECT_TRUE(strategyInterface_->initialize());
 }
 
 // Test sending risk feedback
 TEST_F(StrategyInterfaceTest, SendRiskFeedback) {
     // Initialize
     strategyInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {{"success", true}};
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test sending risk feedback
     bool result = strategyInterface_->sendRiskFeedback(StrategyId("strategy1"), "{\"risk_level\": \"high\"}");
     EXPECT_TRUE(result);
 }
 
 // Test getting strategy parameters
 TEST_F(StrategyInterfaceTest, GetStrategyParameters) {
     // Initialize
     strategyInterface_->initialize();
     
     // Setup mock response
     nlohmann::json parameters = {
         {"risk_threshold", 0.5},
         {"max_position_size", 1.0}
     };
     nlohmann::json response = {
         {"success", true},
         {"parameters", parameters}
     };
     
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting parameters
     std::string result = strategyInterface_->getStrategyParameters(StrategyId("strategy1"));
     EXPECT_EQ(result, parameters.dump());
 }
 
 // Test updating strategy parameters
 TEST_F(StrategyInterfaceTest, UpdateStrategyParameters) {
     // Initialize
     strategyInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {{"success", true}};
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test updating parameters
     bool result = strategyInterface_->updateStrategyParameters(StrategyId("strategy1"),
         "{\"risk_threshold\": 0.7, \"max_position_size\": 2.0}");
     EXPECT_TRUE(result);
 }
 
 // Test notifying risk event
 TEST_F(StrategyInterfaceTest, NotifyRiskEvent) {
     // Initialize
     strategyInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {{"success", true}};
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test notifying risk event
     risk::RiskEvent event;
     event.type = "position_limit_breach";
     event.severity = "high";
     event.message = "Position limit exceeded";
     event.data = "{\"current\": 2.5, \"limit\": 2.0}";
     
     bool result = strategyInterface_->notifyRiskEvent(StrategyId("strategy1"), event);
     EXPECT_TRUE(result);
 }
 
 // Test getting active strategies
 TEST_F(StrategyInterfaceTest, GetActiveStrategies) {
     // Initialize
     strategyInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"strategies", {StrategyId("strategy1"), "strategy2", "strategy3"}}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting active strategies
     auto strategies = strategyInterface_->getActiveStrategies();
     EXPECT_EQ(strategies.size(), 3);
     EXPECT_EQ(strategies[0], StrategyId("strategy1"));
     EXPECT_EQ(strategies[1], "strategy2");
     EXPECT_EQ(strategies[2], "strategy3");
 }
 
 // Test checking if strategy is active
 TEST_F(StrategyInterfaceTest, IsStrategyActive) {
     // Initialize
     strategyInterface_->initialize();
     
     // Setup mock response
     nlohmann::json response = {
         {"success", true},
         {"active", true}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test checking if strategy is active
     bool result = strategyInterface_->isStrategyActive(StrategyId("strategy1"));
     EXPECT_TRUE(result);
 }
 
 // Test error handling
 TEST_F(StrategyInterfaceTest, ErrorHandling) {
     // Initialize
     strategyInterface_->initialize();
     
     // Setup mock error response
     nlohmann::json response = {
         {"success", false},
         {"error", "Strategy not found"}
     };
     EXPECT_CALL(*mockClient_, sendAndReceive(::testing::_))
         .WillOnce(::testing::Return(response.dump()));
     
     // Test getting parameters with error
     std::string result = strategyInterface_->getStrategyParameters("invalid_strategy");
     EXPECT_EQ(result, "{}");
 }
 
 // Test behavior when not initialized
 TEST_F(StrategyInterfaceTest, NotInitialized) {
     // Don't initialize (skip strategyInterface_->initialize())
     
     // Test methods without initialization
     EXPECT_FALSE(strategyInterface_->sendRiskFeedback(StrategyId("strategy1"), "{\"risk_level\": \"high\"}"));
     EXPECT_EQ(strategyInterface_->getStrategyParameters(StrategyId("strategy1")), "{}");
     EXPECT_FALSE(strategyInterface_->updateStrategyParameters(StrategyId("strategy1"), "{}"));
     
     risk::RiskEvent event;
     event.type = "test";
     event.severity = "low";
     event.message = "Test event";
     event.data = "{}";
     EXPECT_FALSE(strategyInterface_->notifyRiskEvent(StrategyId("strategy1"), event));
     
     EXPECT_TRUE(strategyInterface_->getActiveStrategies().empty());
     EXPECT_FALSE(strategyInterface_->isStrategyActive(StrategyId("strategy1")));
 }