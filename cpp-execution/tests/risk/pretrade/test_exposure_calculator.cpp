/**
 * @file test_exposure_calculator.cpp
 * @brief Comprehensive unit tests for the rewritten ExposureCalculator
 * 
 * Tests performance, thread safety, and correctness of the exposure calculations.
 * 
 * @author Trading System Team
 * @date May 27, 2025
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "risk/pretrade/exposure_calculator.hpp"
#include "core/market_types.hpp"  // For the existing TradingSignal
#include <format>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>

using namespace trading::risk;
using namespace trading::core;  // For TradingSignal
using namespace testing;

// Mock implementations
class MockPositionTracker : public PositionTracker {
public:
    MOCK_METHOD(std::vector<Position>, getAllPositions, (), (const, override));
    MOCK_METHOD(std::vector<Position>, getPositionsForAsset, (const std::string&), (const, override));
    MOCK_METHOD(std::vector<Position>, getPositionsForStrategy, (const std::string&), (const, override));
    MOCK_METHOD(std::vector<Position>, getPositionsForExchange, (const core::ExchangeId&), (const, override));
};

class MockDataInterface : public DataInterface {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(std::optional<core::MarketData>, getMarketData, 
                (const std::string&, const std::optional<std::string>&), (override));
    MOCK_METHOD(double, getCurrentPrice, 
                (const std::string&, const std::optional<std::string>&), (override));
    MOCK_METHOD(double, getVolatility, 
                (const std::string&, const std::string&, const std::optional<std::string>&), (override));
    MOCK_METHOD(std::vector<std::pair<Timestamp, double>>, getHistoricalPrices,
                (const std::string&, const std::string&, int, const std::optional<std::string>&), (override));
    MOCK_METHOD(std::vector<OrderRequest>, getOrderHistory,
                (const core::ExchangeId&, const std::string&, int), (override));
    MOCK_METHOD(std::map<std::string, double>, getExchangeBalances,
                (const core::ExchangeId&), (override));
    MOCK_METHOD(void, registerMarketDataCallback,
                (const std::function<void(const core::MarketData&)>&), (override));
    MOCK_METHOD(bool, subscribeMarketData,
                (const std::string&, const std::optional<std::string>&), (override));
    MOCK_METHOD(bool, unsubscribeMarketData,
                (const std::string&, const std::optional<std::string>&), (override));
    MOCK_METHOD(std::map<std::string, double>, getTradingFees,
                (const core::ExchangeId&, const std::optional<std::string>&), (override));
};

class ExposureCalculatorTest : public Test {
protected:
    void SetUp() override {
        mockPositionTracker = std::make_shared<MockPositionTracker>();
        mockDataInterface = std::make_shared<MockDataInterface>();
        
        ExposureCalculator::Config config;
        config.cacheExpiryTime = std::chrono::milliseconds(100);
        config.enableSIMDOptimizations = true;
        config.enableAsyncCalculations = true;
        
        exposureCalculator = std::make_unique<ExposureCalculator>(
            mockPositionTracker, mockDataInterface, config
        );
        
        setupDefaultMocks();
    }

    void setupDefaultMocks() {
        // Setup default price data
        ON_CALL(*mockDataInterface, getCurrentPrice(_, _))
            .WillByDefault(Return(50000.0)); // Default BTC price
            
        ON_CALL(*mockDataInterface, getVolatility(_, _, _))
            .WillByDefault(Return(0.15)); // Default 15% volatility
            
        // Setup default empty positions
        ON_CALL(*mockPositionTracker, getAllPositions())
            .WillByDefault(Return(std::vector<Position>{}));
            
        ON_CALL(*mockPositionTracker, getPositionsForAsset(_))
            .WillByDefault(Return(std::vector<Position>{}));
            
        ON_CALL(*mockPositionTracker, getPositionsForStrategy(_))
            .WillByDefault(Return(std::vector<Position>{}));
            
        ON_CALL(*mockPositionTracker, getPositionsForExchange(_))
            .WillByDefault(Return(std::vector<Position>{}));
    }

    std::shared_ptr<MockPositionTracker> mockPositionTracker;
    std::shared_ptr<MockDataInterface> mockDataInterface;
    std::unique_ptr<ExposureCalculator> exposureCalculator;
    RiskConfig riskConfig;
};

// Test initialization
TEST_F(ExposureCalculatorTest, InitializationSuccess) {
    auto result = exposureCalculator->initialize(riskConfig);
    ASSERT_TRUE(result.has_value());
}

// Test basic exposure validation
TEST_F(ExposureCalculatorTest, ValidateExposure_WithinLimits) {
    TradingSignal signal{
        core::SignalId{"signal_1"},
        core::Symbol::fromString("BTC/USDT"),
        core::ExchangeId::binance(),
        core::Direction::BUY,
        0.8,  // 0.8 BTC
        // ts
        core::Timestamp()
    };
    signal.setTargetSize(0.8);  // 0.8 BTC
    signal.setStrategyId(core::StrategyId{"strategy_1", "v1"});
    signal.setExchangeId(core::ExchangeId::binance());

    EXPECT_CALL(*mockDataInterface, getCurrentPrice("BTC/USDT", _))
        .WillOnce(Return(50000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility("BTC/USDT", "1d", _))
        .WillOnce(Return(0.15));
    
    auto result = exposureCalculator->validateExposure(signal);
    EXPECT_EQ(result, ExposureValidationResult::APPROVED);
}

// Test exposure limit exceeded
TEST_F(ExposureCalculatorTest, ValidateExposure_TotalLimitExceeded) {
    // Setup large existing exposure
    std::vector<Position> existingPositions;
    TradingPair btcPair{"BTC", "USDT", "BTC/USDT"};
    StrategyId strategy{"strategy_1", "v1"};
    
    existingPositions.emplace_back(
        core::ExchangeId::binance(), 
        btcPair, 
        strategy, 
        1.5,      // 1.5 BTC
        50000.0   // $50k entry price
    );
    
    EXPECT_CALL(*mockPositionTracker, getAllPositions())
        .WillRepeatedly(Return(existingPositions));
    
    // New signal that would exceed total limit
    TradingSignal signal{
        "signal_2",
        "BTC/USDT",
        core::ExchangeId::binance(),
        core::Direction::BUY,
        1.0,  // 1 more BTC
        "strategy_1"
    };
    
    EXPECT_CALL(*mockDataInterface, getCurrentPrice("BTC/USDT", _))
        .WillRepeatedly(Return(50000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility("BTC/USDT", "1d", _))
        .WillRepeatedly(Return(0.15));
    
    // Set a low total exposure limit
    ExposureLimitConfig limits;
    limits.maxTotalExposure = 50000.0; // $50k total limit
    exposureCalculator->updateExposureLimits(limits);
    
    auto result = exposureCalculator->validateExposure(signal);
    EXPECT_EQ(result, ExposureValidationResult::TOTAL_LIMIT_EXCEEDED);
}
//  TradingSignal(
//         const core::SignalId& sid,
//         const core::Symbol& sym,
//         const core::ExchangeId& exId,
//         core::Direction d,
//         double s = 0.0,
//         const core::Timestamp& gt = core::Timestamp()
//     ) 
// Test caching functionality
TEST_F(ExposureCalculatorTest, CachingWorks) {
    TradingSignal signal{
        core::SignalId{"signal_cache_test"},
        core::Symbol::fromString("ETH/USDT"),
        core::ExchangeId::coinbase(),
        core::Direction::SELL,
        10.0,  // 10 ETH
        // ts
        core::Timestamp()
    };
    
    EXPECT_CALL(*mockDataInterface, getCurrentPrice("ETH/USDT", _))
        .Times(1)  // Should only be called once due to caching
        .WillOnce(Return(3000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility("ETH/USDT", "1d", _))
        .Times(1)  // Should only be called once due to caching
        .WillOnce(Return(0.20));
    
    // First call
    auto result1 = exposureCalculator->validateExposure(signal);
    
    // Second call should use cached data
    auto result2 = exposureCalculator->validateExposure(signal);
    
    EXPECT_EQ(result1, ExposureValidationResult::APPROVED);
    EXPECT_EQ(result2, ExposureValidationResult::APPROVED);
    
    const auto& stats = exposureCalculator->getPerformanceStats();
    EXPECT_GT(stats.cacheHits.load(), 0);
}

// Test thread safety
TEST_F(ExposureCalculatorTest, ThreadSafety) {
    constexpr int numThreads = 10;
    constexpr int operationsPerThread = 100;
    
    std::atomic<int> successCount{0};
    std::atomic<int> errorCount{0};
    
    EXPECT_CALL(*mockDataInterface, getCurrentPrice(_, _))
        .WillRepeatedly(Return(50000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility(_, _, _))
        .WillRepeatedly(Return(0.15));
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 gen(t); // Thread-specific random generator
            std::uniform_real_distribution<> sizeDist(0.1, 2.0);
            
            for (int i = 0; i < operationsPerThread; ++i) {
                TradingSignal signal{
                    std::format("signal_{}_{}", t, i),
                    "BTC/USDT",
                    core::ExchangeId::binance(),
                    core::Direction::BUY,
                    sizeDist(gen),
                    std::format("strategy_{}", t)
                };
                
                auto result = exposureCalculator->validateExposure(signal);
                
                if (result == ExposureValidationResult::APPROVED || 
                    result == ExposureValidationResult::TOTAL_LIMIT_EXCEEDED) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                } else {
                    errorCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(successCount.load() + errorCount.load(), numThreads * operationsPerThread);
    EXPECT_LT(errorCount.load(), successCount.load() * 0.1); // Less than 10% errors
}

// Test performance benchmarks
TEST_F(ExposureCalculatorTest, PerformanceBenchmark) {
    constexpr int numValidations = 10000;
    
    EXPECT_CALL(*mockDataInterface, getCurrentPrice(_, _))
        .WillRepeatedly(Return(50000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility(_, _, _))
        .WillRepeatedly(Return(0.15));
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numValidations; ++i) {
        TradingSignal signal{
            core::SignalId{"signal_{}", i},
            core::Symbol::fromString("BTC/USDT"),
            core::ExchangeId::binance(),
            core::Direction::BUY,
            1.0,
            core::Timestamp()
        };
        
        auto result = exposureCalculator->validateExposure(signal);
        EXPECT_NE(result, ExposureValidationResult::CALCULATION_ERROR);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    double avgLatencyUs = static_cast<double>(duration.count()) / numValidations;
    
    // Performance requirement: average latency should be less than 10 microseconds
    EXPECT_LT(avgLatencyUs, 10.0) << "Average latency: " << avgLatencyUs << " microseconds";
    
    const auto& stats = exposureCalculator->getPerformanceStats();
    std::cout << "Performance Stats:\n"
              << "  Total validations: " << stats.totalValidations.load() << "\n"
              << "  Cache hits: " << stats.cacheHits.load() << "\n"
              << "  Cache misses: " << stats.cacheMisses.load() << "\n"
              << "  Avg calculation time: " << stats.avgCalculationTimeNs.load() << " ns\n"
              << "  Max calculation time: " << stats.maxCalculationTimeNs.load() << " ns\n";
}

// Test SIMD optimizations
TEST_F(ExposureCalculatorTest, SIMDOptimizationPerformance) {
    // Create a large number of positions to trigger SIMD path
    std::vector<Position> positions;
    TradingPair btcPair = core::Symbol::fromString("BTC/USDT");
    StrategyId strategy{"strategy_simd", "v1"};
    
    for (int i = 0; i < 100; ++i) {
        positions.emplace_back(
            core::ExchangeId::binance(),
            btcPair,
            strategy,
            0.1 * (i + 1),  // Varying position sizes
            50000.0
        );
    }
    
    EXPECT_CALL(*mockPositionTracker, getAllPositions())
        .WillRepeatedly(Return(positions));
    EXPECT_CALL(*mockDataInterface, getCurrentPrice(_, _))
        .WillRepeatedly(Return(50000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility(_, _, _))
        .WillRepeatedly(Return(0.15));
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Calculate total exposure multiple times
    for (int i = 0; i < 1000; ++i) {
        auto result = exposureCalculator->calculateTotalExposure();
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result.value(), 0.0);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    double avgLatencyUs = static_cast<double>(duration.count()) / 1000;
    
    // With SIMD optimizations, should be faster for large position sets
    EXPECT_LT(avgLatencyUs, 50.0) << "SIMD-optimized calculation too slow: " << avgLatencyUs << " μs";
}

// Test cache expiry
TEST_F(ExposureCalculatorTest, CacheExpiry) {
    TradingSignal signal{
        core::SignalId{"signal_cache_test"},
        core::Symbol::fromString("BTC/USDT"),
        core::ExchangeId::binance(),
        core::Direction::BUY,
        1.0,
        core::Timestamp()
    };
    
    EXPECT_CALL(*mockDataInterface, getCurrentPrice("BTC/USDT", _))
        .Times(2)  // Should be called twice due to cache expiry
        .WillRepeatedly(Return(50000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility("BTC/USDT", "1d", _))
        .Times(2)
        .WillRepeatedly(Return(0.15));
    
    // First call
    auto result1 = exposureCalculator->validateExposure(signal);
    EXPECT_EQ(result1, ExposureValidationResult::APPROVED);
    
    // Wait for cache to expire (test config has 100ms expiry)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Second call should trigger cache miss
    auto result2 = exposureCalculator->validateExposure(signal);
    EXPECT_EQ(result2, ExposureValidationResult::APPROVED);
}

// Test different violation types
TEST_F(ExposureCalculatorTest, DifferentViolationTypes) {
    ExposureLimitConfig limits;
    limits.maxTotalExposure = 100000.0;
    limits.maxAssetExposure = 25000.0;    // Low asset limit
    limits.maxStrategyExposure = 50000.0;
    limits.maxExchangeExposure = 80000.0;
    exposureCalculator->updateExposureLimits(limits);
    
    // Signal that would exceed asset limit
    TradingSignal assetLimitSignal{
        "signal_asset_limit",
        "BTC/USDT",
        core::ExchangeId::binance(),
        core::Direction::BUY,
        1.0,  // 1 BTC = $50k > $25k asset limit
        "strategy_test"
    };
    
    EXPECT_CALL(*mockDataInterface, getCurrentPrice("BTC/USDT", _))
        .WillRepeatedly(Return(50000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility("BTC/USDT", "1d", _))
        .WillRepeatedly(Return(0.15));
    
    auto result = exposureCalculator->validateExposure(assetLimitSignal);
    EXPECT_EQ(result, ExposureValidationResult::ASSET_LIMIT_EXCEEDED);
    
    // Test detailed violations
    auto violations = exposureCalculator->getViolationDetails(assetLimitSignal);
    EXPECT_FALSE(violations.empty());
    EXPECT_EQ(violations[0].type, ViolationType::EXPOSURE_LIMIT);
}

// Test async market data updates
TEST_F(ExposureCalculatorTest, AsyncMarketDataUpdate) {
    core::MarketData marketData;
    marketData.tradingPair = TradingPair::fromString("BTC/USDT");
    marketData.price = 55000.0;  // Updated price
    marketData.priceChangePercent24h = 10.0;  // 10% change
    
    // Update market data asynchronously
    auto handle = exposureCalculator->updateMarketDataAsync(marketData);
    
    // Give the coroutine time to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Verify the new price is cached
    TradingSignal signal{
        core::SignalId{"signal_async_test"},
        core::Symbol::fromString("BTC/USDT"),
        core::ExchangeId::binance(),
        core::Direction::BUY,
        1.0,
        core::Timestamp()
    };
    
    // Should use cached price, not call data interface
    EXPECT_CALL(*mockDataInterface, getCurrentPrice(_, _))
        .Times(0);
    EXPECT_CALL(*mockDataInterface, getVolatility(_, _, _))
        .Times(0);
    
    auto result = exposureCalculator->validateExposure(signal);
    EXPECT_EQ(result, ExposureValidationResult::APPROVED);
}

// Test error handling
TEST_F(ExposureCalculatorTest, ErrorHandling) {
    // Mock data interface to throw exception
    EXPECT_CALL(*mockDataInterface, getCurrentPrice(_, _))
        .WillOnce(Throw(std::runtime_error("Price service unavailable")));
    
    TradingSignal signal{
        core::SignalId{"signal_error_test"},
        core::Symbol::fromString("INVALID/PAIR"),
        core::ExchangeId::binance(),
        core::Direction::BUY,
        1.0,
        core::Timestamp()
    };
    
    // Should handle error gracefully and return calculation error
    auto result = exposureCalculator->validateExposure(signal);
    EXPECT_EQ(result, ExposureValidationResult::CALCULATION_ERROR);
}

// Test limits update
TEST_F(ExposureCalculatorTest, LimitsUpdate) {
    ExposureLimitConfig originalLimits = exposureCalculator->getExposureLimits();
    
    ExposureLimitConfig newLimits;
    newLimits.maxTotalExposure = 200000.0;
    newLimits.maxAssetExposure = 50000.0;
    newLimits.maxStrategyExposure = 100000.0;
    newLimits.maxExchangeExposure = 150000.0;
    newLimits.volatilityMultiplier = 2.0;
    
    exposureCalculator->updateExposureLimits(newLimits);
    
    ExposureLimitConfig updatedLimits = exposureCalculator->getExposureLimits();
    
    EXPECT_EQ(updatedLimits.maxTotalExposure, 200000.0);
    EXPECT_EQ(updatedLimits.maxAssetExposure, 50000.0);
    EXPECT_EQ(updatedLimits.maxStrategyExposure, 100000.0);
    EXPECT_EQ(updatedLimits.maxExchangeExposure, 150000.0);
    EXPECT_EQ(updatedLimits.volatilityMultiplier, 2.0);
}

// Test cache clearing
 
TEST_F(ExposureCalculatorTest, CacheClearing) {
    TradingSignal signal{
        core::SignalId{"signal_cache_clear"},
        core::Symbol::fromString("BTC/USDT"),
        core::ExchangeId::binance(),
        core::Direction::BUY
    };
    
    EXPECT_CALL(*mockDataInterface, getCurrentPrice("BTC/USDT", _))
        .Times(2)  // Should be called twice after cache clear
        .WillRepeatedly(Return(50000.0));
    EXPECT_CALL(*mockDataInterface, getVolatility("BTC/USDT", "1d", _))
        .Times(2)
        .WillRepeatedly(Return(0.15));
    
    // First call to populate cache
    auto result1 = exposureCalculator->validateExposure(signal);
    EXPECT_EQ(result1, ExposureValidationResult::APPROVED);
    
    // Clear caches
    exposureCalculator->clearCaches();
    
    // Second call should trigger cache miss
    auto result2 = exposureCalculator->validateExposure(signal);
    EXPECT_EQ(result2, ExposureValidationResult::APPROVED);
    
    // Performance stats should be reset
    const auto& stats = exposureCalculator->getPerformanceStats();
    EXPECT_EQ(stats.totalValidations.load(), 1);  // Only the second call after reset
}

// Memory leak test
TEST_F(ExposureCalculatorTest, MemoryUsage) {
    // This test would typically use valgrind or similar tools
    // Here we just ensure no obvious leaks by creating many objects
    
    for (int i = 0; i < 10000; ++i) {
        // Construct TradingSignal using explicit member assignments to avoid
        // ambiguous/incorrect aggregate initialization ordering.
        TradingSignal signal{};
        signal.id = std::format("signal_{}", i);
        signal.setTradingPair(core::Symbol::fromString("BTC/USDT"));
        signal.setExchangeId(core::ExchangeId::binance());
        signal.setDirection(core::Direction::BUY);
        signal.setTargetSize(0.1);
        signal.setStrategyId(std::format("strategy_{}", i % 10));

        EXPECT_CALL(*mockDataInterface, getCurrentPrice(_, _))
            .WillRepeatedly(Return(50000.0));
        EXPECT_CALL(*mockDataInterface, getVolatility(_, _, _))
            .WillRepeatedly(Return(0.15));
        
        auto result = exposureCalculator->validateExposure(signal);
        EXPECT_NE(result, ExposureValidationResult::CALCULATION_ERROR);
    }
    
    // Force cache cleanup
    exposureCalculator->clearCaches();
    
    // If we reach here without crashes, memory management is likely correct
    SUCCEED();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}