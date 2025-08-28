/**
 * @file test_imbalance_detection_engine.cpp
 * @brief Tests for the ImbalanceDetectionEngine component
 * 
 * @details
 * Contains unit tests and component tests for the imbalance detection engine
 * 
 * Created on: May 6, 2025
 */

 #include "strategy/pattern/imbalance_detection_engine.hpp"
 #include "data_ingestion/orderbook/order_book.hpp"
 #include <gtest/gtest.h>
 #include <memory>
 #include <thread>
 #include <chrono>
 #include <atomic>
 
 namespace trading {
 namespace strategy {
 namespace test {
 
 // Create a test fixture for ImbalanceDetectionEngine tests
 class ImbalanceDetectionEngineTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create and initialize the detection engine
         engine = std::make_shared<ImbalanceDetectionEngine>();
         
         // Setup default configuration
         ImbalanceDetectionConfig config;
         config.maxHistorySize = 10;
         config.detectionWindowMs = 1000;
         
         // Initialize the engine
         engine->initialize(config);
         
         // Set custom thresholds for testing
         ThresholdSet thresholds;
         thresholds[ImbalanceType::VOLUME_IMBALANCE] = 1.5;        // Lower threshold for easier testing
         thresholds[ImbalanceType::PRICE_LEVEL_IMBALANCE] = 1.5;
         thresholds[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = 0.6; // Using normalized value (0-1 range)
         thresholds[ImbalanceType::ABNORMAL_SPREAD] = 1.5;
         
         engine->setThresholds(thresholds);
     }
     
     void TearDown() override {
         engine.reset();
     }
     
     // Helper method to create sample order book metrics
     OrderBookMetrics createSampleMetrics(const core::ExchangeId& exchangeId, const std::string& symbol) {
         OrderBookMetrics metrics;
         metrics.exchangeId = exchangeId;
         metrics.symbol = symbol;
         metrics.timestamp = std::chrono::system_clock::now();
         
         // Set default metrics
         metrics.metrics["bid_volume"] = 100.0;
         metrics.metrics["ask_volume"] = 100.0;
         metrics.metrics["bid_level_count"] = 10.0;
         metrics.metrics["ask_level_count"] = 10.0;
         metrics.metrics["bid_flow"] = 0.0;
         metrics.metrics["ask_flow"] = 0.0;
         metrics.metrics["spread_percent"] = 0.1;
         metrics.metrics["average_spread_percent"] = 0.1;
         metrics.metrics["volatility"] = 0.005;
         
         return metrics;
     }
     
     // Helper to create metrics with volume imbalance
     OrderBookMetrics createVolumeImbalanceMetrics(const core::ExchangeId& exchangeId, const std::string& symbol, double bidVol, double askVol) {
         OrderBookMetrics metrics = createSampleMetrics(exchangeId, symbol);
         metrics.metrics["bid_volume"] = bidVol;
         metrics.metrics["ask_volume"] = askVol;
         return metrics;
     }
     
     // Helper to create metrics with price level imbalance
     OrderBookMetrics createPriceLevelImbalanceMetrics(const core::ExchangeId& exchangeId, const std::string& symbol, double bidLevels, double askLevels) {
         OrderBookMetrics metrics = createSampleMetrics(exchangeId, symbol);
         metrics.metrics["bid_level_count"] = bidLevels;
         metrics.metrics["ask_level_count"] = askLevels;
         return metrics;
     }
     
     // Helper to create metrics with liquidity flow imbalance
     OrderBookMetrics createLiquidityFlowImbalanceMetrics(const core::ExchangeId& exchangeId, const std::string& symbol, double bidFlow, double askFlow) {
         OrderBookMetrics metrics = createSampleMetrics(exchangeId, symbol);
         metrics.metrics["bid_flow"] = bidFlow;
         metrics.metrics["ask_flow"] = askFlow;
         return metrics;
     }
     
     // Helper to create metrics with spread anomaly
     OrderBookMetrics createSpreadAnomalyMetrics(const core::ExchangeId& exchangeId, const std::string& symbol, double spread, double avgSpread) {
         OrderBookMetrics metrics = createSampleMetrics(exchangeId, symbol);
         metrics.metrics["spread_percent"] = spread;
         metrics.metrics["average_spread_percent"] = avgSpread;
         return metrics;
     }
     
     std::shared_ptr<ImbalanceDetectionEngine> engine;
 };
 
 // Test basic initialization
 TEST_F(ImbalanceDetectionEngineTest, Initialization) {
     ASSERT_TRUE(engine != nullptr);
     
     // Verify initial state
     EXPECT_EQ(engine->getDetectedImbalances().size(), 0);
     EXPECT_FALSE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     EXPECT_FALSE(engine->hasSignificantImbalance(ImbalanceType::PRICE_LEVEL_IMBALANCE));
     EXPECT_FALSE(engine->hasSignificantImbalance(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE));
     EXPECT_FALSE(engine->hasSignificantImbalance(ImbalanceType::ABNORMAL_SPREAD));
     
     // Verify thresholds were set
     ThresholdSet thresholds = engine->getThresholds();
     EXPECT_EQ(thresholds.size(), 4);
     EXPECT_EQ(thresholds[ImbalanceType::VOLUME_IMBALANCE], 1.5);
 }
 
 // Test volume imbalance detection (buy side)
 TEST_F(ImbalanceDetectionEngineTest, VolumeImbalanceBuySide) {
     // Create metrics with significant buy-side volume imbalance
     OrderBookMetrics metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", 200.0, 100.0);
     
     // Process the metrics
     EXPECT_TRUE(engine->analyzeMetrics(metrics));
     
     // Verify imbalance was detected
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     
     // Check the imbalance event details
     auto imbalances = engine->getDetectedImbalances();
     ASSERT_EQ(imbalances.size(), 1);
     
     // Verify event properties
     const ImbalanceEvent& event = imbalances[0];
     EXPECT_EQ(event.type, ImbalanceType::VOLUME_IMBALANCE);
     EXPECT_EQ(event.direction, Direction::BUY);
     EXPECT_NEAR(event.strength, 1.0, 0.01);  // bid/ask = 2.0, strength = |2.0 - 1.0| = 1.0
     EXPECT_EQ(event.exchangeId, "binance");
     EXPECT_EQ(event.symbol, "BTC/USDT");
 }
 
 // Test volume imbalance detection (sell side)
 TEST_F(ImbalanceDetectionEngineTest, VolumeImbalanceSellSide) {
     // Create metrics with significant sell-side volume imbalance
     OrderBookMetrics metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", 50.0, 100.0);
     
     // Process the metrics
     EXPECT_TRUE(engine->analyzeMetrics(metrics));
     
     // Verify imbalance was detected
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     
     // Check the imbalance event details
     auto imbalances = engine->getDetectedImbalances();
     ASSERT_EQ(imbalances.size(), 1);
     
     // Verify event properties
     const ImbalanceEvent& event = imbalances[0];
     EXPECT_EQ(event.type, ImbalanceType::VOLUME_IMBALANCE);
     EXPECT_EQ(event.direction, Direction::SELL);
     EXPECT_NEAR(event.strength, 0.5, 0.01);  // bid/ask = 0.5, strength = |0.5 - 1.0| = 0.5
 }
 
 // Test no imbalance detection when balanced
 TEST_F(ImbalanceDetectionEngineTest, NoImbalanceWhenBalanced) {
     // Create balanced metrics
     OrderBookMetrics metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", 100.0, 100.0);
     
     // Process the metrics
     EXPECT_FALSE(engine->analyzeMetrics(metrics));
     
     // Verify no imbalance was detected
     EXPECT_FALSE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     EXPECT_EQ(engine->getDetectedImbalances().size(), 0);
 }
 
 // Test price level imbalance detection
 TEST_F(ImbalanceDetectionEngineTest, PriceLevelImbalance) {
     // Create metrics with price level imbalance
     OrderBookMetrics metrics = createPriceLevelImbalanceMetrics("binance", "BTC/USDT", 20.0, 10.0);
     
     // Process the metrics
     EXPECT_TRUE(engine->analyzeMetrics(metrics));
     
     // Verify imbalance was detected
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::PRICE_LEVEL_IMBALANCE));
     
     // Check the imbalance event details
     auto imbalances = engine->getDetectedImbalances();
     ASSERT_EQ(imbalances.size(), 1);
     
     // Verify event properties
     const ImbalanceEvent& event = imbalances[0];
     EXPECT_EQ(event.type, ImbalanceType::PRICE_LEVEL_IMBALANCE);
     EXPECT_EQ(event.direction, Direction::BUY);
     EXPECT_NEAR(event.strength, 1.0, 0.01);  // bid/ask = 2.0, strength = |2.0 - 1.0| = 1.0
 }
 
 // Test liquidity flow imbalance detection
 TEST_F(ImbalanceDetectionEngineTest, LiquidityFlowImbalance) {
     // Create metrics with liquidity flow imbalance
     OrderBookMetrics metrics = createLiquidityFlowImbalanceMetrics("binance", "BTC/USDT", 100.0, 20.0);
     
     // Process the metrics
     EXPECT_TRUE(engine->analyzeMetrics(metrics));
     
     // Verify imbalance was detected
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE));
     
     // Check the imbalance event details
     auto imbalances = engine->getDetectedImbalances();
     ASSERT_EQ(imbalances.size(), 1);
     
     // Verify event properties
     const ImbalanceEvent& event = imbalances[0];
     EXPECT_EQ(event.type, ImbalanceType::LIQUIDITY_FLOW_IMBALANCE);
     EXPECT_EQ(event.direction, Direction::BUY);
     // Net flow = 100 - 20 = 80, Total flow = |100| + |20| = 120, Imbalance = |80|/120 = 0.667
     EXPECT_NEAR(event.strength, 0.667, 0.01);
 }
 
 // Test abnormal spread detection
 TEST_F(ImbalanceDetectionEngineTest, AbnormalSpread) {
     // Create metrics with abnormal spread
     OrderBookMetrics metrics = createSpreadAnomalyMetrics("binance", "BTC/USDT", 0.2, 0.1);
     
     // Process the metrics
     EXPECT_TRUE(engine->analyzeMetrics(metrics));
     
     // Verify imbalance was detected
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::ABNORMAL_SPREAD));
     
     // Check the imbalance event details
     auto imbalances = engine->getDetectedImbalances();
     ASSERT_EQ(imbalances.size(), 1);
     
     // Verify event properties
     const ImbalanceEvent& event = imbalances[0];
     EXPECT_EQ(event.type, ImbalanceType::ABNORMAL_SPREAD);
     EXPECT_NEAR(event.strength, 1.0, 0.01);  // spread ratio = 2.0, strength = 2.0 - 1.0 = 1.0
 }
 
 // Test multiple imbalance detection
 TEST_F(ImbalanceDetectionEngineTest, MultipleImbalances) {
     // Create metrics with multiple imbalances
     OrderBookMetrics metrics = createSampleMetrics("binance", "BTC/USDT");
     metrics.metrics["bid_volume"] = 200.0;
     metrics.metrics["ask_volume"] = 100.0;
     metrics.metrics["bid_level_count"] = 20.0;
     metrics.metrics["ask_level_count"] = 10.0;
     
     // Process the metrics
     EXPECT_TRUE(engine->analyzeMetrics(metrics));
     
     // Verify imbalances were detected
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::PRICE_LEVEL_IMBALANCE));
     
     // Check the imbalance events
     auto imbalances = engine->getDetectedImbalances();
     ASSERT_EQ(imbalances.size(), 2);
 }
 
 // Test clear imbalances
 TEST_F(ImbalanceDetectionEngineTest, ClearImbalances) {
     // Create metrics with imbalance
     OrderBookMetrics metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", 200.0, 100.0);
     
     // Process the metrics
     engine->analyzeMetrics(metrics);
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     
     // Clear imbalances
     engine->clearImbalances();
     
     // Verify no imbalances remain
     EXPECT_FALSE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     EXPECT_EQ(engine->getDetectedImbalances().size(), 0);
 }
 
 // Test callback notification
 TEST_F(ImbalanceDetectionEngineTest, CallbackNotification) {
     // Setup test variables
     std::atomic<bool> callbackCalled(false);
     ImbalanceEvent receivedEvent;
     
     // Register callback
     size_t callbackId = engine->registerImbalanceCallback([&](const ImbalanceEvent& event) {
         callbackCalled = true;
         receivedEvent = event;
     });
     
     // Create metrics with imbalance
     OrderBookMetrics metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", 200.0, 100.0);
     
     // Process the metrics
     engine->analyzeMetrics(metrics);
     
     // Wait a bit for callback to be processed
     std::this_thread::sleep_for(std::chrono::milliseconds(10));
     
     // Verify callback was called
     EXPECT_TRUE(callbackCalled);
     EXPECT_EQ(receivedEvent.type, ImbalanceType::VOLUME_IMBALANCE);
     EXPECT_EQ(receivedEvent.direction, Direction::BUY);
     
     // Unregister callback
     EXPECT_TRUE(engine->unregisterImbalanceCallback(callbackId));
     
     // Reset test variables
     callbackCalled = false;
     
     // Create new imbalance
     metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", 50.0, 100.0);
     engine->clearImbalances();
     engine->analyzeMetrics(metrics);
     
     // Wait a bit
     std::this_thread::sleep_for(std::chrono::milliseconds(10));
     
     // Verify callback was not called after unregistering
     EXPECT_FALSE(callbackCalled);
 }
 
 // Test threshold changes
 TEST_F(ImbalanceDetectionEngineTest, ThresholdChanges) {
     // Create metrics with moderate imbalance
     OrderBookMetrics metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", 145.0, 100.0);
     
     // Process with default thresholds (1.5)
     EXPECT_FALSE(engine->analyzeMetrics(metrics));
     EXPECT_FALSE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     
     // Set more sensitive thresholds
     ThresholdSet thresholds = engine->getThresholds();
     thresholds[ImbalanceType::VOLUME_IMBALANCE] = 1.4;  // Lower threshold
     engine->setThresholds(thresholds);
     
     // Clear previous state
     engine->clearImbalances();
     
     // Process with new thresholds
     EXPECT_TRUE(engine->analyzeMetrics(metrics));
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
 }
 
 // Test with missing metrics
 TEST_F(ImbalanceDetectionEngineTest, MissingMetrics) {
     // Create metrics with missing data
     OrderBookMetrics metrics;
     metrics.exchangeId = "binance";
     metrics.symbol = "BTC/USDT";
     metrics.timestamp = std::chrono::system_clock::now();
     
     // Add only some metrics
     metrics.metrics["bid_volume"] = 200.0;
     // Deliberately missing ask_volume
     
     // Process the metrics
     EXPECT_FALSE(engine->analyzeMetrics(metrics));
     
     // Verify no imbalance was detected due to missing data
     EXPECT_FALSE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     EXPECT_EQ(engine->getDetectedImbalances().size(), 0);
 }
 
 // Integration test with multiple updates
 TEST_F(ImbalanceDetectionEngineTest, MultipleUpdates) {
     // Send a series of updates with evolving imbalance
     for (int i = 0; i < 5; i++) {
         // Gradually increasing buy-side imbalance
         double bidVolume = 100.0 + (i * 20.0);
         OrderBookMetrics metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", bidVolume, 100.0);
         
         engine->analyzeMetrics(metrics);
         
         // Wait a bit
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
     }
     
     // Verify final state
     EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
     
     auto imbalances = engine->getDetectedImbalances();
     ASSERT_GT(imbalances.size(), 0);
     
     // The last imbalance should be the strongest
     const ImbalanceEvent& lastEvent = imbalances.back();
     EXPECT_EQ(lastEvent.direction, Direction::BUY);
     EXPECT_NEAR(lastEvent.strength, 0.8, 0.1);  // Approximated based on final bid volume of 180
 }
    // Performance test for the imbalance detection engine
    TEST_F(ImbalanceDetectionEngineTest, PerformanceTesting) {
        const int NUM_ITERATIONS = 10000;
        
        // Time how long it takes to process many metrics
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < NUM_ITERATIONS; i++) {
            // Create varying metrics
            double bidVol = 100.0 + (i % 100);
            double askVol = 100.0 + ((i + 50) % 100);
            OrderBookMetrics metrics = createVolumeImbalanceMetrics("binance", "BTC/USDT", bidVol, askVol);
            
            engine->analyzeMetrics(metrics);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Output performance metrics
        double avgProcessingTime = static_cast<double>(duration.count()) / NUM_ITERATIONS;
        std::cout << "Processed " << NUM_ITERATIONS << " updates in " << duration.count() 
                << " microseconds. Average: " << avgProcessingTime << " µs per update." << std::endl;
        
        // Expected performance should be under 10µs per update for a high-performance system
        EXPECT_LT(avgProcessingTime, 10.0);
    }
    // Stress test with concurrent updates
    TEST_F(ImbalanceDetectionEngineTest, ConcurrentUpdates) {
        const int NUM_THREADS = 4;
        const int UPDATES_PER_THREAD = 1000;
        
        std::vector<std::thread> threads;
        std::atomic<int> callbackCount(0);
        
        // Register callback to track events
        engine->registerImbalanceCallback([&](const ImbalanceEvent&) {
            callbackCount++;
        });
        
        // Function for each thread to run
        auto threadFunc = [&](int threadId) {
            for (int i = 0; i < UPDATES_PER_THREAD; i++) {
                // Create metrics with some randomness based on thread and iteration
                double bidVol = 100.0 + ((threadId * i) % 150);
                double askVol = 100.0;
                
                OrderBookMetrics metrics = createVolumeImbalanceMetrics(
                    "binance", "BTC/USDT", bidVol, askVol);
                
                engine->analyzeMetrics(metrics);
            }
        };
        
        // Start threads
        for (int i = 0; i < NUM_THREADS; i++) {
            threads.emplace_back(threadFunc, i);
        }
        
        // Wait for all threads to finish
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Verify the number of callbacks is as expected (should be less than the number of updates)
        std::cout << "Received " << callbackCount << " imbalance events from " 
                << (NUM_THREADS * UPDATES_PER_THREAD) << " updates." << std::endl;
        
        // We should have some imbalance events, but not for every update
        EXPECT_GT(callbackCount, 0);
        EXPECT_LT(callbackCount, NUM_THREADS * UPDATES_PER_THREAD);
    }
 } // namespace test
 } // namespace strategy
 } // namespace trading
 
 // Main function for running the tests
 int main(int argc, char** argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }