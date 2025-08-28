// Integration test with OrderBook
TEST(ImbalanceDetectionIntegrationTest, OrderBookIntegration) {
    // Create order book
    trading::data_ingestion::OrderBook orderBook("binance", "BTC/USDT");
    
    // Create imbalance detection engine
    auto engine = std::make_shared<trading::strategy::ImbalanceDetectionEngine>();
    ImbalanceDetectionConfig config;
    engine->initialize(config);
    
    // Create order book analyzer (would need to be implemented)
    auto analyzer = std::make_shared<trading::strategy::OrderBookAnalyzer>();
    
    // Register callback to connect the analyzer and detection engine
    analyzer->setMetricsCallback([&](const OrderBookMetrics& metrics) {
        engine->analyzeMetrics(metrics);
    });
    
    // Create a callback to track imbalance events
    std::vector<ImbalanceEvent> detectedEvents;
    engine->registerImbalanceCallback([&](const ImbalanceEvent& event) {
        detectedEvents.push_back(event);
    });
    
    // Apply order book updates that should create an imbalance
    std::vector<trading::data_ingestion::PriceLevel> bids = {
        {10000.0, 20.0},
        {9990.0, 30.0},
        {9980.0, 25.0},
        {9970.0, 40.0},
    };
    
    std::vector<trading::data_ingestion::PriceLevel> asks = {
        {10010.0, 5.0},
        {10020.0, 7.0},
        {10030.0, 10.0},
        {10040.0, 8.0},
    };
    
    // Apply the order book snapshot (this would trigger the analyzer)
    orderBook.applySnapshot(bids, asks, 1, std::chrono::system_clock::now());
    
    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Verify imbalance was detected
    EXPECT_GT(detectedEvents.size(), 0);
    EXPECT_TRUE(engine->hasSignificantImbalance(ImbalanceType::VOLUME_IMBALANCE));
}