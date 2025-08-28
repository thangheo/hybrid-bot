/**
 * @file strategy_metrics.cpp
 * @brief Implementation of common metrics calculations
 * 
 * @author Trading System Team
 * @date May 6, 2025
 */

 #include "strategy/core_analysis/strategy_metrics.hpp"
 #include <algorithm>
 #include <numeric>
 #include <cmath>
 #include <random>
 #include <queue>
 
 namespace trading {
 namespace strategy {
 
 StrategyMetrics::StrategyMetrics() {
     // Default constructor
 }
 
 StrategyMetrics::StrategyMetrics(const ParameterSet& params) {
     // Constructor with parameters (unused for now)
     (void)params; // Avoid unused parameter warning
 }
 
 std::vector<double> StrategyMetrics::calculateEMA(
     const std::vector<double>& values, 
     double alpha) {
     
     if (values.empty()) {
         return {};
     }
     
     std::vector<double> ema_values;
     ema_values.reserve(values.size());
     
     // First value is the same as input
     ema_values.push_back(values[0]);
     
     // Calculate EMA for remaining values
     for (size_t i = 1; i < values.size(); ++i) {
         double ema = alpha * values[i] + (1.0 - alpha) * ema_values[i - 1];
         ema_values.push_back(ema);
     }
     
     return ema_values;
 }
 
 double StrategyMetrics::updateEMA(
     double current_ema, 
     double new_value, 
     double alpha) {
     
     return alpha * new_value + (1.0 - alpha) * current_ema;
 }
 
 std::vector<double> StrategyMetrics::calculateSMA(
     const std::vector<double>& values, 
     int window) {
     
     if (values.empty() || window <= 0 || window > static_cast<int>(values.size())) {
         return {};
     }
     
     std::vector<double> sma_values;
     sma_values.reserve(values.size() - window + 1);
     
     // Calculate initial sum
     double sum = 0.0;
     for (int i = 0; i < window; ++i) {
         sum += values[i];
     }
     
     // First SMA value
     sma_values.push_back(sum / window);
     
     // Calculate remaining SMA values using sliding window
     for (size_t i = window; i < values.size(); ++i) {
         sum = sum - values[i - window] + values[i];
         sma_values.push_back(sum / window);
     }
     
     return sma_values;
 }
 
 std::vector<std::array<double, 3>> StrategyMetrics::calculateBollingerBands(
     const std::vector<double>& values, 
     int window, 
     double num_std_dev) {
     
     if (values.empty() || window <= 0 || window > static_cast<int>(values.size())) {
         return {};
     }
     
     // Calculate SMA
     auto sma = calculateSMA(values, window);
     
     // Calculate standard deviations for each window
     std::vector<std::array<double, 3>> bollinger_bands;
     bollinger_bands.reserve(sma.size());
     
     for (size_t i = 0; i < sma.size(); ++i) {
         // Calculate standard deviation for this window
         double variance = 0.0;
         for (int j = 0; j < window; ++j) {
             double diff = values[i + j] - sma[i];
             variance += diff * diff;
         }
         variance /= window;
         double std_dev = std::sqrt(variance);
         
         // Calculate bands
         double middle_band = sma[i];
         double upper_band = middle_band + num_std_dev * std_dev;
         double lower_band = middle_band - num_std_dev * std_dev;
         
         bollinger_bands.push_back({middle_band, upper_band, lower_band});
     }
     
     return bollinger_bands;
 }
 
 std::vector<double> StrategyMetrics::calculateRSI(
     const std::vector<double>& values, 
     int window) {
     
     if (values.size() < 2 || window <= 0 || window >= static_cast<int>(values.size())) {
         return {};
     }
     
     // Calculate price changes
     std::vector<double> changes;
     changes.reserve(values.size() - 1);
     
     for (size_t i = 1; i < values.size(); ++i) {
         changes.push_back(values[i] - values[i - 1]);
     }
     
     // Calculate initial average gain and loss
     double avg_gain = 0.0;
     double avg_loss = 0.0;
     
     for (int i = 0; i < window; ++i) {
         if (changes[i] > 0) {
             avg_gain += changes[i];
         } else {
             avg_loss += -changes[i];
         }
     }
     
     avg_gain /= window;
     avg_loss /= window;
     
     // Calculate initial RSI
     std::vector<double> rsi_values;
     rsi_values.reserve(changes.size() - window + 1);
     
     double rs = avg_loss > 0 ? avg_gain / avg_loss : 100.0;
     rsi_values.push_back(100.0 - (100.0 / (1.0 + rs)));
     
     // Calculate remaining RSI values
     for (size_t i = window; i < changes.size(); ++i) {
         // Update average gain and loss using smoothing
         double current_gain = changes[i] > 0 ? changes[i] : 0.0;
         double current_loss = changes[i] < 0 ? -changes[i] : 0.0;
         
         avg_gain = ((window - 1) * avg_gain + current_gain) / window;
         avg_loss = ((window - 1) * avg_loss + current_loss) / window;
         
         // Calculate RSI
         rs = avg_loss > 0 ? avg_gain / avg_loss : 100.0;
         rsi_values.push_back(100.0 - (100.0 / (1.0 + rs)));
     }
     
     return rsi_values;
 }
 
 std::vector<std::array<double, 3>> StrategyMetrics::calculateMACD(
     const std::vector<double>& values, 
     int fast_period, 
     int slow_period, 
     int signal_period) {
     
     if (values.empty() || fast_period <= 0 || slow_period <= 0 || signal_period <= 0 ||
         fast_period >= slow_period || slow_period >= static_cast<int>(values.size())) {
         return {};
     }
     
     // Calculate fast and slow EMAs
     double fast_alpha = 2.0 / (fast_period + 1.0);
     double slow_alpha = 2.0 / (slow_period + 1.0);
     
     auto fast_ema = calculateEMA(values, fast_alpha);
     auto slow_ema = calculateEMA(values, slow_alpha);
     
     // Calculate MACD line
     std::vector<double> macd_line;
     macd_line.reserve(values.size());
     
     // MACD line starts at the slow_period point
     for (size_t i = slow_period - 1; i < values.size(); ++i) {
         macd_line.push_back(fast_ema[i] - slow_ema[i]);
     }
     
     // Calculate signal line
     double signal_alpha = 2.0 / (signal_period + 1.0);
     auto signal_line = calculateEMA(macd_line, signal_alpha);
     
     // Calculate histogram
     std::vector<std::array<double, 3>> macd_values;
     macd_values.reserve(signal_line.size());
     
     for (size_t i = 0; i < signal_line.size(); ++i) {
         double histogram = macd_line[i + signal_period - 1] - signal_line[i];
         macd_values.push_back({
             macd_line[i + signal_period - 1], 
             signal_line[i], 
             histogram
         });
     }
     
     return macd_values;
 }
 
 double StrategyMetrics::calculateStdDev(const std::vector<double>& values) {
     if (values.empty()) {
         return 0.0;
     }
     
     double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
     double variance = 0.0;
     
     for (double value : values) {
         double diff = value - mean;
         variance += diff * diff;
     }
     
     variance /= values.size();
     return std::sqrt(variance);
 }
 
 double StrategyMetrics::calculateCorrelation(
     const std::vector<double>& x, 
     const std::vector<double>& y) {
     
     if (x.empty() || y.empty() || x.size() != y.size()) {
         return 0.0;
     }
     
     double mean_x = std::accumulate(x.begin(), x.end(), 0.0) / x.size();
     double mean_y = std::accumulate(y.begin(), y.end(), 0.0) / y.size();
     
     double numerator = 0.0;
     double denom_x = 0.0;
     double denom_y = 0.0;
     
     for (size_t i = 0; i < x.size(); ++i) {
         double diff_x = x[i] - mean_x;
         double diff_y = y[i] - mean_y;
         numerator += diff_x * diff_y;
         denom_x += diff_x * diff_x;
         denom_y += diff_y * diff_y;
     }
     
     if (denom_x <= 0.0 || denom_y <= 0.0) {
         return 0.0;
     }
     
     return numerator / std::sqrt(denom_x * denom_y);
 }
 
 double StrategyMetrics::calculateZScore(
     double value, 
     double mean, 
     double std_dev) {
     
     if (std_dev <= 0.0) {
         return 0.0;
     }
     
     return (value - mean) / std_dev;
 }
 
 std::vector<double> StrategyMetrics::normalizeValues(
     const std::vector<double>& values) {
     
     if (values.empty()) {
         return {};
     }
     
     double min_value = *std::min_element(values.begin(), values.end());
     double max_value = *std::max_element(values.begin(), values.end());
     double range = max_value - min_value;
     
     std::vector<double> normalized_values;
     normalized_values.reserve(values.size());
     
     if (range > 0.0) {
         for (double value : values) {
             normalized_values.push_back((value - min_value) / range);
         }
     } else {
         // All values are the same
         normalized_values.resize(values.size(), 0.5);
     }
     
     return normalized_values;
 }
 
 std::vector<double> StrategyMetrics::calculateEWMA(
     const std::vector<double>& values, 
     double com) {
     
     if (values.empty() || com <= 0.0) {
         return {};
     }
     
     // Calculate alpha from center of mass
     double alpha = 1.0 / (1.0 + com);
     
     return calculateEMA(values, alpha);
 }
 
 double StrategyMetrics::calculateSignalStrength(
     const std::map<std::string, double>& metrics,
     const std::map<std::string, double>& weights) {
     
     if (metrics.empty() || weights.empty()) {
         return 0.0;
     }
     
     double total_weight = 0.0;
     double weighted_sum = 0.0;
     
     for (const auto& [name, value] : metrics) {
         auto weight_it = weights.find(name);
         if (weight_it != weights.end()) {
             weighted_sum += value * weight_it->second;
             total_weight += weight_it->second;
         }
     }
     
     if (total_weight > 0.0) {
         return weighted_sum / total_weight;
     }
     
     return 0.0;
 }
 
 PerformanceMetrics StrategyMetrics::calculatePerformanceMetrics(
     const std::vector<double>& pnl_series,
     const std::vector<double>& benchmark_series,
     double risk_free_rate) {
     
     PerformanceMetrics metrics;
     
     if (pnl_series.empty()) {
         return metrics;
     }
     
     // Calculate returns from P&L series
     std::vector<double> returns;
     returns.reserve(pnl_series.size() - 1);
     
     for (size_t i = 1; i < pnl_series.size(); ++i) {
         if (pnl_series[i-1] != 0.0) {
             returns.push_back(pnl_series[i] / pnl_series[i-1] - 1.0);
         } else {
             returns.push_back(0.0);
         }
     }
     
     // Calculate basic metrics
     metrics.totalReturn = (pnl_series.back() / pnl_series.front()) - 1.0;
     metrics.maxDrawdown = calculateMaxDrawdown(pnl_series);
     metrics.sharpeRatio = calculateSharpeRatio(returns, risk_free_rate);
     metrics.sortino_ratio = calculateSortinoRatio(returns, risk_free_rate);
     
     // Calculate trades metrics if provided
     if (!pnl_series.empty()) {
         metrics.winRate = calculateWinRate(returns);
         metrics.profitFactor = calculateProfitFactor(returns);
     }
     
     // Calculate benchmark metrics if provided
     if (!benchmark_series.empty() && benchmark_series.size() == pnl_series.size()) {
         std::vector<double> benchmark_returns;
         benchmark_returns.reserve(benchmark_series.size() - 1);
         
         for (size_t i = 1; i < benchmark_series.size(); ++i) {
             if (benchmark_series[i-1] != 0.0) {
                 benchmark_returns.push_back(benchmark_series[i] / benchmark_series[i-1] - 1.0);
             } else {
                 benchmark_returns.push_back(0.0);
             }
         }
         
         metrics.benchmark_return = (benchmark_series.back() / benchmark_series.front()) - 1.0;
         metrics.alpha = metrics.totalReturn - metrics.benchmark_return;
         metrics.beta = calculateCorrelation(returns, benchmark_returns);
         
         // Information ratio
         std::vector<double> excess_returns;
         excess_returns.reserve(returns.size());
         
         for (size_t i = 0; i < returns.size(); ++i) {
             excess_returns.push_back(returns[i] - benchmark_returns[i]);
         }
         
         double excess_return_mean = std::accumulate(excess_returns.begin(), excess_returns.end(), 0.0) / excess_returns.size();
         double tracking_error = calculateStdDev(excess_returns);
         
         if (tracking_error > 0.0) {
             metrics.information_ratio = excess_return_mean / tracking_error;
         }
     }
     
     return metrics;
 }
 
 double StrategyMetrics::calculateMaxDrawdown(const std::vector<double>& equity_curve) {
     if (equity_curve.empty()) {
         return 0.0;
     }
     
     double maxDrawdown = 0.0;
     double peak = equity_curve[0];
     
     for (size_t i = 1; i < equity_curve.size(); ++i) {
         if (equity_curve[i] > peak) {
             peak = equity_curve[i];
         } else {
             double drawdown = (peak - equity_curve[i]) / peak;
             maxDrawdown = std::max(maxDrawdown, drawdown);
         }
     }
     
     return maxDrawdown;
 }
 
 double StrategyMetrics::calculateSharpeRatio(
     const std::vector<double>& returns,
     double risk_free_rate) {
     
     if (returns.empty()) {
         return 0.0;
     }
     
     double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
     double excess_return = mean_return - risk_free_rate;
     double std_dev = calculateStdDev(returns);
     
     if (std_dev <= 0.0) {
         return 0.0;
     }
     
     return excess_return / std_dev;
 }
 
 double StrategyMetrics::calculateSortinoRatio(
     const std::vector<double>& returns,
     double risk_free_rate) {
     
     if (returns.empty()) {
         return 0.0;
     }
     
     double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
     double excess_return = mean_return - risk_free_rate;
     
     // Calculate downside deviation
     double sum_squared_downside = 0.0;
     int count_downside = 0;
     
     for (double r : returns) {
         if (r < risk_free_rate) {
             double downside = risk_free_rate - r;
             sum_squared_downside += downside * downside;
             count_downside++;
         }
     }
     
     double downside_deviation = 0.0;
     if (count_downside > 0) {
         downside_deviation = std::sqrt(sum_squared_downside / count_downside);
     }
     
     if (downside_deviation <= 0.0) {
         return 0.0;
     }
     
     return excess_return / downside_deviation;
 }
 
 double StrategyMetrics::calculateWinRate(const std::vector<double>& trades) {
     if (trades.empty()) {
         return 0.0;
     }
     
     int winning_trades = 0;
     for (double trade : trades) {
         if (trade > 0.0) {
             winning_trades++;
         }
     }
     
     return 100.0 * static_cast<double>(winning_trades) / trades.size();
 }
 
 double StrategyMetrics::calculateProfitFactor(const std::vector<double>& trades) {
     if (trades.empty()) {
         return 0.0;
     }
     
     double gross_profit = 0.0;
     double gross_loss = 0.0;
     
     for (double trade : trades) {
         if (trade > 0.0) {
             gross_profit += trade;
         } else {
             gross_loss -= trade;
         }
     }
     
     if (gross_loss <= 0.0) {
         return gross_profit > 0.0 ? 100.0 : 1.0;
     }
     
     return gross_profit / gross_loss;
 }
 
 double StrategyMetrics::calculateExpectedShortfall(
     const std::vector<double>& returns,
     double confidence_level) {
     
     if (returns.empty()) {
         return 0.0;
     }
     
     // Sort returns in ascending order
     std::vector<double> sorted_returns = returns;
     std::sort(sorted_returns.begin(), sorted_returns.end());
     
     // Calculate VaR
     size_t var_index = static_cast<size_t>(returns.size() * (1.0 - confidence_level));
     var_index = std::min(var_index, returns.size() - 1);
     
     // Calculate expected shortfall as the average of returns below VaR
     double es_sum = 0.0;
     int es_count = 0;
     
     for (size_t i = 0; i <= var_index; ++i) {
         es_sum += sorted_returns[i];
         es_count++;
     }
     
     if (es_count == 0) {
         return 0.0;
     }
     
     return -es_sum / es_count;
 }
 
 double StrategyMetrics::calculateValueAtRisk(
     const std::vector<double>& returns,
     double confidence_level,
     const std::string& method) {
     
     if (returns.empty()) {
         return 0.0;
     }
     
     if (method == "historical") {
         // Sort returns
         std::vector<double> sorted_returns = returns;
         std::sort(sorted_returns.begin(), sorted_returns.end());
         
         // Find VaR index
         size_t var_index = static_cast<size_t>(returns.size() * (1.0 - confidence_level));
         var_index = std::min(var_index, returns.size() - 1);
         
         // Return the value at that index (negative to represent loss)
         return -sorted_returns[var_index];
     } 
     else if (method == "parametric") {
         // Calculate mean and standard deviation
         double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
         double std_dev = calculateStdDev(returns);
         
         // Calculate z-score for the confidence level
         double z_score = 0.0;
         
         if (confidence_level == 0.95) {
             z_score = 1.645;
         } else if (confidence_level == 0.99) {
             z_score = 2.326;
         } else {
             // Simplified approximation of inverse normal CDF
             // Using Abramowitz and Stegun approximation for inverse error function
             double p = 2.0 * confidence_level - 1.0;
             if (p >= 0) {
                 double t = std::sqrt(-2.0 * std::log((1.0 - p) / 2.0));
                 z_score = t - (2.30753 + 0.27061 * t) / (1.0 + 0.99229 * t + 0.04481 * t * t);
             } else {
                 double t = std::sqrt(-2.0 * std::log((1.0 + p) / 2.0));
                 z_score = -t + (2.30753 + 0.27061 * t) / (1.0 + 0.99229 * t + 0.04481 * t * t);
             }
         }
         
         // Calculate parametric VaR
         return -(mean - z_score * std_dev);
     }
     else if (method == "monte_carlo") {
         // Calculate mean and standard deviation
         double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
         double std_dev = calculateStdDev(returns);
         
         // Generate random samples
         const int num_samples = 10000;
         std::vector<double> samples;
         samples.reserve(num_samples);
         
         std::random_device rd;
         std::mt19937 gen(rd());
         std::normal_distribution<double> dist(mean, std_dev);
         
         for (int i = 0; i < num_samples; ++i) {
             samples.push_back(dist(gen));
         }
         
         // Sort samples and find VaR
         std::sort(samples.begin(), samples.end());
         size_t var_index = static_cast<size_t>(num_samples * (1.0 - confidence_level));
         var_index = std::min(var_index, samples.size() - 1);
         
         return -samples[var_index];
     }
     
     // Default to historical method
     return calculateValueAtRisk(returns, confidence_level, "historical");
 }
 
 double StrategyMetrics::calculateJarqueBera(const std::vector<double>& values) {
     if (values.size() < 3) {
         return 0.0;
     }
     
     double skewness = calculateSkewness(values);
     double kurtosis = calculateKurtosis(values);
     
     return values.size() * (skewness * skewness / 6.0 + std::pow(kurtosis - 3.0, 2.0) / 24.0);
 }
 
 double StrategyMetrics::calculateSkewness(const std::vector<double>& values) {
     if (values.size() < 3) {
         return 0.0;
     }
     
     double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
     
     double sum_cubed_diff = 0.0;
     double sum_squared_diff = 0.0;
     
     for (double value : values) {
         double diff = value - mean;
         sum_cubed_diff += diff * diff * diff;
         sum_squared_diff += diff * diff;
     }
     
     double variance = sum_squared_diff / values.size();
     
     if (variance <= 0.0) {
         return 0.0;
     }
     
     double std_dev = std::sqrt(variance);
     return sum_cubed_diff / (values.size() * std::pow(std_dev, 3.0));
 }
 
 double StrategyMetrics::calculateKurtosis(const std::vector<double>& values) {
     if (values.size() < 4) {
         return 3.0; // Normal distribution kurtosis
     }
     
     double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
     
     double sum_quartic_diff = 0.0;
     double sum_squared_diff = 0.0;
     
     for (double value : values) {
         double diff = value - mean;
         sum_quartic_diff += diff * diff * diff * diff;
         sum_squared_diff += diff * diff;
     }
     
     double variance = sum_squared_diff / values.size();
     
     if (variance <= 0.0) {
         return 3.0;
     }
     
     return sum_quartic_diff / (values.size() * variance * variance);
 }
 
 bool StrategyMetrics::hasTrend(
     const std::vector<double>& values, 
     double threshold) {
     
     if (values.size() < 3) {
         return false;
     }
     
     // Simple linear regression to calculate slope
     double sum_x = 0.0;
     double sum_y = 0.0;
     double sum_xy = 0.0;
     double sum_xx = 0.0;
     
     for (size_t i = 0; i < values.size(); ++i) {
         sum_x += i;
         sum_y += values[i];
         sum_xy += i * values[i];
         sum_xx += i * i;
     }
     
     double n = static_cast<double>(values.size());
     double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
     
     // Calculate standard error of slope
     double intercept = (sum_y - slope * sum_x) / n;
     double sum_squared_errors = 0.0;
     
     for (size_t i = 0; i < values.size(); ++i) {
         double predicted = intercept + slope * i;
         double error = values[i] - predicted;
         sum_squared_errors += error * error;
     }
     
     double standard_error = std::sqrt(sum_squared_errors / (n - 2.0));
     standard_error /= std::sqrt(n * sum_xx - sum_x * sum_x);
     
     // Calculate t-statistic
     double t_stat = std::abs(slope / standard_error);
     
     // Compare t-statistic with threshold
     return t_stat > threshold;
 }
 
 double StrategyMetrics::calculateHurstExponent(const std::vector<double>& values) {
     if (values.size() < 10) {
         return 0.5; // Default for random walk
     }
     
     // Calculate log returns
     std::vector<double> returns;
     returns.reserve(values.size() - 1);
     
     for (size_t i = 1; i < values.size(); ++i) {
         returns.push_back(std::log(values[i] / values[i-1]));
     }
     
     // Simplified R/S analysis
     const int min_size = 10;
     const int max_size = static_cast<int>(returns.size()) / 4;
     const int num_sizes = 5;
     
     std::vector<double> sizes_log;
     std::vector<double> rs_log;
     
     for (int size_idx = 0; size_idx < num_sizes; ++size_idx) {
         int size = min_size * (1 << size_idx);
         if (size > max_size) break;
         
         sizes_log.push_back(std::log(static_cast<double>(size)));
         
         // Calculate R/S for this size
         int num_segments = static_cast<int>(returns.size()) / size;
         double rs_avg = 0.0;
         
         for (int seg = 0; seg < num_segments; ++seg) {
             std::vector<double> segment(
                 returns.begin() + seg * size,
                 returns.begin() + (seg + 1) * size);
             
             double mean = std::accumulate(segment.begin(), segment.end(), 0.0) / size;
             
             // Calculate cumulative deviation
             std::vector<double> deviation(size, 0.0);
             deviation[0] = segment[0] - mean;
             
             for (int i = 1; i < size; ++i) {
                 deviation[i] = deviation[i-1] + (segment[i] - mean);
             }
             
             // Calculate range
             double r = *std::max_element(deviation.begin(), deviation.end()) - 
                       *std::min_element(deviation.begin(), deviation.end());
             
             // Calculate standard deviation
             double s = calculateStdDev(segment);
             
             if (s > 0.0) {
                 rs_avg += r / s;
             }
         }
         
         rs_avg /= num_segments;
         rs_log.push_back(std::log(rs_avg));
     }
     
     // Linear regression to find Hurst exponent
     if (sizes_log.size() < 2) {
         return 0.5;
     }
     
     double sum_x = 0.0;
     double sum_y = 0.0;
     double sum_xy = 0.0;
     double sum_xx = 0.0;
     
     for (size_t i = 0; i < sizes_log.size(); ++i) {
         sum_x += sizes_log[i];
         sum_y += rs_log[i];
         sum_xy += sizes_log[i] * rs_log[i];
         sum_xx += sizes_log[i] * sizes_log[i];
     }
     
     double n = static_cast<double>(sizes_log.size());
     double hurst = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
     
     return hurst;
 }
 
 std::vector<size_t> StrategyMetrics::detectChangePoints(
     const std::vector<double>& values,
     const std::string& method) {
     
     if (values.size() < 10) {
         return {};
     }
     
     if (method == "cusum") {
         return detectChangePointsCusum(values);
     } else if (method == "pelt") {
         // PELT is more complex and not implemented here
         return detectChangePointsCusum(values);
     } else if (method == "binseg") {
         // Binary segmentation is simplified here
         return detectChangePointsBinarySegmentation(values);
     }
     
     // Default to CUSUM
     return detectChangePointsCusum(values);
 }
 
 // Helper functions for change point detection
 std::vector<size_t> StrategyMetrics::detectChangePointsCusum(
     const std::vector<double>& values) {
     
     if (values.size() < 10) {
         return {};
     }
     
     std::vector<size_t> change_points;
     
     // Calculate mean
     double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
     
     // Calculate standard deviation
     double variance = 0.0;
     for (double value : values) {
         double diff = value - mean;
         variance += diff * diff;
     }
     variance /= values.size();
     double std_dev = std::sqrt(variance);
     
     if (std_dev <= 0.0) {
         return change_points;
     }
     
     // CUSUM detection
     double threshold = 1.0 * std_dev;
     double cusum_pos = 0.0;
     double cusum_neg = 0.0;
     
     for (size_t i = 0; i < values.size(); ++i) {
         double normalized = (values[i] - mean) / std_dev;
         
         cusum_pos = std::max(0.0, cusum_pos + normalized);
         cusum_neg = std::max(0.0, cusum_neg - normalized);
         
         if (cusum_pos > threshold || cusum_neg > threshold) {
             change_points.push_back(i);
             
             // Reset CUSUM
             cusum_pos = 0.0;
             cusum_neg = 0.0;
             
             // Recalculate mean for next segment
             if (i < values.size() - 1) {
                 mean = std::accumulate(values.begin() + i + 1, values.end(), 0.0) / 
                        static_cast<double>(values.size() - i - 1);
             }
         }
     }
     
     return change_points;
 }
 
 std::vector<size_t> StrategyMetrics::detectChangePointsBinarySegmentation(
     const std::vector<double>& values) {
     
     if (values.size() < 10) {
         return {};
     }
     
     std::vector<size_t> change_points;
     
     // Simplified binary segmentation
     // For simplicity, we just find the point that minimizes the sum of squared errors
     std::function<void(size_t, size_t, int)> find_split;
     find_split = [&](size_t start, size_t end, int depth) {
         if (end - start < 10 || depth > 3) {
             return;
         }
         
         double best_error = std::numeric_limits<double>::max();
         size_t best_split = 0;
         
         for (size_t split = start + 5; split < end - 5; ++split) {
             // Calculate means of the two segments
             double mean1 = 0.0;
             for (size_t i = start; i < split; ++i) {
                 mean1 += values[i];
             }
             mean1 /= (split - start);
             
             double mean2 = 0.0;
             for (size_t i = split; i < end; ++i) {
                 mean2 += values[i];
             }
             mean2 /= (end - split);
             
             // Calculate sum of squared errors
             double error = 0.0;
             for (size_t i = start; i < split; ++i) {
                 double diff = values[i] - mean1;
                 error += diff * diff;
             }
             for (size_t i = split; i < end; ++i) {
                 double diff = values[i] - mean2;
                 error += diff * diff;
             }
             
             if (error < best_error) {
                 best_error = error;
                 best_split = split;
             }
         }
         
         // Calculate error for no split
         double mean = 0.0;
         for (size_t i = start; i < end; ++i) {
             mean += values[i];
         }
         mean /= (end - start);
         
         double unsplit_error = 0.0;
         for (size_t i = start; i < end; ++i) {
             double diff = values[i] - mean;
             unsplit_error += diff * diff;
         }
         
         // If splitting improves error significantly, add the change point
         if (best_error < 0.8 * unsplit_error) {
             change_points.push_back(best_split);
             
             // Recursively find more change points
             find_split(start, best_split, depth + 1);
             find_split(best_split, end, depth + 1);
         }
     };
     
     find_split(0, values.size(), 0);
     
     // Sort change points
     std::sort(change_points.begin(), change_points.end());
     
     return change_points;
 }
 
 } // namespace strategy
 } // namespace trading