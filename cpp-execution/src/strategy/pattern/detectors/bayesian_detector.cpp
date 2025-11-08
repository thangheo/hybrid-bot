#include "strategy/pattern/detectors/bayesian_detector.hpp"
#include "strategy/core_analysis/regime_subscriber.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
    // Utility function to safely extract numeric values
    double safe(double value, double defaultValue = 0.0) {
        return (std::isfinite(value)) ? value : defaultValue;
    }
}

namespace trading::strategy::detectors {

BayesianDetector::BayesianDetector(const BayesianConfig& config)
    : AbstractDetector("BayesianDetector", ImbalanceType::BAYESIAN_ML, true)
    , config_(config) {
    
    // Initialize models directory and copy from backup if needed
    initializeModelsDirectory();
    
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
        DetectorAnalysisResult result;
        result.hasSignal = false;
        result.confidence = 0.0;
        result.strength = 0.0;
        result.reason = "Invalid context";
        return result;
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
        DetectorAnalysisResult result;
        result.hasSignal = false;
        result.confidence = 0.0;
        result.strength = 0.0;
        result.reason = "Model not available or unhealthy";
        return result;
    }
    
    // Update feature history
    updateFeatureHistory(symbol, metrics);
    
    // Extract features from order book metrics
    auto features = extractFeatures(metrics, symbol);
    if (features.empty()) {
        DetectorAnalysisResult result;
        result.hasSignal = false;
        result.confidence = 0.0;
        result.strength = 0.0;
        result.reason = "Feature extraction failed";
        return result;
    }
    
    // Make prediction using Bayesian model
    auto prediction = model->predict(features);
    
    // Convert prediction to detector result
    return convertPredictionToResult(prediction, features);
}

std::shared_ptr<perception::BayesianTradingModel> BayesianDetector::getOrCreateModel(
    const std::string& symbol, int regime) {
    
    std::pair<std::string, int> key = std::make_pair(symbol, regime);
    
    auto it = regimeModels_.find(key);
    if (it != regimeModels_.end() && it->second) {
        return it->second;
    }
    
    // Create new model with mapped configuration
    trading::perception::BayesianTradingModel::ModelConfig modelConfig;
    modelConfig.initial_sigma = config_.initialSigma;
    modelConfig.beta = config_.beta;  
    modelConfig.min_probability = config_.minProbability;
    modelConfig.enable_log_transform = true;
    modelConfig.log_transform_indices = {2}; // Volume feature (matches original)
    
    auto model = std::make_shared<perception::BayesianTradingModel>(modelConfig);
    // Initialize model with a dummy prediction to trigger initialization
    std::vector<double> dummyFeatures(19, 0.0);
    model->predict(dummyFeatures); // This will trigger initialization internally
    
    // Try to load existing model from file
    std::string modelPath = getModelFilePath(symbol, regime);
    if (loadModelFromFile(model, modelPath)) {
        // Model loaded successfully
    } else {
        // New model created
    }
    
    regimeModels_[key] = model;
    return model;
}

