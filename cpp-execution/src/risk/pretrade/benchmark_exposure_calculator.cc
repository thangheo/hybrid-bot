/**
 * @file benchmark_exposure_calculator.cpp
 * @brief Performance benchmarks for ExposureCalculator
 * 
 * Measures performance characteristics under various load conditions
 * to ensure HFT latency requirements are met.
 * 
 * @author Trading System Team
 * @date May 27, 2025
 */

#include "risk/pretrade/exposure_calculator.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include "risk/interfaces/data_interface.hpp"
#include "core/market_types.hpp"
#include "risk/risk_types.hpp"
#include "common/logger.hpp"

#include <memory>
#include <random>
#include <vector>
#include <thread>
#include <iostream>
#include <chrono>
#include <atomic>
#include <sstream>

#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#endif

using namespace trading::risk;
using namespace trading::core;

// Simple benchmark timer class since we're not using Google Benchmark
class BenchmarkTimer {
public:
    void start() {
        startTime_ = std::chrono::high_resolution_clock::now();
    }
    
    void stop() {
        endTime_ = std::chrono::high_resolution_clock::now();
    }
    
    double elapsedNanoseconds() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(endTime_ - startTime_).count();
    }
    
    double elapsedMicroseconds() const {
        return elapsedNanoseconds() / 1000.0;
    }

private:
    std::chrono::high_resolution_clock::time_point startTime_, endTime_;
};

// Mock implementations for benchmarking
class BenchmarkPositionTracker : public PositionTracker {
public:
    BenchmarkPositionTracker(size_t numPositions, const RiskConfig& config, std::shared_ptr<DataInterface> dataInterface) 
        : PositionTracker(config, dataInterface) {
        
        // Generate test positions using risk::StrategyId explicitly
        TradingPair btcPair{"BTC", "USDT", "BTC/USDT"};
        trading::risk::StrategyId strategy{"benchmark_strategy", "v1"};
        
        std::mt19937 gen(42); // Fixed seed for reproducibility
        std::uniform_real_distribution<> sizeDist(0.1, 2.0);
        std::uniform_real_distribution<> priceDist(45000.0, 55000.0);
        
        for (size_t i = 0; i < numPositions; ++i) {
            positions_.emplace_back(
                ExchangeId::binance(),
                btcPair,
                strategy,
                sizeDist(gen),
                priceDist(gen)
            );
        }
    }
    
    // Override base class methods with correct signatures matching position_tracker.hpp
    std::vector<Position> getAllPositions() const override {
        return positions_;
    }
    
    std::vector<Position> getExchangePositions(const ExchangeId& /* exchangeId */) const override {
        return positions_; // Simplified for benchmark
    }
    
    std::vector<Position> getPositionsByTradingPair(const TradingPair& tradingPair) const override {
        std::vector<Position> result;
        for (const auto& pos : positions_) {
            if (pos.getTradingPair().symbol == tradingPair.symbol) {
                result.push_back(pos);
            }
        }
        return result;
    }
    
    std::vector<Position> getStrategyPositions(const trading::risk::StrategyId& /* strategyId */) const override {
        return positions_; // Simplified for benchmark
    }

private:
    std::vector<Position> positions_;
};

class BenchmarkDataInterface : public DataInterface {
public:
    bool initialize() override { return true; }
    
    std::optional<MarketData> getMarketData(
        const std::string& tradingPair,
        const std::optional<std::string>& /* exchangeId */ = std::nullopt
    ) override {
        MarketData data;
        data.tradingPair = tradingPair;
        data.price = 50000.0;
        data.exchangeId = ExchangeId::binance();
        data.timestamp = std::chrono::system_clock::now();
        return data;
    }
    
    double getCurrentPrice(
        const std::string& /* tradingPair */,
        const std::optional<std::string>& /* exchangeId */ = std::nullopt
    ) override {
        return 50000.0; // Fixed price for benchmark consistency
    }
    
    double getVolatility(
        const std::string& /* tradingPair */,
        const std::string& /* timeframe */,
        const std::optional<std::string>& /* exchangeId */ = std::nullopt
    ) override {
        return 0.15; // Fixed 15% volatility
    }
    
    // Implement other required methods as no-ops for benchmarking
    std::vector<std::pair<Timestamp, double>> getHistoricalPrices(
        const std::string&, const std::string&, int, const std::optional<std::string>&
    ) override { return {}; }
    
    std::vector<OrderRequest> getOrderHistory(
        const ExchangeId&, const std::string&, int
    ) override { return {}; }
    
    std::map<std::string, double> getExchangeBalances(
        const ExchangeId&
    ) override { return {}; }
    
    void registerMarketDataCallback(
        const std::function<void(const MarketData&)>&
    ) override {}
    
