/**
 * @file strategy_metrics.hpp
 * @brief Common metrics and calculations for trading strategies
 * 
 * This component provides common metrics calculations and utilities
 * used across various parts of the strategy implementation.
 * 
 * @author Trading System Team
 * @date May 6, 2025
 */

 #pragma once

 #include <string>
 #include <vector>
 #include <map>
 #include <chrono>
 #include <functional>
 #include <array>
//  #include "strategy/types.hpp"
 #include "strategy/types/parameter_set.hpp"
 #include "strategy/types/performance_metrics.hpp"
 
 namespace trading {
 namespace strategy {
 
 /**
  * Provides common metrics calculations and utilities for trading strategies
  */
 class StrategyMetrics {
 public:
     /**
      * Creates a strategy metrics calculator
      */
     StrategyMetrics();
     
     /**
      * Creates a strategy metrics calculator with specified parameters
      */
     StrategyMetrics(const ParameterSet& params);
     
     virtual ~StrategyMetrics() = default;
     
     /**
      * Calculate exponential moving average
      * 
      * @param values Input values
      * @param alpha Smoothing factor (0-1)
      * @return EMA values
      */
     static std::vector<double> calculateEMA(const std::vector<double>& values, double alpha);
     
     /**
      * Calculate exponential moving average for a new value
      * 
      * @param current_ema Current EMA value
      * @param new_value New value to include
      * @param alpha Smoothing factor (0-1)
      * @return Updated EMA value
      */
     static double updateEMA(double current_ema, double new_value, double alpha);
     
     /**
      * Calculate simple moving average
      * 
      * @param values Input values
      * @param window Window size
      * @return SMA values
      */
     static std::vector<double> calculateSMA(const std::vector<double>& values, int window);
     
     /**
      * Calculate Bollinger Bands
      * 
      * @param values Input values
      * @param window Window size
      * @param num_std_dev Number of standard deviations
      * @return Array of [middle band, upper band, lower band]
      */
     static std::vector<std::array<double, 3>> calculateBollingerBands(
         const std::vector<double>& values, int window, double num_std_dev = 2.0);
     
     /**
      * Calculate relative strength index (RSI)
      * 
      * @param values Input values
      * @param window Window size
      * @return RSI values
      */
     static std::vector<double> calculateRSI(const std::vector<double>& values, int window);
     
     /**
      * Calculate moving average convergence divergence (MACD)
      * 
      * @param values Input values
      * @param fast_period Fast EMA period
      * @param slow_period Slow EMA period
      * @param signal_period Signal EMA period
      * @return Array of [MACD line, signal line, histogram]
      */
     static std::vector<std::array<double, 3>> calculateMACD(
         const std::vector<double>& values, int fast_period = 12, int slow_period = 26, int signal_period = 9);
     
     /**
      * Calculate standard deviation
      * 
      * @param values Input values
      * @return Standard deviation
      */
     static double calculateStdDev(const std::vector<double>& values);
     
     /**
      * Calculate Pearson correlation coefficient
      * 
      * @param x First set of values
      * @param y Second set of values
      * @return Correlation coefficient (-1 to 1)
      */
     static double calculateCorrelation(const std::vector<double>& x, const std::vector<double>& y);
     
     /**
      * Calculate z-score
      * 
      * @param value Value to calculate z-score for
      * @param mean Mean of the distribution
      * @param std_dev Standard deviation of the distribution
      * @return Z-score
      */
     static double calculateZScore(double value, double mean, double std_dev);
     
     /**
      * Normalize values to range [0, 1]
      * 
      * @param values Input values
      * @return Normalized values
      */
     static std::vector<double> normalizeValues(const std::vector<double>& values);
     
     /**
      * Calculate exponential weighted moving average
      * 
      * @param values Input values
      * @param com Center of mass (higher values = slower decay)
      * @return EWMA values
      */
     static std::vector<double> calculateEWMA(const std::vector<double>& values, double com);
     
     /**
      * Calculate signal strength based on multiple metrics
      * 
      * @param metrics Map of metric names to values
      * @param weights Map of metric names to weights
      * @return Weighted signal strength (0-1)
      */
     static double calculateSignalStrength(
         const std::map<std::string, double>& metrics,
         const std::map<std::string, double>& weights);
     
     /**
      * Calculate performance metrics for a strategy
      * 
      * @param pnl_series Series of P&L values
      * @param benchmark_series Optional benchmark series for comparison
      * @param risk_free_rate Risk-free rate for Sharpe calculation
      * @return Performance metrics
      */
     static PerformanceMetrics calculatePerformanceMetrics(
         const std::vector<double>& pnl_series,
         const std::vector<double>& benchmark_series = {},
         double risk_free_rate = 0.01);
     
     /**
      * Calculate maximum drawdown
      * 
      * @param equity_curve Equity curve values
      * @return Maximum drawdown as a percentage
      */
     static double calculateMaxDrawdown(const std::vector<double>& equity_curve);
     
     /**
      * Calculate Sharpe ratio
      * 
      * @param returns Series of returns
      * @param risk_free_rate Risk-free rate
      * @return Sharpe ratio
      */
     static double calculateSharpeRatio(
         const std::vector<double>& returns,
         double risk_free_rate = 0.01);
     
     /**
      * Calculate Sortino ratio
      * 
      * @param returns Series of returns
      * @param risk_free_rate Risk-free rate
      * @return Sortino ratio
      */
     static double calculateSortinoRatio(
         const std::vector<double>& returns,
         double risk_free_rate = 0.01);
     
     /**
      * Calculate win rate
      * 
      * @param trades Series of trade P&Ls
      * @return Win rate as a percentage
      */
     static double calculateWinRate(const std::vector<double>& trades);
     
     /**
      * Calculate profit factor
      * 
      * @param trades Series of trade P&Ls
      * @return Profit factor (gross profit / gross loss)
      */
     static double calculateProfitFactor(const std::vector<double>& trades);
     
     /**
      * Calculate expected shortfall (CVaR)
      * 
      * @param returns Series of returns
      * @param confidence_level Confidence level (e.g., 0.95)
      * @return Expected shortfall
      */
     static double calculateExpectedShortfall(
         const std::vector<double>& returns,
         double confidence_level = 0.95);
     
     /**
      * Calculate value at risk (VaR)
      * 
      * @param returns Series of returns
      * @param confidence_level Confidence level (e.g., 0.95)
      * @param method Method ('historical', 'parametric', or 'monte_carlo')
      * @return Value at risk
      */
     static double calculateValueAtRisk(
         const std::vector<double>& returns,
         double confidence_level = 0.95,
         const std::string& method = "historical");
     
     /**
      * Calculate Jarque-Bera test statistic for normality
      * 
      * @param values Input values
      * @return JB test statistic
      */
     static double calculateJarqueBera(const std::vector<double>& values);
     
     /**
      * Calculate skewness of a distribution
      * 
      * @param values Input values
      * @return Skewness
      */
     static double calculateSkewness(const std::vector<double>& values);
     
     /**
      * Calculate kurtosis of a distribution
      * 
      * @param values Input values
      * @return Kurtosis
      */
     static double calculateKurtosis(const std::vector<double>& values);
     
     /**
      * Check for significant trend in a time series
      * 
      * @param values Input values
      * @param threshold Z-score threshold for significance
      * @return True if a significant trend exists
      */
     static bool hasTrend(const std::vector<double>& values, double threshold = 1.96);
     
     /**
      * Calculate Hurst exponent to measure long-term memory in a time series
      * 
      * @param values Input values
      * @return Hurst exponent
      */
     static double calculateHurstExponent(const std::vector<double>& values);
     
     /**
      * Detect change points in a time series
      * 
      * @param values Input values
      * @param method Method ('cusum', 'pelt', or 'binseg')
      * @return Vector of change point indices
      */
     static std::vector<size_t> detectChangePoints(
         const std::vector<double>& values,
         const std::string& method = "cusum");

     /**
      * Helper function to detect change points using CUSUM method
      * 
      * @param values Input values
      * @return Vector of change point indices
      */
     static std::vector<size_t> detectChangePointsCusum(
         const std::vector<double>& values);

     /**
      * Helper function to detect change points using binary segmentation
      * 
      * @param values Input values
      * @return Vector of change point indices
      */
     static std::vector<size_t> detectChangePointsBinarySegmentation(
         const std::vector<double>& values);

     /**
      * Implementation of inverse error function for use in VaR calculation
      * 
      * @param x Input value between -1 and 1
      * @return Inverse error function
      */
     static double erfinv(double x);
 };

 
 } // namespace strategy
 } // namespace trading