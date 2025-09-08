/**
 * @file logistic_regression_signal_generator.cpp
 * @brief Implementation of logistic regression-based trading signal generator
 * 
 * Created on: May 6, 2025
 */

 #include "strategy/signal/logistic_regression_signal_generator.hpp"
 #include "common/logger.hpp"
 #include <cmath>
 #include <algorithm>
 #include <numeric>
 #include <fstream>
 #include <sstream>
 #include <iomanip>
 
 namespace trading {
 namespace strategy {
 
 LogisticRegressionSignalGenerator::LogisticRegressionSignalGenerator()
     : confidence_threshold_(0.7),
       signal_strength_multiplier_(1.0),
       is_initialized_(false) {
     // Initialize with default values
 }
 
 void LogisticRegressionSignalGenerator::initialize(const ModelParameters& params) {
     // Load model weights and bias from parameters
     weights_ = params.weights;
     bias_ = params.bias;
     
     // Set configuration parameters
     confidence_threshold_ = params.confidence_threshold;
     signal_strength_multiplier_ = params.signal_strength_multiplier;
     
     is_initialized_ = true;
     
     LOG_INFO("LogisticRegressionSignalGenerator initialized with {} features", weights_.size());
 }
 
 void LogisticRegressionSignalGenerator::processFeatures(
     const OrderBookMetrics& metrics, const AdditionalFeatures& additional_features) {
     
     if (!is_initialized_) {
         LOG_WARN("LogisticRegressionSignalGenerator not initialized, skipping feature processing");
         return;
     }
     
     // Extract and normalize features from order book metrics
     std::vector<double> features;
     features.reserve(weights_.size());
     
     // Extract order book features
     features.push_back(metrics.bidAskVolumeRatio);
     features.push_back(metrics.bid_ask_price_gap);
     features.push_back(metrics.mid_price);
     features.push_back(metrics.spread);
     features.push_back(metrics.spreadPercent);
     features.push_back(metrics.volumeImbalance );
     features.push_back(metrics.priceLevelImbalance);
     features.push_back(metrics.order_flow);
     
     // Add additional features if available
     for (const auto& feature : additional_features.values) {
         features.push_back(feature);
     }
     
     // Ensure we have the right number of features
     if (features.size() != weights_.size()) {
         LOG_ERROR("Feature vector size ({}) doesn't match weight vector size ({})",
                  features.size(), weights_.size());
         return;
     }
     
     // Store features for signal generation
     recent_features_.push_back(features);
     
     // Keep only the last N feature vectors to prevent memory bloat
     constexpr size_t MAX_FEATURE_HISTORY = 100;
     if (recent_features_.size() > MAX_FEATURE_HISTORY) {
         recent_features_.erase(recent_features_.begin());
     }
 }
 
 double LogisticRegressionSignalGenerator::predict(const std::vector<double>& features) {
     // Calculate the dot product of features and weights
     double z = std::inner_product(features.begin(), features.end(), weights_.begin(), bias_);
     
     // Apply logistic function to get probability
     double probability = 1.0 / (1.0 + std::exp(-z));
     
     return probability;
 }
 
 std::vector<TradingSignal> LogisticRegressionSignalGenerator::generateSignals() {
     std::vector<TradingSignal> signals;
     
     if (!is_initialized_ || recent_features_.empty()) {
         return signals;
     }
     
     // Use the most recent feature vector
     const auto& features = recent_features_.back();
     
     // Predict probability with logistic regression
     double probability = predict(features);
     
     // Only generate signals if probability is above or below thresholds
     if (probability >= confidence_threshold_) {
         // Generate buy signal
         TradingSignal signal;
         signal.id = generateSignalId();
         signal.direction = Direction::BUY;
         signal.strength = (probability - 0.5) * 2.0 * signal_strength_multiplier_;
         signal.confidence = probability;
         signal.generationTime = std::chrono::system_clock::now();
         signal.ttl = std::chrono::milliseconds(60000); // 1 minute TTL
         signal.sourceType = SignalSourceType::LOGISTIC_REGRESSION;
         
         // Add additional metrics
         signal.signalMetrics["probability"] = probability;
         signal.signalMetrics["threshold"] = confidence_threshold_;
         
         signals.push_back(signal);
         
         LOG_DEBUG("Generated BUY signal with probability {:.4f}, strength {:.4f}",
                  probability, signal.strength);
     }
     else if (probability <= (1.0 - confidence_threshold_)) {
         // Generate sell signal
         TradingSignal signal;
         signal.id = generateSignalId();
         signal.direction = Direction::SELL;
         signal.strength = (0.5 - probability) * 2.0 * signal_strength_multiplier_;
         signal.confidence = 1.0 - probability;
         signal.generationTime = std::chrono::system_clock::now();
         signal.ttl = std::chrono::milliseconds(60000); // 1 minute TTL
         signal.sourceType = SignalSourceType::LOGISTIC_REGRESSION;
         
         // Add additional metrics
         signal.signalMetrics["probability"] = probability;
         signal.signalMetrics["threshold"] = confidence_threshold_;
         
         signals.push_back(signal);
         
         LOG_DEBUG("Generated SELL signal with probability {:.4f}, strength {:.4f}",
                  probability, signal.strength);
     }
     
     // Notify callback if registered
     if (!signals.empty() && callback_) {
         callback_(signals);
     }
     
     return signals;
 }
 
 void LogisticRegressionSignalGenerator::configureSignalParameters(const SignalParameters& params) {
     confidence_threshold_ = params.confidence_threshold;
     signal_strength_multiplier_ = params.signal_strength_multiplier;
     
     LOG_INFO("Updated signal parameters: threshold={}, multiplier={}",
              confidence_threshold_, signal_strength_multiplier_);
 }
 
 void LogisticRegressionSignalGenerator::registerSignalCallback(const SignalCallback& callback) {
     callback_ = callback;
 }
 
 std::string LogisticRegressionSignalGenerator::generateSignalId() {
     // Generate a unique signal ID based on timestamp and random value
     auto now = std::chrono::system_clock::now();
     auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
         now.time_since_epoch()).count();
     
     std::stringstream ss;
     ss << "LR-" << std::hex << std::setw(16) << std::setfill('0') << timestamp;
     
     return ss.str();
 }
 
 } // namespace strategy
 } // namespace trading