std::vector<double> BayesianDetector::extractFeatures(
    const OrderBookMetrics& metrics, const std::string& symbol) {
    
    // First run detectors to get detector results (matching original approach)
    std::vector<DetectorAnalysisResult> detectorResults = runBasicDetectors(metrics);
    
    std::vector<double> features;
    features.reserve(detectorResults.size() + 16); // Dynamic size: detector results + 16 base features
    
    // PART 1: Detector results as features (exactly as in original)
    for (const auto& result : detectorResults) {
        double v = 0.0;
        if (std::isfinite(result.strength) && result.strength > 0.0) {
            v = result.strength;
        } else if (std::isfinite(result.rawValue)) {
            // Inline strength calculation matching original
            double ratio = result.rawValue;
            double threshold = 1.0;
            double strength;
            if (ratio > threshold) strength = (ratio - threshold) / threshold;
            else if (ratio < 1.0 / threshold) strength = (1.0/threshold - ratio) / (1.0/threshold);
            else strength = 0.0;
            v = std::min(1.0, std::max(0.0, strength));
        }
        features.push_back(v);
    }

    // PART 2: Base features from OrderBookMetrics (exactly as in original)
    double bar = safe(metrics.bidAskVolumeRatio, 1.0);
    {
        double ratio = bar; 
        double threshold = 1.0; 
        double strength;
        if (ratio > threshold) strength = (ratio - threshold) / threshold;
        else if (ratio < 1.0/threshold) strength = (1.0/threshold - ratio) / (1.0/threshold);
        else strength = 0.0;
        features.push_back(std::min(1.0, std::max(0.0, strength)));
    }
    
    features.push_back(safe(metrics.priceVelocity, 0.0));
    features.push_back(std::tanh(safe(metrics.volumeVelocity, 0.0) / 1000.0));
    features.push_back(std::tanh(safe(metrics.spreadVelocity, 0.0) * 10.0));

    features.push_back(std::clamp(safe(metrics.liquidityPressure, 0.0), -10.0, 10.0));
    features.push_back(std::clamp(safe(metrics.volatilityExpectation, 0.0), 0.0, 10.0));
    features.push_back(std::clamp(safe(metrics.realizedVolatility, 0.0), 0.0, 100.0));
    features.push_back(std::clamp(safe(metrics.volatilityPerUnitTime, 0.0), 0.0, 100.0));

    double totalBid = safe(metrics.totalBidVolume, 0.0);
    double totalAsk = safe(metrics.totalAskVolume, 0.0);
    {
        double ratio = (totalBid + 1.0) / (totalAsk + 1.0);
        double threshold = 1.0; 
        double strength;
        if (ratio > threshold) strength = (ratio - threshold) / threshold;
        else if (ratio < 1.0/threshold) strength = (1.0/threshold - ratio) / (1.0/threshold);
        else strength = 0.0;
        features.push_back(std::min(1.0, std::max(0.0, strength)));
    }

    double topBidSum = 0.0;
    double topAskSum = 0.0;
    for (size_t i = 0; i < std::min<size_t>(3, metrics.bidVolumesByLevel.size()); ++i) {
        topBidSum += safe(metrics.bidVolumesByLevel[i], 0.0);
    }
    for (size_t i = 0; i < std::min<size_t>(3, metrics.askVolumesByLevel.size()); ++i) {
        topAskSum += safe(metrics.askVolumesByLevel[i], 0.0);
    }
    {
        double ratio = (topBidSum + 1.0) / (topAskSum + 1.0);
        double threshold = 1.0; 
        double strength;
        if (ratio > threshold) strength = (ratio - threshold) / threshold;
        else if (ratio < 1.0/threshold) strength = (1.0/threshold - ratio) / (1.0/threshold);
        else strength = 0.0;
        features.push_back(std::min(1.0, std::max(0.0, strength)));
    }

    return features;
}

