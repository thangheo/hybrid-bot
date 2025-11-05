#pragma once

#include <Eigen/Dense>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <jsoncpp/json/json.h>
#include <core/direction.hpp>
#include <strategy/types/trading_signal.hpp>
using namespace trading::core;
/**
 * BayesianTradingModel - Online Bayesian Learning for Trading Signals
 * 
 * Features:
 * - Multi-feature Bayesian regression with uncertainty quantification
 * - Online feature normalization (running mean/std)
 * - Flexible trading decisions: threshold-based or expected value
 * - Position sizing based on prediction confidence
 * - Thread-safe operations
 * - Model persistence (save/load)
 */
namespace trading {
namespace perception {

class BayesianTradingModel {
public:
    static constexpr int NUM_CLASSES = 3; // 0: BUY, 1: SELL, 2: NEUTRAL (matches Direction enum)
    struct TradingSignal {
        double probability;      // P(price_up) ∈ [0,1]
        double expected_value;   // Expected value of trade
        double position_size;    // Recommended position size [0,1]
        double uncertainty;      // Model uncertainty (trace of covariance)
        bool should_trade;       // Final trading decision
        core::Direction direction;          // BUY, SELL, or NEUTRAL
    };

    struct ModelConfig {
        double initial_sigma = 1.0;        // Initial weight uncertainty
    double beta = 0.081;                 // Bayesian update strength (learning rate)
        double min_probability = 0.65;      // Threshold for trading decision
        double base_position_size = 0.1;   // Base position size
        double uncertainty_penalty = 0.5;  // Reduce size when uncertain
        bool use_expected_value = true;    // Use EV instead of threshold
        double payoff_long = 1.0;          // Expected payoff for long position
        double payoff_short = 1.0;         // Expected payoff for short position
        double forgetting_factor = 0.95;    // Discount factor for prior precision (1.0 = no forgetting)
        double process_noise = 0.0001;        // Additive diagonal process noise per update (helps prevent overconfidence)
        
        // Feature preprocessing options
        bool enable_log_transform = true;     // Enable log transformation for skewed features
        std::vector<int> log_transform_indices = {2}; // Indices of features to apply log transform
        double log_transform_threshold = 0.0; // Only apply log transform if feature value > threshold
    };

private:
    // Model parameters (one-vs-rest per class)
    std::vector<Eigen::VectorXd> mu_;              // Mean weights per class (NUM_CLASSES x num_features)
    std::vector<Eigen::VectorXd> sigma_diag_;      // Diagonal covariance per class (variance vector)
    std::vector<Eigen::VectorXd> sigma_inv_diag_;  // Inverse diagonal covariance per class
    
    // Feature normalization
    Eigen::VectorXd feature_mean_;    // Running mean
    Eigen::VectorXd feature_std_;     // Running std
    Eigen::VectorXd feature_m2_;      // For Welford's online variance
    
    // Configuration and state
    ModelConfig config_;
    int num_features_;
    int num_updates_;
    bool is_initialized_;
    
    // Thread safety
    mutable std::mutex model_mutex_;
    //min_confidence_threshold_
    double min_confidence_threshold_ = 0.55;  // Require 55% confidence
    double neutral_veto_threshold_ = 0.40;    // Veto if >40% uncertain
    // Helper methods
    void initialize_model(int num_features);
    void update_feature_stats(const Eigen::VectorXd& features);
    Eigen::VectorXd normalize_features(const Eigen::VectorXd& features) const;
    void update_bayesian_weights(const Eigen::VectorXd& normalized_features, int label);
    double sigmoid(double x) const;
    void update_sigma_inverse();
    std::tuple<double, double, double> predict_multiclass(const std::vector<double>& features);
    double calculatePositionSize(double primary_prob, double neutral_prob) const {
        double confidence_gap = std::max(0.0, primary_prob - neutral_prob);
        return std::clamp(confidence_gap / (1.0 - neutral_prob + 1e-10), 0.0, 1.0);
    }
        
    double calculateEntropy(double p1, double p2, double p3) const {
        auto safe_log = [](double p) { 
            return p > 1e-10 ? std::log(p) : 0.0; 
        };
        return -(p1 * safe_log(p1) + p2 * safe_log(p2) + p3 * safe_log(p3));
    }
    
    TradingSignal createNeutralSignal() const {
        return {0.0, 0.0, 0.0, 1.0, false, Direction::NEUTRAL};
    }
public:
    /**
     * Constructor
     */
    BayesianTradingModel();
    explicit BayesianTradingModel(const ModelConfig& config);
    
    /**
     * Destructor
     */
    ~BayesianTradingModel() = default;
    
    /**
     * Predict trading signal from raw features
     * @param features Raw feature vector (will be normalized internally)
     * @return Complete trading signal with probability, EV, position size
     */
    // TradingSignal predict(const std::vector<double>& features);
    TradingSignal predict(const std::vector<double>& features) ;
    

    /**
     * Update model with new observation
     * @param features Raw feature vector
     * @param label 0 = SELL (price down), 1 = NEUTRAL, 2 = BUY (price up)
     */
    void update(const std::vector<double>& features, int label);
    
    /**
     * Get current model weights and uncertainty
     * @return pair of (mean_weights, diagonal_uncertainty)
     */
    std::pair<std::vector<double>, std::vector<double>> get_weights() const;
    
    /**
     * Get model statistics
     */
    struct ModelStats {
        int num_features;
        int num_updates;
        double total_uncertainty;
        std::vector<double> feature_means;
        std::vector<double> feature_stds;
        std::vector<double> weight_means;
        std::vector<double> weight_uncertainties;
    };
    ModelStats get_stats() const;
    
    /**
     * Configuration management
     */
    void set_config(const ModelConfig& config);
    ModelConfig get_config() const;
    
    /**
     * Model persistence
     */
    bool save_model(const std::string& filepath) const;
    bool load_model(const std::string& filepath);
    
    /**
     * Reset model to initial state
     */
    void reset();
    
    /**
     * Check if model is ready for predictions
     */
    bool is_ready() const;
    
    /**
     * Print model summary
     */
    void print_summary() const;
};

/**
 * Utility functions for model evaluation
 */
namespace BayesianTradingUtils {
    /**
     * Calculate Sharpe ratio from prediction history
     */
    double calculate_sharpe_ratio(const std::vector<double>& returns);
    
    /**
     * Calculate prediction accuracy
     */
    double calculate_accuracy(const std::vector<int>& predictions, 
                             const std::vector<int>& actual);
    
    /**
     * Calculate information ratio
     */
    double calculate_information_ratio(const std::vector<double>& returns,
                                     const std::vector<double>& benchmark);
}


} // namespace perception
} // namespace trading