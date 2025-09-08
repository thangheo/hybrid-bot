/**
 * Utilities for risk calculations
 *
 * File: risk_metrics.hpp
 * Created: 2025-04-29
 */
#pragma once

#include "../risk_types.hpp"

#include <vector>
#include <string>
#include <map>
#include <functional>

namespace trading {
namespace risk {

/**
 * RiskMetrics provides utility functions for calculating common risk metrics.
 * It implements statistical utilities and risk models.
 */
class RiskMetrics {
public:
    /**
     * Calculate Value at Risk (VaR)
     * @param returns Vector of returns
     * @param confidence Confidence level (e.g., 0.95)
     * @return VaR value
     */
    static double calculateVaR(const std::vector<double>& returns, double confidence);
    
    /**
     * Calculate Expected Shortfall (Conditional VaR)
     * @param returns Vector of returns
     * @param confidence Confidence level (e.g., 0.95)
     * @return Expected Shortfall value
     */
    static double calculateExpectedShortfall(const std::vector<double>& returns, double confidence);
    
    /**
     * Calculate volatility
     * @param prices Vector of prices
     * @param annualize Whether to annualize the result
     * @return Volatility value
     */
    static double calculateVolatility(const std::vector<double>& prices, bool annualize = true);
    
    /**
     * Calculate Sharpe ratio
     * @param returns Vector of returns
     * @param riskFreeRate Risk-free rate
     * @return Sharpe ratio
     */
    static double calculateSharpeRatio(const std::vector<double>& returns, double riskFreeRate);
    
    /**
     * Calculate maximum drawdown
     * @param prices Vector of prices
     * @return Maximum drawdown percentage
     */
    static double calculateMaxDrawdown(const std::vector<double>& prices);
    
    /**
     * Calculate correlation between two series
     * @param series1 First data series
     * @param series2 Second data series
     * @return Correlation coefficient
     */
    static double calculateCorrelation(const std::vector<double>& series1, const std::vector<double>& series2);
    
    /**
     * Calculate correlation matrix
     * @param seriesMap Map of series names to data
     * @return Correlation matrix
     */
    static std::map<std::string, std::map<std::string, double>> calculateCorrelationMatrix(
        const std::map<std::string, std::vector<double>>& seriesMap);
    
    /**
     * Calculate beta of a series against a benchmark
     * @param returns Series returns
     * @param benchmarkReturns Benchmark returns
     * @return Beta value
     */
    static double calculateBeta(const std::vector<double>& returns, const std::vector<double>& benchmarkReturns);
    
    /**
     * Calculate exponential moving average
     * @param data Input data series
     * @param alpha Smoothing factor
     * @return EMA values
     */
    static std::vector<double> calculateEMA(const std::vector<double>& data, double alpha);
    
    /**
     * Calculate simple moving average
     * @param data Input data series
     * @param period Moving average period
     * @return SMA values
     */
    static std::vector<double> calculateSMA(const std::vector<double>& data, int period);
    
    /**
     * Apply a function to a rolling window of data
     * @param data Input data series
     * @param windowSize Window size
     * @param func Function to apply to each window
     * @return Results of rolling function application
     */
    static std::vector<double> rollingFunction(const std::vector<double>& data, int windowSize, 
                                           const std::function<double(const std::vector<double>&)>& func);
    
    /**
     * Convert returns to prices
     * @param returns Return series
     * @param initialPrice Initial price
     * @return Price series
     */
    static std::vector<double> returnsToPrices(const std::vector<double>& returns, double initialPrice = 100.0);
    
    /**
     * Convert prices to returns
     * @param prices Price series
     * @param logReturns Whether to calculate log returns
     * @return Return series
     */
    static std::vector<double> pricesToReturns(const std::vector<double>& prices, bool logReturns = false);
};

} // namespace risk
}