std::vector<DetectorAnalysisResult> BayesianDetector::runBasicDetectors(const OrderBookMetrics& metrics) {
    std::vector<DetectorAnalysisResult> results;
    results.reserve(8); // Exact count from monolithic system
    
    // EXACT MATCH to monolithic system detector calls:
    // results.push_back(detectLargeOrderAppearance(m));
    // results.push_back(detectLargeOrderDisappearance(m));
    // results.push_back(detectOrderFlowImbalance(m));
    // results.push_back(detectPriceLevelConcentration(m));
    // results.push_back(detectLiquidityImbalance(m));
    // results.push_back(detectTrendContinuation(m));
    // results.push_back(detectTrendReversal(m));
    // results.push_back(detectTradeImbalance(m));
    
    std::string symbol = metrics.symbol.toString();
    
    // 1. Large Order Appearance Detection
    DetectorAnalysisResult largeOrderAppear;
    largeOrderAppear.hasSignal = false;
    double largeOrderThreshold = std::max(metrics.bidPrice, metrics.askPrice) * 1.5;
    if (metrics.bidPrice > largeOrderThreshold || metrics.askPrice > largeOrderThreshold) {
        largeOrderAppear.hasSignal = true;
        largeOrderAppear.strength = std::max(metrics.bidPrice, metrics.askPrice) / largeOrderThreshold;
    } else {
        largeOrderAppear.strength = std::max(metrics.bidPrice, metrics.askPrice) / std::max(largeOrderThreshold, 1.0);
    }
    largeOrderAppear.rawValue = largeOrderAppear.strength;
    largeOrderAppear.type = ImbalanceType::LARGE_ORDER_APPEARANCE;
    results.push_back(largeOrderAppear);
    
    // 2. Large Order Disappearance Detection
    DetectorAnalysisResult largeOrderDisappear;
    largeOrderDisappear.hasSignal = false;
    static std::map<std::string, double> prevTotalVolume;
    double currentTotal = metrics.totalBidVolume + metrics.totalAskVolume;
    double volumeChangeRatio = 0.0;
    if (prevTotalVolume.count(symbol) && prevTotalVolume[symbol] > 0) {
        volumeChangeRatio = (prevTotalVolume[symbol] - currentTotal) / prevTotalVolume[symbol];
        if (volumeChangeRatio > 0.15) largeOrderDisappear.hasSignal = true;
    }
    largeOrderDisappear.strength = std::max(0.0, volumeChangeRatio);
    largeOrderDisappear.rawValue = largeOrderDisappear.strength;
    largeOrderDisappear.type = ImbalanceType::LARGE_ORDER_DISAPPEARANCE;
    results.push_back(largeOrderDisappear);
    prevTotalVolume[symbol] = currentTotal;
    
    // 3. Order Flow Imbalance Detection
    DetectorAnalysisResult orderFlow;
    orderFlow.hasSignal = false;
    double totalVolume = metrics.totalBidVolume + metrics.totalAskVolume;
    double flowImbalance = 0.0;
    if (totalVolume > 0) {
        flowImbalance = (metrics.totalBidVolume - metrics.totalAskVolume) / totalVolume;
        if (std::abs(flowImbalance) > 0.2) orderFlow.hasSignal = true;
    }
    orderFlow.strength = std::abs(flowImbalance);
    orderFlow.rawValue = flowImbalance;
    orderFlow.type = ImbalanceType::ORDER_FLOW_IMBALANCE;
    results.push_back(orderFlow);
    
    // 4. Price Level Concentration Detection
    DetectorAnalysisResult levelConcentration;
    levelConcentration.hasSignal = false;
    double concentration = 0.0;
    if (!metrics.bidVolumesByLevel.empty() && !metrics.askVolumesByLevel.empty() && 
        metrics.totalBidVolume > 0 && metrics.totalAskVolume > 0) {
        double topBidRatio = metrics.bidVolumesByLevel[0] / metrics.totalBidVolume;
        double topAskRatio = metrics.askVolumesByLevel[0] / metrics.totalAskVolume;
        concentration = std::max(topBidRatio, topAskRatio);
        if (concentration > 0.4) levelConcentration.hasSignal = true;
    }
    levelConcentration.strength = concentration;
    levelConcentration.rawValue = concentration;
    levelConcentration.type = ImbalanceType::PRICE_LEVEL_CONCENTRATION;
    results.push_back(levelConcentration);
    
    // 5. Liquidity Imbalance Detection
    DetectorAnalysisResult liquidityImbalance;
    liquidityImbalance.hasSignal = false;
    double liquidityRatio = 0.0;
    if (metrics.totalBidVolume + metrics.totalAskVolume > 0) {
        liquidityRatio = (metrics.totalBidVolume - metrics.totalAskVolume) / (metrics.totalBidVolume + metrics.totalAskVolume);
        if (std::abs(liquidityRatio) > 0.3) liquidityImbalance.hasSignal = true;
    }
    liquidityImbalance.strength = std::abs(liquidityRatio);
    liquidityImbalance.rawValue = liquidityRatio;
    liquidityImbalance.type = ImbalanceType::LIQUIDITY_IMBALANCE;
    results.push_back(liquidityImbalance);
    
    // 6. Trend Continuation Detection
    DetectorAnalysisResult trendContinuation;
    trendContinuation.hasSignal = false;
    static std::map<std::string, std::deque<double>> priceHistory;
    priceHistory[symbol].push_back(metrics.midPrice);
    if (priceHistory[symbol].size() > 5) priceHistory[symbol].pop_front();
    
    double trendStrength = 0.0;
    if (priceHistory[symbol].size() >= 3) {
        auto& prices = priceHistory[symbol];
        double recentChange = (prices.back() - prices[0]) / prices[0];
        trendStrength = std::abs(recentChange);
        if (trendStrength > 0.002) trendContinuation.hasSignal = true; // 0.2% move
    }
    trendContinuation.strength = trendStrength;
    trendContinuation.rawValue = trendStrength;
    trendContinuation.type = ImbalanceType::TREND_CONTINUATION;
    results.push_back(trendContinuation);
    
    // 7. Trend Reversal Detection
    DetectorAnalysisResult trendReversal;
    trendReversal.hasSignal = false;
    double reversalStrength = 0.0;
    if (priceHistory[symbol].size() >= 4) {
        auto& prices = priceHistory[symbol];
        double oldSlope = prices[1] - prices[0];
        double newSlope = prices.back() - prices[prices.size()-2];
        if (oldSlope * newSlope < 0) { // Opposite directions
            reversalStrength = std::abs(newSlope / std::max(std::abs(oldSlope), 1e-8));
            if (reversalStrength > 0.5) trendReversal.hasSignal = true;
        }
    }
    trendReversal.strength = std::min(1.0, reversalStrength);
    trendReversal.rawValue = reversalStrength;
    trendReversal.type = ImbalanceType::TREND_REVERSAL;
    results.push_back(trendReversal);
    
    // 8. Trade Imbalance Detection 
    DetectorAnalysisResult tradeImbalance;
    tradeImbalance.hasSignal = false;
    double tradeImbalanceRatio = 0.0;
    // Use volume velocity as proxy for trade flow imbalance
    if (std::abs(metrics.volumeVelocity) > 100.0) {
        tradeImbalanceRatio = std::tanh(metrics.volumeVelocity / 1000.0); // Normalize
        if (std::abs(tradeImbalanceRatio) > 0.1) tradeImbalance.hasSignal = true;
    }
    tradeImbalance.strength = std::abs(tradeImbalanceRatio);
    tradeImbalance.rawValue = tradeImbalanceRatio;
    tradeImbalance.type = ImbalanceType::TRADE_IMBALANCE;
    results.push_back(tradeImbalance);
    
    // Liquidity pressure detector
    DetectorAnalysisResult liquidityResult;
    liquidityResult.hasSignal = false;
    liquidityResult.strength = std::abs(metrics.liquidityPressure);
    liquidityResult.rawValue = metrics.liquidityPressure;
    liquidityResult.type = ImbalanceType::LIQUIDITY_IMBALANCE;
    results.push_back(liquidityResult);
    
    return results;
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
    const std::string& symbol,
    int regime,
    double actualReturn,
    const std::vector<double>& features) {
    
    // For now, just placeholder - could implement online learning updates
    // This would update the model with actual trading outcomes
}

