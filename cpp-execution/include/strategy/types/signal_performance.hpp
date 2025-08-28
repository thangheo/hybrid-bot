#pragma once

#include <unordered_map>
#include <string>
#include <chrono>
#include <vector>

namespace trading {
namespace strategy {

// Forward declarations
enum class ImbalanceType;

/**
 * Stores performance metrics for a single imbalance type
 */
struct TypePerformanceMetrics {
    double accuracy;           // Ratio of correct signals (0.0-1.0)
    double profitFactor;      // Ratio of profits to losses
    double avg_return;         // Average return per signal
    double maxDrawdown;       // Maximum drawdown
    int signal_count;          // Number of signals generated
    double avg_duration_ms;    // Average signal duration in milliseconds
    double avg_strength;       // Average signal strength
    
    // Additional metrics for detailed analysis
    double win_rate;           // Percentage of winning signals
    double avg_win;            // Average profit of winning signals
    double avg_loss;           // Average loss of losing signals
    int consecutive_wins;      // Maximum consecutive winning signals
    int consecutive_losses;    // Maximum consecutive losing signals
};

/**
 * Stores performance data for all imbalance signals
 * Used by ThresholdManager to adjust detection thresholds
 */
struct SignalPerformance {
    // Map of imbalance type to its performance metrics
    std::unordered_map<ImbalanceType, TypePerformanceMetrics> typePerformance;
    
    // Overall performance metrics
    double overall_accuracy;
    double overall_profit_factor;
    double overall_return;
    double sharpe_ratio;
    
    // Time period for these metrics
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    
    // Number of total signals analyzed
    int total_signals;
    
    // Market conditions during evaluation period
    double avg_volatility;
    double avg_volume;
    double market_trend;  // Positive value for uptrend, negative for downtrend
    
    // Constructor with defaults
    SignalPerformance() 
        : overall_accuracy(0.0)
        , overall_profit_factor(0.0)
        , overall_return(0.0)
        , sharpe_ratio(0.0)
        , total_signals(0)
        , avg_volatility(0.0)
        , avg_volume(0.0)
        , market_trend(0.0)
    {
        start_time = std::chrono::system_clock::now();
        end_time = start_time;
    }
    
    // Add a single performance record for an imbalance type
    void addTypePerformance(ImbalanceType type, const TypePerformanceMetrics& metrics) {
        typePerformance[type] = metrics;
        updateOverallMetrics();
    }
    
private:
    // Update overall metrics based on individual type metrics
    void updateOverallMetrics() {
        if (typePerformance.empty()) {
            return;
        }
        
        double total_accuracy = 0.0;
        double total_profit_factor = 0.0;
        double totalReturn = 0.0;
        int signal_count = 0;
        
        for (const auto& [type, metrics] : typePerformance) {
            total_accuracy += metrics.accuracy * metrics.signal_count;
            total_profit_factor += metrics.profitFactor * metrics.signal_count;
            totalReturn += metrics.avg_return * metrics.signal_count;
            signal_count += metrics.signal_count;
        }
        
        if (signal_count > 0) {
            overall_accuracy = total_accuracy / signal_count;
            overall_profit_factor = total_profit_factor / signal_count;
            overall_return = totalReturn;
            total_signals = signal_count;
        }
    }
};

} // namespace strategy
} // namespace trading