    bool subscribeMarketData(
        const std::string&, const std::optional<std::string>&
    ) override { return true; }
    
    bool unsubscribeMarketData(
        const std::string&, const std::optional<std::string>&
    ) override { return true; }
    
    std::map<std::string, double> getTradingFees(
        const ExchangeId&, const std::optional<std::string>&
    ) override { return {}; }
};

// Helper function to create a trading signal - using string formatting without std::format
TradingSignal createTestSignal(const std::string& id, const std::string& pair, 
                              Direction direction, double size, const std::string& strategy) {
    TradingSignal signal;
    signal.signal_id = id;
    signal.tradingPair = pair;
    signal.direction = direction;
    signal.target_size = size;
    signal.strategy_id = strategy;
    signal.exchange = ExchangeId::binance();
    signal.strength = 0.8;
    signal.signal_ttl = std::chrono::milliseconds(5000);
    return signal;
}

// Benchmark fixture class
class ExposureCalculatorBenchmark {
public:
    void setup(size_t numPositions) {
        RiskConfig riskConfig;
        dataInterface_ = std::make_shared<BenchmarkDataInterface>();
        positionTracker_ = std::make_shared<BenchmarkPositionTracker>(numPositions, riskConfig, dataInterface_);
        
        ExposureCalculator::Config config;
        config.cacheExpiryTime = std::chrono::milliseconds(1000);
        config.enableOptimizations = true;
        
        exposureCalculator_ = std::make_unique<ExposureCalculator>(
            positionTracker_, dataInterface_, config
        );
        
        bool initResult = exposureCalculator_->initialize(riskConfig);
        if (!initResult) {
            throw std::runtime_error("Failed to initialize exposure calculator");
        }
        
        // Pre-generate signals for consistent benchmarking
        generateTestSignals();
    }
    
    void teardown() {
        exposureCalculator_.reset();
        positionTracker_.reset();
        dataInterface_.reset();
    }

    std::shared_ptr<BenchmarkPositionTracker> positionTracker_;
    std::shared_ptr<BenchmarkDataInterface> dataInterface_;
    std::unique_ptr<ExposureCalculator> exposureCalculator_;
    std::vector<TradingSignal> signals_;

protected:
    void generateTestSignals() {
        std::mt19937 gen(123);
        std::uniform_real_distribution<> sizeDist(0.1, 2.0);
        std::vector<std::string> tradingPairs = {"BTC/USDT", "ETH/USDT", "ADA/USDT", "DOT/USDT"};
        std::vector<std::string> strategies = {"strategy_1", "strategy_2", "strategy_3"};
        
        for (int i = 0; i < 10000; ++i) {
            // Create signal ID using stringstream instead of std::format
            std::stringstream ss;
            ss << "signal_" << i;
            
            signals_.push_back(createTestSignal(
                ss.str(),
                tradingPairs[i % tradingPairs.size()],
                (i % 2 == 0) ? Direction::BUY : Direction::SELL,
                sizeDist(gen),
                strategies[i % strategies.size()]
            ));
        }
    }
};

// Benchmark functions
void benchmarkSingleValidation(size_t numPositions, size_t iterations) {
    ExposureCalculatorBenchmark benchmark;
    benchmark.setup(numPositions);
    
    BenchmarkTimer timer;
    size_t signalIndex = 0;
    
    timer.start();
    for (size_t i = 0; i < iterations; ++i) {
        const auto& signal = benchmark.signals_[signalIndex % benchmark.signals_.size()];
        auto result = benchmark.exposureCalculator_->validateExposure(signal);
        (void)result; // Prevent optimization
        signalIndex++;
    }
    timer.stop();
    
    double avgLatencyNs = timer.elapsedNanoseconds() / iterations;
    double avgLatencyUs = timer.elapsedMicroseconds() / iterations;
    
    std::cout << "Single Validation Benchmark (Positions: " << numPositions << ")\n";
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Average latency: " << avgLatencyNs << " ns (" << avgLatencyUs << " μs)\n";
    std::cout << "  Total time: " << timer.elapsedMicroseconds() << " μs\n";
    std::cout << "  Throughput: " << (iterations * 1000000.0 / timer.elapsedMicroseconds()) << " ops/sec\n\n";
    
    benchmark.teardown();
}

void benchmarkTotalExposureCalculation(size_t numPositions, size_t iterations) {
    ExposureCalculatorBenchmark benchmark;
    benchmark.setup(numPositions);
    
    BenchmarkTimer timer;
    
    timer.start();
    for (size_t i = 0; i < iterations; ++i) {
        auto result = benchmark.exposureCalculator_->calculateTotalExposure();
        (void)result; // Prevent optimization
    }
    timer.stop();
    
    double avgLatencyNs = timer.elapsedNanoseconds() / iterations;
    double avgLatencyUs = timer.elapsedMicroseconds() / iterations;
    
    std::cout << "Total Exposure Calculation Benchmark (Positions: " << numPositions << ")\n";
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Average latency: " << avgLatencyNs << " ns (" << avgLatencyUs << " μs)\n";
    std::cout << "  Total time: " << timer.elapsedMicroseconds() << " μs\n\n";
    
    benchmark.teardown();
}

