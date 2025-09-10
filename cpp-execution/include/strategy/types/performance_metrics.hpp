/**
 * @file strategy/types/performance_metrics.hpp
 * @brief Performance metrics for strategy evaluation
 * 
 * This file defines performance metrics used to evaluate trading strategies.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once
#include <string>
#include <vector>
#include <core/market_types.hpp>
 namespace trading {
 namespace strategy {
 

 /**
  * Performance metrics for strategy evaluation
  */
 struct PerformanceMetrics {
    double pnl;             // Profit and loss
    double sharpeRatio;    // Sharpe ratio
    double sortino_ratio;   // Sortino ratio
    double benchmark_return; // Benchmark return
    double totalReturn;     // Total return
    double maxDrawdown;    // Maximum drawdown
    double winRate;        // Win rate (percentage)
    double profitFactor;   // Profit factor (gross profit / gross loss)
    int trade_count;        // Number of trades
    double avg_trade_pnl;   // Average P&L per trade
    double volatility;      // Volatility of returns
    double downside_deviation; // Downside deviation
    double value_at_risk;  // Value at risk
    double expected_shortfall; // Expected shortfall
    double alpha;         // Alpha (excess return over benchmark)
    double beta;          // Beta (sensitivity to benchmark)
    double information_ratio; // Information ratio (excess return / tracking error)
    double turnover;       // Turnover rate
    double currentDrawdown; // Current drawdown
    // Timestamp
    core::Timestamp timestamp; // Timestamp of the metrics
    // Default constructor with zeroed metrics
    PerformanceMetrics()
        : pnl(0.0), sharpeRatio(0.0), maxDrawdown(0.0),
           winRate(0.0), profitFactor(0.0), trade_count(0), avg_trade_pnl(0.0) {}
 };
 
/**
 * Results for a specific variant in an experiment
 */
struct VariantResult {
    std::string variantId;                        // Variant identifier
    uint64_t impressions;                         // Number of times variant was assigned
    uint64_t conversions;                         // Number of signals that led to trades
    double conversionRate;                        // Conversion rate (conversions / impressions)
    std::vector<PerformanceMetrics> metrics;      // Individual performance metrics
    PerformanceMetrics aggregateMetrics;          // Aggregated performance metrics
    
    VariantResult() :
        impressions(0),
        conversions(0),
        conversionRate(0.0) {}
};


/**
 * Results for an A/B testing experiment
 */
struct ExperimentResults {
    std::string experimentId;                             // Experiment identifier
    std::string experimentName;                           // Experiment name
    std::map<std::string, VariantResult> variantResults;  // Results by variant
    std::chrono::system_clock::time_point lastUpdateTime; // Last update time
    
    ExperimentResults() : 
        experimentId(""),
        experimentName(""),
        lastUpdateTime(std::chrono::system_clock::now()) {}
};

/**
 * Structure representing a variant of the strategy for A/B testing
 */
struct StrategyVariant {
    std::string variantId;                     // Unique identifier for the variant
    std::string description;                    // Description of the variant
    std::map<std::string, double> parameters;   // Parameter overrides for this variant
    
    StrategyVariant() = default;
    
    StrategyVariant(const std::string& id, const std::string& desc)
        : variantId(id), description(desc) {}
        
    void setParameter(const std::string& name, double value) {
        parameters[name] = value;
    }
    
    double getParameter(const std::string& name, double defaultValue = 0.0) const {
        auto it = parameters.find(name);
        return (it != parameters.end()) ? it->second : defaultValue;
    }
};



 } // namespace strategy
 } // namespace trading