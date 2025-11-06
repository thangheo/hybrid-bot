#pragma once

#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <chrono>
#include "core/types.hpp"
#include "core/direction.hpp"

namespace trading {
namespace strategy {

/**
 * Profit-aware labeling system that only generates BUY/SELL signals
 * when expected profit exceeds trading costs (0.1% fee)
 */
class ProfitAwareLabeling {
public:
    struct ProfitThresholds {
        double minExpectedProfit = 0.001;  // 0.1% minimum expected profit
        double tradingCost = 0.001;        // 0.1% trading fee
        double confidenceThreshold = 0.65; // Minimum confidence for labeling
    };

    struct HistoricalReturn {
        double normalizedReturn;
        double realizedProfit;  // Actual profit/loss after fees
        double volatility;
        int regime;
        double confidence;
        std::chrono::milliseconds timestamp;
    };

private:
    std::map<int, std::deque<HistoricalReturn>> regimeHistory_;
    std::map<int, double> regimeProfitability_; // Track average profit per regime
    ProfitThresholds thresholds_;
    mutable std::mutex mutex_;
    
    static constexpr size_t MAX_HISTORY_SIZE = 1000;
    static constexpr double MIN_SAMPLES_FOR_ESTIMATION = 50;

public:
    ProfitAwareLabeling() : thresholds_{} {}
    
    ProfitAwareLabeling(const ProfitThresholds& thresholds) 
        : thresholds_(thresholds) {}

    /**
     * Calculate expected profit for a given signal strength and regime
     */
    double calculateExpectedProfit(double normalizedReturn, int regime, double confidence) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = regimeHistory_.find(regime);
        if (it == regimeHistory_.end() || it->second.size() < MIN_SAMPLES_FOR_ESTIMATION) {
            // Fallback: use simple normalized return as proxy
            return std::abs(normalizedReturn) - thresholds_.tradingCost;
        }

        const auto& history = it->second;
        
        // Find similar signals in history
        std::vector<double> similarOutcomes;
        const double similarityThreshold = 0.2; // Within 20% of current signal
        
        for (const auto& record : history) {
            double signalSimilarity = std::abs(record.normalizedReturn - normalizedReturn);
            double confidenceSimilarity = std::abs(record.confidence - confidence);
            
            if (signalSimilarity <= similarityThreshold && 
                confidenceSimilarity <= 0.1) { // Similar confidence
                similarOutcomes.push_back(record.realizedProfit);
            }
        }
        
        if (similarOutcomes.empty()) {
            // Use regime average profitability
            auto profitIt = regimeProfitability_.find(regime);
            if (profitIt != regimeProfitability_.end()) {
                return profitIt->second;
            }
            return std::abs(normalizedReturn) - thresholds_.tradingCost;
        }
        
        // Calculate expected profit from similar historical signals
        double sumProfit = 0.0;
        for (double profit : similarOutcomes) {
            sumProfit += profit;
        }
        
        return sumProfit / similarOutcomes.size();
    }

    /**
     * Generate profit-aware label
     */
    core::Direction generateProfitAwareLabel(
        double normalizedReturn, 
        int regime, 
        double confidence,
        double volatility
    ) const {
        
        // Only consider labeling if confidence is high enough
        if (confidence < thresholds_.confidenceThreshold) {
            return core::Direction::NEUTRAL;
        }
        
        // Calculate expected profit for BUY signal
        double expectedProfitBuy = calculateExpectedProfit(
            std::abs(normalizedReturn), regime, confidence
        );
        
        // Calculate expected profit for SELL signal  
        double expectedProfitSell = calculateExpectedProfit(
            std::abs(normalizedReturn), regime, confidence
        );
        
        // Only label if expected profit > minimum threshold + trading cost
        double minProfitThreshold = thresholds_.minExpectedProfit + thresholds_.tradingCost;
        
        if (normalizedReturn > 0 && expectedProfitBuy > minProfitThreshold) {
            return core::Direction::BUY;
        } else if (normalizedReturn < 0 && expectedProfitSell > minProfitThreshold) {
            return core::Direction::SELL;
        }
        
        return core::Direction::NEUTRAL;
    }

    /**
     * Update historical data with realized outcome
     */
    void updateHistory(
        double normalizedReturn,
        double realizedProfit, // Actual profit after fees
        int regime,
        double confidence,
        double volatility
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        HistoricalReturn record{
            normalizedReturn,
            realizedProfit,
            volatility,
            regime,
            confidence,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            )
        };
        
        auto& history = regimeHistory_[regime];
        history.push_back(record);
        
        // Maintain max history size
        while (history.size() > MAX_HISTORY_SIZE) {
            history.pop_front();
        }
        
        // Update regime profitability average
        updateRegimeProfitability(regime);
    }

    /**
     * Get profitability statistics for a regime
     */
    struct ProfitStats {
        double averageProfit = 0.0;
        double winRate = 0.0;
        double averageWin = 0.0;
        double averageLoss = 0.0;
        size_t totalTrades = 0;
    };

    ProfitStats getRegimeStats(int regime) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        ProfitStats stats;
        auto it = regimeHistory_.find(regime);
        if (it == regimeHistory_.end()) {
            return stats;
        }
        
        const auto& history = it->second;
        stats.totalTrades = history.size();
        
        if (history.empty()) return stats;
        
        double totalProfit = 0.0;
        size_t wins = 0;
        double totalWins = 0.0;
        double totalLosses = 0.0;
        
        for (const auto& record : history) {
            totalProfit += record.realizedProfit;
            if (record.realizedProfit > 0) {
                wins++;
                totalWins += record.realizedProfit;
            } else {
                totalLosses += record.realizedProfit;
            }
        }
        
        stats.averageProfit = totalProfit / history.size();
        stats.winRate = static_cast<double>(wins) / history.size();
        stats.averageWin = wins > 0 ? totalWins / wins : 0.0;
        stats.averageLoss = (history.size() - wins) > 0 ? totalLosses / (history.size() - wins) : 0.0;
        
        return stats;
    }

    /**
     * Set profit thresholds
     */
    void setThresholds(const ProfitThresholds& thresholds) {
        std::lock_guard<std::mutex> lock(mutex_);
        thresholds_ = thresholds;
    }

    ProfitThresholds getThresholds() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return thresholds_;
    }

private:
    void updateRegimeProfitability(int regime) {
        const auto& history = regimeHistory_[regime];
        if (history.empty()) return;
        
        double totalProfit = 0.0;
        for (const auto& record : history) {
            totalProfit += record.realizedProfit;
        }
        
        regimeProfitability_[regime] = totalProfit / history.size();
    }
};

} // namespace strategy
} // namespace trading