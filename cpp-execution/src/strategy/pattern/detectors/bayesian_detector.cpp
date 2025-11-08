#include "strategy/pattern/detectors/bayesian_detector.hpp"
#include "common/logger.hpp"
#include <cmath>
#include <algorithm>

namespace trading::strategy::detectors {

BayesianDetector::BayesianDetector(const BayesianConfig& config)
    : AbstractDetector("BayesianDetector", ImbalanceType::BAYESIAN_ML, true)
    , config_(config) {
    
    // Initialize regime subscriber if using per-regime models
    if (config_.enablePerRegimeModels) {
        regimeSubscriber_ = std::make_shared<trading::strategy::core_analysis::RegimeSubscriber>();
    }
    
    LOG_INFO("BayesianDetector initialized with {} regime models, online learning: {}", 
             config_.maxRegimes, config_.enableOnlineLearning);
}

void BayesianDetector::updateConfig(const std::map<std::string, double>& params) {
    // Update configuration parameters from the provided map
    for (const auto& [key, value] : params) {
        if (key == "minProbability") {
            config_.minProbability = value;
        } else if (key == "beta") {
            config_.beta = value;
        } else if (key == "uncertaintyPenalty") {
            config_.uncertaintyPenalty = value;
        } else if (key == "lookbackWindow") {
            config_.lookbackWindow = static_cast<int>(value);
        } else if (key == "trainingWarmupSamples") {
            config_.trainingWarmupSamples = static_cast<int>(value);
        }
        // Add more parameter mappings as needed
    }
    
    LOG_DEBUG("BayesianDetector configuration updated with {} parameters", params.size());
}

DetectorAnalysisResult BayesianDetector::detectImpl(const DetectorContext& context) {
    if (!context.isValid() || !context.metrics) {
        return DetectorAnalysisResult{false, 0.0, 0.0};
    }
    
    const auto& metrics = *context.metrics;
    
    // Extract symbol from context (if available through metrics or other means)
    // For now, use a default symbol - this should be passed through context
    std::string symbol = "DEFAULT"; // TODO: Extract from context
    
    // Get current regime
    int regime = getCurrentRegime(symbol);
    
    // Get or create model for this symbol-regime pair
    auto model = getOrCreateModel(symbol, regime);
    if (!model || !isModelHealthy(model)) {
        return DetectorAnalysisResult{false, 0.0, 0.0};
    }
    
    // Update feature history
    updateFeatureHistory(symbol, metrics);
    
    // Extract features from order book metrics
    auto features = extractFeatures(metrics, symbol);
    if (features.empty()) {
        return DetectorAnalysisResult{false, 0.0, 0.0};
    }
    
    // Make prediction using Bayesian model
    auto prediction = model->predict(features);
    
    // Convert prediction to detector result
    return convertPredictionToResult(prediction, features);
}

std::shared_ptr<trading::perception::BayesianTradingModel> 
BayesianDetector::getOrCreateModel(const std::string& symbol, int regime) {
    auto key = std::make_pair(symbol, regime);
    
    auto it = regimeModels_.find(key);
    if (it != regimeModels_.end()) {
        return it->second;
    }
    
    // Create new model with configuration
    trading::perception::BayesianTradingModel::ModelConfig modelConfig;
    modelConfig.initial_sigma = config_.initialSigma;
    modelConfig.beta = config_.beta;
    modelConfig.min_probability = config_.minProbability;
    modelConfig.base_position_size = config_.basePositionSize;
    modelConfig.uncertainty_penalty = config_.uncertaintyPenalty;
    
    auto model = std::make_shared<trading::perception::BayesianTradingModel>(modelConfig);
    
    regimeModels_[key] = model;
    
    LOG_DEBUG("Created new Bayesian model for symbol {} regime {}", symbol, regime);
    return model;
}

std::vector<double> BayesianDetector::extractFeatures(
    const OrderBookMetrics& metrics, const std::string& symbol) {
    
    std::vector<double> features;
    features.reserve(10); // Estimate feature count
    
    // Volume features
    if (config_.enableVolumeFeatures) {
        features.push_back(calculateVolumeImbalance(metrics));
        features.push_back(calculateVolumeFlow(symbol));
    }
    
    // Spread features
    if (config_.enableSpreadFeatures) {
        features.push_back(calculateSpreadPressure(metrics));
        features.push_back(calculateMidPriceMovement(symbol));
    }
    
    // Momentum features
    if (config_.enableMomentumFeatures) {
        features.push_back(calculateMomentumIndicator(symbol));
        features.push_back(calculateOrderBookPressure(metrics));
    }
    
    return features;
}

void BayesianDetector::updateFeatureHistory(
    const std::string& symbol, const OrderBookMetrics& metrics) {
    
    // Update volume history
    auto& volHistory = volumeHistory_[symbol];
    volHistory.push_back(calculateVolumeImbalance(metrics));
    if (volHistory.size() > static_cast<size_t>(config_.lookbackWindow)) {
        volHistory.erase(volHistory.begin());
    }
    
    // Update spread history
    auto& spreadHistory = spreadHistory_[symbol];
    spreadHistory.push_back(calculateSpreadPressure(metrics));
    if (spreadHistory.size() > static_cast<size_t>(config_.lookbackWindow)) {
        spreadHistory.erase(spreadHistory.begin());
    }
    
    // Update momentum history
    auto& momentumHistory = momentumHistory_[symbol];
    momentumHistory.push_back(calculateOrderBookPressure(metrics));
    if (momentumHistory.size() > static_cast<size_t>(config_.lookbackWindow)) {
        momentumHistory.erase(momentumHistory.begin());
    }
}

double BayesianDetector::calculateVolumeImbalance(const OrderBookMetrics& metrics) const {
    if (metrics.totalBidVolume + metrics.totalAskVolume == 0.0) {
        return 0.0;
    }
    return (metrics.totalBidVolume - metrics.totalAskVolume) / 
           (metrics.totalBidVolume + metrics.totalAskVolume);
}

double BayesianDetector::calculateVolumeFlow(const std::string& symbol) const {
    const auto it = volumeHistory_.find(symbol);
    if (it == volumeHistory_.end() || it->second.size() < 2) {
        return 0.0;
    }
    
    const auto& history = it->second;
    return history.back() - history[history.size() - 2];
}

double BayesianDetector::calculateSpreadPressure(const OrderBookMetrics& metrics) const {
    if (metrics.midPrice == 0.0) {
        return 0.0;
    }
    return metrics.spread / metrics.midPrice;
}

double BayesianDetector::calculateMidPriceMovement(const std::string& symbol) const {
    const auto it = spreadHistory_.find(symbol);
    if (it == spreadHistory_.end() || it->second.size() < 2) {
        return 0.0;
    }
    
    const auto& history = it->second;
    return history.back() - history[history.size() - 2];
}

double BayesianDetector::calculateMomentumIndicator(const std::string& symbol) const {
    const auto it = momentumHistory_.find(symbol);
    if (it == momentumHistory_.end() || it->second.size() < config_.lookbackWindow / 2) {
        return 0.0;
    }
    
    const auto& history = it->second;
    size_t mid = history.size() / 2;
    
    double recentAvg = 0.0, olderAvg = 0.0;
    for (size_t i = mid; i < history.size(); ++i) {
        recentAvg += history[i];
    }
    for (size_t i = 0; i < mid; ++i) {
        olderAvg += history[i];
    }
    
    recentAvg /= (history.size() - mid);
    olderAvg /= mid;
    
    return recentAvg - olderAvg;
}

double BayesianDetector::calculateOrderBookPressure(const OrderBookMetrics& metrics) const {
    // Simple implementation using bid/ask ratio
    if (metrics.totalAskVolume == 0.0) {
        return 1.0;
    }
    if (metrics.totalBidVolume == 0.0) {
        return -1.0;
    }
    
    double ratio = metrics.totalBidVolume / metrics.totalAskVolume;
    return (ratio - 1.0) / (ratio + 1.0);
}

DetectorAnalysisResult BayesianDetector::convertPredictionToResult(
    const trading::perception::BayesianTradingModel::TradingSignal& prediction,
    const std::vector<double>& features) const {
    
    DetectorAnalysisResult result;
    
    // Check if prediction meets minimum confidence threshold
    double adjustedConfidence = applyUncertaintyPenalty(
        prediction.probability, prediction.uncertainty);
        
    result.hasSignal = prediction.should_trade && (adjustedConfidence >= config_.minProbability);
    result.confidence = adjustedConfidence;
    result.strength = prediction.position_size;
    result.direction = prediction.direction;
    result.type = ImbalanceType::BAYESIAN_ML;
    result.rawValue = prediction.expected_value;
    result.reason = "Bayesian ML prediction with " + 
                   std::to_string(features.size()) + " features";
    
    return result;
}

void BayesianDetector::processTradingFeedback(
    const std::string& symbol, int regime, double actualReturn,
    const std::vector<double>& features) {
    
    if (!config_.enableOnlineLearning) {
        return;
    }
    
    auto model = getOrCreateModel(symbol, regime);
    if (!model) {
        return;
    }
    
    // Update model with actual outcome
    // Note: This assumes BayesianTradingModel has an update method
    // model->updateWithFeedback(features, actualReturn);
    
    // Update sample count
    sampleCounts_[symbol]++;
    
    // Apply feedback decay
    feedbackBuffer_[symbol] = feedbackBuffer_[symbol] * config_.feedbackDecayRate + 
                             actualReturn * (1.0 - config_.feedbackDecayRate);
}

void BayesianDetector::initializeRegimeSubscriber() {
    if (!regimeSubscriber_) {
        regimeSubscriber_ = std::make_shared<trading::strategy::core_analysis::RegimeSubscriber>();
    }
}

int BayesianDetector::getCurrentRegime(const std::string& symbol) const {
    if (!config_.enablePerRegimeModels || !regimeSubscriber_) {
        return 0; // Default regime
    }
    
    auto it = currentRegimes_.find(symbol);
    return (it != currentRegimes_.end()) ? it->second : 0;
}

bool BayesianDetector::isModelHealthy(
    const std::shared_ptr<trading::perception::BayesianTradingModel>& model) const {
    
    if (!model) {
        return false;
    }
    
    // Check if model has sufficient training samples
    const auto symbolIt = std::find_if(sampleCounts_.begin(), sampleCounts_.end(),
        [this](const auto& pair) {
            return pair.second >= config_.trainingWarmupSamples;
        });
    
    return symbolIt != sampleCounts_.end();
}

double BayesianDetector::applyUncertaintyPenalty(double confidence, double uncertainty) const {
    if (uncertainty <= 0.0) {
        return confidence;
    }
    
    double penalty = config_.uncertaintyPenalty * uncertainty;
    return std::max(0.0, confidence - penalty);
}

} // namespace trading::strategy::detectors