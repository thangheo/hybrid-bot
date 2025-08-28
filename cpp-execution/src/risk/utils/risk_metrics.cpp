/**
 * Implementation of risk metrics utilities
 *
 * File: risk_metrics.cpp
 * Created: 2025-04-29
 */

 #include "../utils/risk_metrics.hpp"
 #include <algorithm>
 #include <cmath>
 #include <numeric>
 #include <stdexcept>
 namespace trading{
 namespace risk {
 
 double RiskMetrics::calculateVaR(const std::vector<double>& returns, double confidence) {
     if (returns.empty()) {
         throw std::invalid_argument("Returns vector cannot be empty");
     }
     
     if (confidence <= 0.0 || confidence >= 1.0) {
         throw std::invalid_argument("Confidence level must be between 0 and 1");
     }
     
     // Create a copy of returns for sorting
     std::vector<double> sortedReturns = returns;
     std::sort(sortedReturns.begin(), sortedReturns.end());
     
     // Find the index at the confidence level
     const size_t idx = static_cast<size_t>(std::floor(sortedReturns.size() * (1.0 - confidence)));
     
     // Return the value at that index (negative because it's a loss)
     return -sortedReturns[idx];
 }
 
 double RiskMetrics::calculateExpectedShortfall(const std::vector<double>& returns, double confidence) {
     if (returns.empty()) {
         throw std::invalid_argument("Returns vector cannot be empty");
     }
     
     if (confidence <= 0.0 || confidence >= 1.0) {
         throw std::invalid_argument("Confidence level must be between 0 and 1");
     }
     
     // Create a copy of returns for sorting
     std::vector<double> sortedReturns = returns;
     std::sort(sortedReturns.begin(), sortedReturns.end());
     
     // Find the index at the confidence level
     const size_t idx = static_cast<size_t>(std::floor(sortedReturns.size() * (1.0 - confidence)));
     
     // Calculate average of returns below VaR
     double sum = 0.0;
     for (size_t i = 0; i < idx; ++i) {
         sum += sortedReturns[i];
     }
     
     // Return the expected shortfall (negative because it's a loss)
     return -sum / static_cast<double>(idx);
 }
 
 double RiskMetrics::calculateVolatility(const std::vector<double>& prices, bool annualize) {
     if (prices.size() < 2) {
         throw std::invalid_argument("Prices vector must have at least 2 elements");
     }
     
     // Convert prices to returns
     std::vector<double> returns = pricesToReturns(prices);
     
     // Calculate mean
     double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
     
     // Calculate sum of squared deviations
     double sumSquaredDeviations = 0.0;
     for (const double r : returns) {
         sumSquaredDeviations += std::pow(r - mean, 2);
     }
     
     // Calculate standard deviation
     double stdDev = std::sqrt(sumSquaredDeviations / (returns.size() - 1));
     
     // Annualize if requested (assuming daily data with 252 trading days)
     if (annualize) {
         stdDev *= std::sqrt(252.0);
     }
     
     return stdDev;
 }
 
 double RiskMetrics::calculateSharpeRatio(const std::vector<double>& returns, double riskFreeRate) {
     if (returns.empty()) {
         throw std::invalid_argument("Returns vector cannot be empty");
     }
     
     // Calculate mean return
     double meanReturn = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
     
     // Calculate volatility
     double sumSquaredDeviations = 0.0;
     for (const double r : returns) {
         sumSquaredDeviations += std::pow(r - meanReturn, 2);
     }
     double stdDev = std::sqrt(sumSquaredDeviations / (returns.size() - 1));
     
     // Avoid division by zero
     if (stdDev < 1e-8) {
         return 0.0;
     }
     
     // Calculate and return Sharpe ratio
     return (meanReturn - riskFreeRate) / stdDev;
 }
 
 double RiskMetrics::calculateMaxDrawdown(const std::vector<double>& prices) {
     if (prices.empty()) {
         throw std::invalid_argument("Prices vector cannot be empty");
     }
     
     double maxDrawdown = 0.0;
     double peak = prices[0];
     
     for (const double price : prices) {
         if (price > peak) {
             peak = price;
         } else {
             double drawdown = (peak - price) / peak;
             maxDrawdown = std::max(maxDrawdown, drawdown);
         }
     }
     
     return maxDrawdown;
 }
 
 double RiskMetrics::calculateCorrelation(const std::vector<double>& series1, const std::vector<double>& series2) {
     if (series1.size() != series2.size()) {
         throw std::invalid_argument("Series must have the same length");
     }
     
     if (series1.empty()) {
         throw std::invalid_argument("Series cannot be empty");
     }
     
     // Calculate means
     double mean1 = std::accumulate(series1.begin(), series1.end(), 0.0) / series1.size();
     double mean2 = std::accumulate(series2.begin(), series2.end(), 0.0) / series2.size();
     
     // Calculate covariance and variances
     double covariance = 0.0;
     double variance1 = 0.0;
     double variance2 = 0.0;
     
     for (size_t i = 0; i < series1.size(); ++i) {
         double dev1 = series1[i] - mean1;
         double dev2 = series2[i] - mean2;
         covariance += dev1 * dev2;
         variance1 += dev1 * dev1;
         variance2 += dev2 * dev2;
     }
     
     // Avoid division by zero
     if (variance1 < 1e-8 || variance2 < 1e-8) {
         return 0.0;
     }
     
     // Calculate and return correlation
     return covariance / std::sqrt(variance1 * variance2);
 }
 
 std::map<std::string, std::map<std::string, double>> RiskMetrics::calculateCorrelationMatrix(
     const std::map<std::string, std::vector<double>>& seriesMap) {
     
     if (seriesMap.empty()) {
         throw std::invalid_argument("Series map cannot be empty");
     }
     
     std::map<std::string, std::map<std::string, double>> correlationMatrix;
     
     for (const auto& [name1, series1] : seriesMap) {
         for (const auto& [name2, series2] : seriesMap) {
             // Skip if already calculated (since correlation(a,b) = correlation(b,a))
             if (correlationMatrix.count(name2) && correlationMatrix[name2].count(name1)) {
                 correlationMatrix[name1][name2] = correlationMatrix[name2][name1];
                 continue;
             }
             
             // Check if series have the same length
             if (series1.size() != series2.size()) {
                 throw std::invalid_argument("All series must have the same length");
             }
             
             // Calculate correlation
             correlationMatrix[name1][name2] = calculateCorrelation(series1, series2);
         }
     }
     
     return correlationMatrix;
 }
 
 double RiskMetrics::calculateBeta(const std::vector<double>& returns, const std::vector<double>& benchmarkReturns) {
     if (returns.size() != benchmarkReturns.size()) {
         throw std::invalid_argument("Return series must have the same length");
     }
     
     if (returns.empty()) {
         throw std::invalid_argument("Return series cannot be empty");
     }
     
     // Calculate correlation
     double correlation = calculateCorrelation(returns, benchmarkReturns);
     
     // Calculate standard deviations
     double stdDev = 0.0;
     double benchmarkStdDev = 0.0;
     
     double meanReturn = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
     double meanBenchmarkReturn = std::accumulate(benchmarkReturns.begin(), benchmarkReturns.end(), 0.0) / benchmarkReturns.size();
     
     for (size_t i = 0; i < returns.size(); ++i) {
         stdDev += std::pow(returns[i] - meanReturn, 2);
         benchmarkStdDev += std::pow(benchmarkReturns[i] - meanBenchmarkReturn, 2);
     }
     
     stdDev = std::sqrt(stdDev / (returns.size() - 1));
     benchmarkStdDev = std::sqrt(benchmarkStdDev / (benchmarkReturns.size() - 1));
     
     // Avoid division by zero
     if (benchmarkStdDev < 1e-8) {
         return 0.0;
     }
     
     // Calculate and return beta
     return correlation * (stdDev / benchmarkStdDev);
 }
 
 std::vector<double> RiskMetrics::calculateEMA(const std::vector<double>& data, double alpha) {
     if (data.empty()) {
         throw std::invalid_argument("Data vector cannot be empty");
     }
     
     if (alpha <= 0.0 || alpha >= 1.0) {
         throw std::invalid_argument("Alpha must be between 0 and 1");
     }
     
     std::vector<double> ema(data.size());
     ema[0] = data[0];
     
     for (size_t i = 1; i < data.size(); ++i) {
         ema[i] = alpha * data[i] + (1.0 - alpha) * ema[i-1];
     }
     
     return ema;
 }
 
 std::vector<double> RiskMetrics::calculateSMA(const std::vector<double>& data, int period) {
     if (data.empty()) {
         throw std::invalid_argument("Data vector cannot be empty");
     }
     
     if (period <= 0) {
         throw std::invalid_argument("Period must be positive");
     }
     
     if (static_cast<size_t>(period) > data.size()) {
         throw std::invalid_argument("Period cannot be larger than data size");
     }
     
     std::vector<double> sma(data.size());
     
     // First period-1 elements will be partially calculated
     double sum = 0.0;
     for (int i = 0; i < static_cast<int>(data.size()); ++i) {
         sum += data[i];
         
         if (i < period - 1) {
             sma[i] = sum / (i + 1);
         } else {
             // For i >= period - 1, we can calculate full SMA
             if (i >= period) {
                 sum -= data[i - period];
             }
             sma[i] = sum / period;
         }
     }
     
     return sma;
 }
 
 std::vector<double> RiskMetrics::rollingFunction(const std::vector<double>& data, int windowSize,
                                              const std::function<double(const std::vector<double>&)>& func) {
     if (data.empty()) {
         throw std::invalid_argument("Data vector cannot be empty");
     }
     
     if (windowSize <= 0) {
         throw std::invalid_argument("Window size must be positive");
     }
     
     std::vector<double> result(data.size());
     
     for (size_t i = 0; i < data.size(); ++i) {
         size_t startIdx = (i >= static_cast<size_t>(windowSize)) ? i - windowSize + 1 : 0;
         std::vector<double> window(data.begin() + startIdx, data.begin() + i + 1);
         result[i] = func(window);
     }
     
     return result;
 }
 
 std::vector<double> RiskMetrics::returnsToPrices(const std::vector<double>& returns, double initialPrice) {
     if (returns.empty()) {
         throw std::invalid_argument("Returns vector cannot be empty");
     }
     
     if (initialPrice <= 0.0) {
         throw std::invalid_argument("Initial price must be positive");
     }
     
     std::vector<double> prices(returns.size() + 1);
     prices[0] = initialPrice;
     
     for (size_t i = 0; i < returns.size(); ++i) {
         prices[i + 1] = prices[i] * (1.0 + returns[i]);
     }
     
     return prices;
 }
 
 std::vector<double> RiskMetrics::pricesToReturns(const std::vector<double>& prices, bool logReturns) {
     if (prices.size() < 2) {
         throw std::invalid_argument("Prices vector must have at least 2 elements");
     }
     
     std::vector<double> returns(prices.size() - 1);
     
     for (size_t i = 1; i < prices.size(); ++i) {
         if (prices[i - 1] <= 0.0) {
             throw std::invalid_argument("Prices must be positive");
         }
         
         if (logReturns) {
             returns[i - 1] = std::log(prices[i] / prices[i - 1]);
         } else {
             returns[i - 1] = (prices[i] / prices[i - 1]) - 1.0;
         }
     }
     
     return returns;
 }
} // namespace risk
 } // namespace trading

