// Example using Google Mock
#include <gmock/gmock.h>

// Mock OrderBookAnalyzer for testing
class MockOrderBookAnalyzer : public trading::strategy::OrderBookAnalyzer {
public:
    MOCK_METHOD(bool, processOrderBook, (const data_ingestion::OrderBook& orderBook), (override));
    MOCK_METHOD(OrderBookMetrics, getMetrics, (), (const, override));
    MOCK_METHOD(void, reset, (), (override));
    // ... other mocked methods
};

// Mock ImbalanceCallback for testing
class MockImbalanceCallback {
public:
    MOCK_METHOD(void, onImbalance, (const ImbalanceEvent& event));
};

// Test with mocks
TEST(ImbalanceDetectionMockTest, TestWithMocks) {
    // Setup mocks
    MockOrderBookAnalyzer mockAnalyzer;
    MockImbalanceCallback mockCallback;
    
    // Create test subject
    auto engine = std::make_shared<ImbalanceDetectionEngine>();
    
    // Configure mocks
    OrderBookMetrics testMetrics = /* create test metrics */;
    EXPECT_CALL(mockAnalyzer, getMetrics())
        .WillRepeatedly(::testing::Return(testMetrics));
    
    EXPECT_CALL(mockCallback, onImbalance(::testing::_))
        .Times(::testing::AtLeast(1));
    
    // Register mock callback
    engine->registerImbalanceCallback(
        std::bind(&MockImbalanceCallback::onImbalance, &mockCallback, std::placeholders::_1));
    
    // Perform test
    engine->analyzeMetrics(testMetrics);
    
    // Verification happens through the EXPECT_CALL macros
}