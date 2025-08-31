// ab_testing_example.cpp
#include "strategy/optimization/ab_testing_framework.hpp"
#include "strategy/optimization/strategy_variant_utils.hpp"
#include "strategy/types.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

using namespace trading::strategy;

/**
 * This example demonstrates how to set up and use the A/B testing framework
 * for a trading strategy.
 */
void runABTestingExample() {
    // Create a base parameter set
    ParameterSet baseParams;
    baseParams.setParameter("bidAskRatioThreshold", 1.5);
    baseParams.setParameter("depthImbalanceThreshold", 2.0);
    baseParams.setParameter("volumeSpikeThreshold", 3.0);
    baseParams.setParameter("orderFlowThreshold", 2.5);
    baseParams.setParameter("signalDecayMinutes", 5.0);
    baseParams.setParameter("minimumConfidence", 0.7);
    
    // Create an A/B testing framework
    ABTestingFramework abFramework;
    
    // Define an experiment with multiple variants
    Experiment experiment("exp-001", "Order Imbalance Strategy Parameter Optimization");
    
    // Add the control variant (baseline)
    StrategyVariant controlVariant = createDefaultVariant("control");
    experiment.addVariant("control", controlVariant, 0.25);  // 25% traffic
    
    // Add test variants with different parameter settings
    
    // Variant 1: More aggressive bidAskRatio
    std::map<std::string, double> variant1Params = {
        {"bidAskRatioThreshold", 1.2},
        {"minimumConfidence", 0.8}
    };
    StrategyVariant variant1 = createVariant(
        "aggressive-bid-ask", 
        "Lower bid/ask ratio threshold with higher confidence requirement",
        variant1Params
    );
    experiment.addVariant("aggressive-bid-ask", variant1, 0.25);  // 25% traffic
    
    // Variant 2: Higher volume spike sensitivity
    std::map<std::string, double> variant2Params = {
        {"volumeSpikeThreshold", 2.0},
        {"orderFlowThreshold", 2.0}
    };
    StrategyVariant variant2 = createVariant(
        "volume-sensitive", 
        "More sensitive to volume spikes and order flow",
        variant2Params
    );
    experiment.addVariant("volume-sensitive", variant2, 0.25);  // 25% traffic
    
    // Variant 3: Longer signal decay
    std::map<std::string, double> variant3Params = {
        {"signalDecayMinutes", 8.0}
    };
    StrategyVariant variant3 = createVariant(
        "longer-decay", 
        "Longer signal decay period",
        variant3Params
    );
    experiment.addVariant("longer-decay", variant3, 0.25);  // 25% traffic
    
    // Define the experiment
    abFramework.defineExperiment(experiment);
    
    // Enable multi-armed bandit optimization
    abFramework.enableAutomaticOptimization("exp-001", true);
    
    // Enable early stopping with 95% confidence
    abFramework.enableEarlyStopping("exp-001", true, 0.95);
    
    // Start the experiment
    abFramework.startExperiment("exp-001");
    
    std::cout << "Started experiment: " << experiment.getId() << " - " << experiment.getName() << std::endl;
    
    // Simulate some order book snapshots and variant assignments
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> priceDist(10000.0, 10500.0);
    std::uniform_real_distribution<> quantityDist(0.1, 10.0);
    
    std::vector<trading::core::ExchangeId> exchanges = {
        "BINANCE",
        "BITFINEX",
        "BITSTAMP",
        "HUOBI",
        "OKEX"
    };
    
    std::vector<std::string> symbols = {
        "BTC/USDT", 
        "ETH/USDT", 
        "SOL/USDT"
    };
    
    // Simulate 100 order book snapshots
    for (int i = 0; i < 100; ++i) {
        OrderBookSnapshot snapshot;
        snapshot.exchange = exchanges[i % exchanges.size()];
        snapshot.symbol = symbols[i % symbols.size()];
        snapshot.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        snapshot.sequenceNumber = i;
        
        // Generate some random bids and asks
        double midPrice = priceDist(gen);
        
        for (int j = 0; j < 10; ++j) {
            // Bids (below mid price)
            PriceLevel bidLevel;
            bidLevel.price = midPrice - (j + 1) * 0.5;
            bidLevel.quantity = quantityDist(gen);
            snapshot.bids.push_back(bidLevel);
            
            // Asks (above mid price)
            PriceLevel askLevel;
            askLevel.price = midPrice + (j + 1) * 0.5;
            askLevel.quantity = quantityDist(gen);
            snapshot.asks.push_back(askLevel);
        }
        
        // Assign a variant for this order book
        StrategyVariant assignedVariant = abFramework.assignVariant("exp-001", snapshot);
        
        // Apply the variant to our base parameters
        ParameterSet variantParams = applyVariant(baseParams, assignedVariant);
        
        std::cout << "Snapshot " << i << " assigned to variant: " << assignedVariant.variantId << std::endl;
        
        // Simulate some performance metrics for this variant
        // In reality, these would come from actual trading results
        trading::strategy::PerformanceMetrics metrics;
        
        // Simulate different performance for different variants
        if (assignedVariant.variantId == "control") {
            // Control variant has baseline performance
            metrics.sharpeRatio = 1.0 + (gen() % 100) / 100.0 * 0.5;  // 1.0-1.5
            metrics.totalReturn = 0.05 + (gen() % 100) / 100.0 * 0.05;  // 5-10%
            metrics.win_rate = 0.52 + (gen() % 100) / 100.0 * 0.08;  // 52-60%
        }
        else if (assignedVariant.variantId == "aggressive-bid-ask") {
            // This variant has higher return but more volatility
            metrics.sharpeRatio = 1.2 + (gen() % 100) / 100.0 * 0.6;  // 1.2-1.8
            metrics.totalReturn = 0.08 + (gen() % 100) / 100.0 * 0.07;  // 8-15%
            metrics.win_rate = 0.55 + (gen() % 100) / 100.0 * 0.1;  // 55-65%
        }
        else if (assignedVariant.variantId == "volume-sensitive") {
            // This variant has more modest performance
            metrics.sharpeRatio = 0.9 + (gen() % 100) / 100.0 * 0.4;  // 0.9-1.3
            metrics.totalReturn = 0.04 + (gen() % 100) / 100.0 * 0.04;  // 4-8%
            metrics.win_rate = 0.50 + (gen() % 100) / 100.0 * 0.08;  // 50-58%
        }
        else if (assignedVariant.variantId == "longer-decay") {
            // This variant has been performing well
            metrics.sharpeRatio = 1.3 + (gen() % 100) / 100.0 * 0.7;  // 1.3-2.0
            metrics.totalReturn = 0.07 + (gen() % 100) / 100.0 * 0.08;  // 7-15%
            metrics.win_rate = 0.58 + (gen() % 100) / 100.0 * 0.12;  // 58-70%
        }
        
        // Record the outcome
        abFramework.recordOutcome("exp-001", assignedVariant.variantId, metrics);
        std::cout << "Recorded outcome for variant: " << assignedVariant.variantId 
                  << " - Sharpe Ratio: " << metrics.sharpeRatio 
                  << ", Total Return: " << metrics.totalReturn 
                  << ", Win Rate: " << metrics.win_rate << std::endl;
    }
    
    // Get and display experiment results
    ExperimentResults results = abFramework.getResults("exp-001");
    
    std::cout << "\nExperiment Results:\n" << generateExperimentReport(results) << std::endl;
    
    // Stop the experiment
    abFramework.stopExperiment("exp-001");
    
    std::cout << "Experiment stopped." << std::endl;
}

int main() {
    runABTestingExample();
    return 0;
}