void benchmarkCachePerformance(size_t numPositions, size_t iterations) {
    ExposureCalculatorBenchmark benchmark;
    benchmark.setup(numPositions);
    
    // Use the same signal repeatedly to test cache hits
    const auto& signal = benchmark.signals_[0];
    
    BenchmarkTimer timer;
    
    timer.start();
    for (size_t i = 0; i < iterations; ++i) {
        auto result = benchmark.exposureCalculator_->validateExposure(signal);
        (void)result; // Prevent optimization
    }
    timer.stop();
    
    double avgLatencyNs = timer.elapsedNanoseconds() / iterations;
    double avgLatencyUs = timer.elapsedMicroseconds() / iterations;
    
    std::cout << "Cache Performance Benchmark (Positions: " << numPositions << ")\n";
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Average latency: " << avgLatencyNs << " ns (" << avgLatencyUs << " μs)\n";
    std::cout << "  Note: Repeated validations should show improved performance due to caching\n\n";
    
    benchmark.teardown();
}

void benchmarkMultithreadedValidation(size_t numPositions, int numThreads, size_t iterationsPerThread) {
    ExposureCalculatorBenchmark benchmark;
    benchmark.setup(numPositions);
    
    std::atomic<size_t> completedValidations{0};
    BenchmarkTimer timer;
    
    timer.start();
    
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (size_t i = 0; i < iterationsPerThread; ++i) {
                const auto& signal = benchmark.signals_[(t * iterationsPerThread + i) % benchmark.signals_.size()];
                auto result = benchmark.exposureCalculator_->validateExposure(signal);
                (void)result; // Prevent optimization
                completedValidations.fetch_add(1);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    timer.stop();
    
    size_t totalValidations = completedValidations.load();
    double avgLatencyNs = timer.elapsedNanoseconds() / totalValidations;
    double avgLatencyUs = timer.elapsedMicroseconds() / totalValidations;
    
    std::cout << "Multithreaded Validation Benchmark (Positions: " << numPositions << ", Threads: " << numThreads << ")\n";
    std::cout << "  Total validations: " << totalValidations << "\n";
    std::cout << "  Average latency: " << avgLatencyNs << " ns (" << avgLatencyUs << " μs)\n";
    std::cout << "  Total time: " << timer.elapsedMicroseconds() << " μs\n";
    std::cout << "  Throughput: " << (totalValidations * 1000000.0 / timer.elapsedMicroseconds()) << " ops/sec\n\n";
    
    benchmark.teardown();
}

// Main benchmark runner
int main(int argc, char** argv) {
    (void)argc; // Suppress unused parameter warning
    (void)argv;
    
    std::cout << "=== ExposureCalculator Performance Benchmarks ===\n\n";
    
    // Set CPU affinity for more consistent results (Linux)
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
#endif
    
    try {
        // Single validation benchmarks with different portfolio sizes
        std::vector<size_t> positionCounts = {0, 10, 100, 1000, 10000};
        const size_t iterations = 100000;
        
        for (size_t positions : positionCounts) {
            benchmarkSingleValidation(positions, iterations);
        }
        
        // Total exposure calculation benchmarks
        std::vector<size_t> exposurePositionCounts = {100, 1000, 10000};
        const size_t exposureIterations = 10000;
        
        for (size_t positions : exposurePositionCounts) {
            benchmarkTotalExposureCalculation(positions, exposureIterations);
        }
        
        // Cache performance benchmark
        benchmarkCachePerformance(100, 10000);
        
        // Multithreaded benchmarks
        std::vector<int> threadCounts = {1, 2, 4, 8, 16};
        const size_t iterationsPerThread = 1000;
        
        for (int threads : threadCounts) {
            benchmarkMultithreadedValidation(100, threads, iterationsPerThread);
        }
        
        std::cout << "=== Benchmark Summary ===\n";
        std::cout << "Target latency for HFT: < 10 microseconds\n";
        std::cout << "All benchmarks should show sub-microsecond performance for single validations\n";
        std::cout << "Cache performance should show improved latency for repeated validations\n";
        std::cout << "Multithreaded performance should scale with thread count up to CPU limits\n";
        
    } catch (const std::exception& ex) {
        std::cerr << "Benchmark failed: " << ex.what() << std::endl;
        return 1;
    }
    
    return 0;
}