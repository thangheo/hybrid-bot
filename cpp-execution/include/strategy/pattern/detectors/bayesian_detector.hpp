#pragma once

#include "imbalance_detector_interface.hpp"
#include "perception/bayesian_trading_model.hpp"
#include "strategy/types/imbalance_type.hpp"
#include "strategy/core_analysis/regime_subscriber.hpp"
#include <map>
#include <memory>
#include <vector>

namespace trading::strategy::detectors {

using namespace trading::strategy;

/**
 * @class BayesianDetector
 * @brief Advanced Bayesian machine learning detector for imbalance detection
 * 
 * This detector integrates the sophisticated BayesianTradingModel into the
 * modular detection architecture, providing:
 * - Per-regime model specialization 
 * - Online learning capabilities
 * - Uncertainty quantification
 * - Feature engineering for volume, spread, and momentum
 */
class BayesianDetector : public AbstractDetector {
public:
    /**
     * @struct BayesianConfig
     * @brief Configuration parameters for Bayesian detector
     */
    struct BayesianConfig {
        // Model hyperparameters
        double initialMu = 0.0;
        double initialSigma = 1.0;
        double beta = 0.081;  // Learning rate
        double minProbability = 0.65;  // Trading threshold
        double basePositionSize = 0.1;
        double uncertaintyPenalty = 0.5;
        
        // Regime-specific settings
        bool enablePerRegimeModels = true;
        int maxRegimes = 4;
        double regimeConfidenceThreshold = 0.2;
        
        // Feature engineering
        bool enableVolumeFeatures = true;
        bool enableSpreadFeatures = true;
        bool enableMomentumFeatures = true;
        int lookbackWindow = 10;
        
        // Online learning
        bool enableOnlineLearning = true;
        int trainingWarmupSamples = 100;
        double feedbackDecayRate = 0.95;
        
        // Model path (optional for pre-trained models)
        std::string modelPath = "";
    };

private:
    BayesianConfig config_;
    
    // Per-symbol, per-regime model management
    using RegimeModelMap = std::map<std::pair<std::string, int>, 
                                   std::shared_ptr<trading::perception::BayesianTradingModel>>;
    RegimeModelMap regimeModels_;
    
    // Feature extraction state
    std::map<std::string, std::vector<double>> volumeHistory_;
    std::map<std::string, std::vector<double>> spreadHistory_;
    std::map<std::string, std::vector<double>> momentumHistory_;
    
    // Regime tracking
    std::shared_ptr<trading::strategy::core_analysis::RegimeSubscriber> regimeSubscriber_;
    std::map<std::string, int> currentRegimes_;
    
    // Online learning state
    std::map<std::string, int> sampleCounts_;
    std::map<std::string, double> feedbackBuffer_;
    
public:
    explicit BayesianDetector(const BayesianConfig& config);
    virtual ~BayesianDetector() = default;
    
    // IImbalanceDetector interface  
    void updateConfig(const std::map<std::string, double>& params) override;

protected:
    // AbstractDetector requires implementing detectImpl, not analyzeOrderBook
    DetectorAnalysisResult detectImpl(const DetectorContext& context) override;

private:
    /**
     * @brief Get or create model for symbol-regime pair
     */
    std::shared_ptr<trading::perception::BayesianTradingModel> getOrCreateModel(
        const std::string& symbol, int regime);
        
    /**
     * @brief Model persistence methods (matching original system)
     */
    std::string getModelFilePath(const std::string& symbol, int regime) const;
    bool loadModelFromFile(std::shared_ptr<trading::perception::BayesianTradingModel> model,
                          const std::string& filePath);
    bool saveModelToFile(std::shared_ptr<trading::perception::BayesianTradingModel> model,
                        const std::string& filePath);
    
    /**
     * @brief Initialize models directory and copy from backup if needed
     */
    void initializeModelsDirectory();
    
    /**
     * @brief Extract features from order book metrics (matching original buildFeatures)
     */
    std::vector<double> extractFeatures(
        const OrderBookMetrics& metrics, 
        const std::string& symbol);
    
    /**
     * @brief Run basic detectors to get results for feature extraction
     */
    std::vector<DetectorAnalysisResult> runBasicDetectors(const OrderBookMetrics& metrics);
    
    /**
     * @brief Update feature history for symbol
     */
    void updateFeatureHistory(
        const std::string& symbol, 
        const OrderBookMetrics& metrics);
    
    /**
     * @brief Calculate volume-based features
     */
    double calculateVolumeImbalance(const OrderBookMetrics& metrics) const;
    double calculateVolumeFlow(const std::string& symbol) const;
    
    /**
     * @brief Calculate spread-based features  
     */
    double calculateSpreadPressure(const OrderBookMetrics& metrics) const;
    double calculateMidPriceMovement(const std::string& symbol) const;
    
    /**
     * @brief Calculate momentum features
     */
    double calculateMomentumIndicator(const std::string& symbol) const;
    double calculateOrderBookPressure(const OrderBookMetrics& metrics) const;
    
    /**
     * @brief Convert BayesianTradingModel prediction to DetectorAnalysisResult
     */
    DetectorAnalysisResult convertPredictionToResult(
        const trading::perception::BayesianTradingModel::TradingSignal& prediction,
        const std::vector<double>& features) const;
    
    /**
     * @brief Process trading feedback for online learning
     */
    void processTradingFeedback(
        const std::string& symbol,
        int regime,
        double actualReturn,
        const std::vector<double>& features);
    
    /**
     * @brief Initialize regime subscriber if needed
     */
    void initializeRegimeSubscriber();
    
    /**
     * @brief Get current regime for symbol
     */
    int getCurrentRegime(const std::string& symbol) const;
    
    /**
     * @brief Validate model health and performance
     */
    bool isModelHealthy(const std::shared_ptr<trading::perception::BayesianTradingModel>& model) const;
    
    /**
     * @brief Apply uncertainty penalty to confidence
     */
    double applyUncertaintyPenalty(double confidence, double uncertainty) const;
};

} // namespace trading::strategy::detectors