std::string BayesianDetector::getModelFilePath(const std::string& symbol, int regime) const {
    // Match original system: models/bayesian/{EXCHANGE}_{SYMBOL}_regime{N}.json
    // Extract exchange and symbol from full symbol (e.g., "BINANCE_BTC_USDT")
    return "models/bayesian/" + symbol + "_regime" + std::to_string(regime) + ".json";
}

bool BayesianDetector::loadModelFromFile(
    std::shared_ptr<perception::BayesianTradingModel> model,
    const std::string& filePath) {
    
    // Try to load model from JSON file
    try {
        // First try the specified path
        std::ifstream file(filePath);
        if (file.good()) {
            file.close();
            return model->load_model(filePath);
        }
        
        // If not found, try backup directory
        std::filesystem::path path(filePath);
        std::string backupPath = "models/bayesian_backup_20251107_202044/" + path.filename().string();
        
        std::ifstream backupFile(backupPath);
        if (backupFile.good()) {
            backupFile.close();
            LOG_INFO("Loading Bayesian model from backup: {}", backupPath);
            return model->load_model(backupPath);
        }
        
        return false; // File doesn't exist in either location
    } catch (const std::exception& e) {
        // Try backup directory as fallback
        std::filesystem::path path(filePath);
        std::string backupPath = "models/bayesian_backup_20251107_202044/" + path.filename().string();
        try {
            LOG_INFO("Fallback: trying to load from backup: {}", backupPath);
            return model->load_model(backupPath);
        } catch (...) {
            return false;
        }
    }
}

bool BayesianDetector::saveModelToFile(
    std::shared_ptr<perception::BayesianTradingModel> model,
    const std::string& filePath) {
    
    // Save model to JSON file
    try {
        // Ensure directory exists
        std::filesystem::path path(filePath);
        std::filesystem::create_directories(path.parent_path());
        
        bool success = model->save_model(filePath);
        if (success) {
            LOG_INFO("Saved Bayesian model to: {}", filePath);
        }
        return success;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save Bayesian model to {}: {}", filePath, e.what());
        return false;
    }
}

void BayesianDetector::initializeModelsDirectory() {
    try {
        // Ensure models/bayesian directory exists
        std::filesystem::create_directories("models/bayesian");
        
        // Check if backup directory exists and copy models if needed
        std::string backupDir = "models/bayesian_backup_20251107_202044";
        if (std::filesystem::exists(backupDir) && std::filesystem::is_directory(backupDir)) {
            
            // Copy all JSON files from backup to active directory
            for (const auto& entry : std::filesystem::directory_iterator(backupDir)) {
                if (entry.path().extension() == ".json") {
                    std::string filename = entry.path().filename();
                    std::string destPath = "models/bayesian/" + filename;
                    
                    // Only copy if destination doesn't exist
                    if (!std::filesystem::exists(destPath)) {
                        try {
                            std::filesystem::copy_file(entry.path(), destPath);
                            LOG_INFO("Copied Bayesian model: {} -> {}", entry.path().string(), destPath);
                        } catch (const std::exception& e) {
                            LOG_WARN("Failed to copy model {}: {}", entry.path().string(), e.what());
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize models directory: {}", e.what());
    }
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