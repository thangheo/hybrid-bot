#include "perception/bayesian_trading_model.hpp"
#include <random>
#include <numeric>
// #include <json/json.h>  // For JSON serialization

namespace trading {
    namespace perception {
BayesianTradingModel::BayesianTradingModel()
    : BayesianTradingModel(ModelConfig{}) {
}

BayesianTradingModel::BayesianTradingModel(const ModelConfig& config)
    : config_(config), num_features_(0), num_updates_(0), is_initialized_(false) {
}

void BayesianTradingModel::initialize_model(int num_features) {
    std::lock_guard<std::mutex> lock(model_mutex_);
    
    if (is_initialized_ && num_features_ == num_features) {
        return;  // Already initialized with same dimensions
    }
    
    num_features_ = num_features;
    num_updates_ = 0;
    
    // Initialize Bayesian parameters
    mu_ = Eigen::VectorXd::Zero(num_features);
    sigma_ = config_.initial_sigma * Eigen::MatrixXd::Identity(num_features, num_features);
    update_sigma_inverse();
    
    // Initialize feature normalization
    feature_mean_ = Eigen::VectorXd::Zero(num_features);
    feature_std_ = Eigen::VectorXd::Ones(num_features);
    feature_m2_ = Eigen::VectorXd::Zero(num_features);
    
    is_initialized_ = true;
    
    std::cout << "[BayesianTradingModel] Initialized with " << num_features << " features" << std::endl;
}

void BayesianTradingModel::update_feature_stats(const Eigen::VectorXd& features) {
    // Welford's online algorithm for mean and variance
    num_updates_++;
    
    for (int i = 0; i < num_features_; ++i) {
        double delta = features(i) - feature_mean_(i);
        feature_mean_(i) += delta / num_updates_;
        double delta2 = features(i) - feature_mean_(i);
        feature_m2_(i) += delta * delta2;
        
        if (num_updates_ > 1) {
            feature_std_(i) = std::sqrt(feature_m2_(i) / (num_updates_ - 1));
            // Prevent division by zero
            if (feature_std_(i) < 1e-8) {
                feature_std_(i) = 1.0;
            }
        }
    }
}

Eigen::VectorXd BayesianTradingModel::normalize_features(const Eigen::VectorXd& features) const {
    if (num_updates_ <= 1) {
        return features;  // Not enough data for normalization
    }
    
    Eigen::VectorXd normalized(num_features_);
    for (int i = 0; i < num_features_; ++i) {
        normalized(i) = (features(i) - feature_mean_(i)) / feature_std_(i);
        // Clip outliers to [-3, 3] sigma
        normalized(i) = std::max(-3.0, std::min(3.0, normalized(i)));
    }
    
    return normalized;
}

void BayesianTradingModel::update_bayesian_weights(const Eigen::VectorXd& normalized_features, int label) {
    // Bayesian update with logistic regression
    // Posterior: N(mu_post, Sigma_post)
    // Sigma_post^{-1} = Sigma_prior^{-1} + β * x * x^T
    // mu_post = Sigma_post * (Sigma_prior^{-1} * mu_prior + β * x * y)
    
    Eigen::MatrixXd feature_outer = normalized_features * normalized_features.transpose();
    Eigen::MatrixXd sigma_inv_new = sigma_inv_ + config_.beta * feature_outer;
    
    // Update mean
    Eigen::VectorXd mu_new = sigma_inv_new.ldlt().solve(
        sigma_inv_ * mu_ + config_.beta * normalized_features * label
    );
    
    // Update covariance
    Eigen::MatrixXd sigma_new = sigma_inv_new.inverse();
    
    // Apply updates
    mu_ = mu_new;
    sigma_ = sigma_new;
    sigma_inv_ = sigma_inv_new;
    
    // Regularization to prevent numerical issues
    double trace_sigma = sigma_.trace();
    if (trace_sigma > 100.0) {  // Too much uncertainty
        sigma_ *= 100.0 / trace_sigma;
        update_sigma_inverse();
    }
}

double BayesianTradingModel::sigmoid(double x) const {
    // Numerical stable sigmoid
    if (x > 500) return 1.0;
    if (x < -500) return 0.0;
    return 1.0 / (1.0 + std::exp(-x));
}

void BayesianTradingModel::update_sigma_inverse() {
    sigma_inv_ = sigma_.inverse();
}

BayesianTradingModel::TradingSignal BayesianTradingModel::predict(const std::vector<double>& features) {
    if (features.empty()) {
        throw std::invalid_argument("Features vector cannot be empty");
    }
    
    // Initialize if needed
    if (!is_initialized_) {
        initialize_model(features.size());
    }
    
    if (features.size() != static_cast<size_t>(num_features_)) {
        throw std::invalid_argument("Feature dimension mismatch");
    }
    
    std::lock_guard<std::mutex> lock(model_mutex_);
    
    // Convert to Eigen vector
    Eigen::VectorXd feature_vec = Eigen::Map<const Eigen::VectorXd>(features.data(), features.size());
    
    // Normalize features
    Eigen::VectorXd normalized_features = normalize_features(feature_vec);
    
    // Calculate prediction
    double score = mu_.dot(normalized_features);
    double probability = sigmoid(score);
    
    // Calculate uncertainty (predictive variance)
    double uncertainty = normalized_features.transpose() * sigma_ * normalized_features;
    uncertainty = std::sqrt(uncertainty);
    
    // Calculate expected value
    double expected_value = 0.0;
    if (config_.use_expected_value) {
        expected_value = probability * config_.payoff_long - (1.0 - probability) * config_.payoff_short;
    }
    
    // Calculate position size (incorporate uncertainty)
    double confidence = 1.0 / (1.0 + config_.uncertainty_penalty * uncertainty);
    double position_size = config_.base_position_size * confidence;
    
    if (config_.use_expected_value) {
        position_size *= std::max(0.0, expected_value);  // Scale by positive EV
    } else {
        // Scale by distance from 0.5 (neutral)
        position_size *= 2.0 * std::abs(probability - 0.5);
    }
    
    position_size = std::min(1.0, position_size);  // Cap at 100%
    
    // Trading decision
    bool should_trade = false;
    int direction = 0;
    
    if (config_.use_expected_value) {
        should_trade = (expected_value > 0.01);  // Minimum EV threshold
        direction = (expected_value > 0) ? 1 : -1;
    } else {
        should_trade = (probability > config_.min_probability) || (probability < (1.0 - config_.min_probability));
        direction = (probability > 0.5) ? 1 : -1;
    }
    
    return TradingSignal{
        .probability = probability,
        .expected_value = expected_value,
        .position_size = position_size,
        .uncertainty = uncertainty,
        .should_trade = should_trade,
        .direction = direction
    };
}

void BayesianTradingModel::update(const std::vector<double>& features, int label) {
    if (features.empty()) {
        throw std::invalid_argument("Features vector cannot be empty");
    }
    
    if (label != 0 && label != 1) {
        throw std::invalid_argument("Label must be 0 (price down) or 1 (price up)");
    }
    
    // Initialize if needed
    if (!is_initialized_) {
        initialize_model(features.size());
    }
    
    if (features.size() != static_cast<size_t>(num_features_)) {
        throw std::invalid_argument("Feature dimension mismatch");
    }
    
    std::lock_guard<std::mutex> lock(model_mutex_);
    
    // Convert to Eigen vector
    Eigen::VectorXd feature_vec = Eigen::Map<const Eigen::VectorXd>(features.data(), features.size());
    
    // Update feature statistics
    update_feature_stats(feature_vec);
    
    // Normalize features (using updated statistics)
    Eigen::VectorXd normalized_features = normalize_features(feature_vec);
    
    // Update Bayesian weights
    update_bayesian_weights(normalized_features, label);
}

std::pair<std::vector<double>, std::vector<double>> BayesianTradingModel::get_weights() const {
    std::lock_guard<std::mutex> lock(model_mutex_);
    
    if (!is_initialized_) {
        return {{}, {}};
    }
    
    std::vector<double> means(mu_.data(), mu_.data() + mu_.size());
    
    std::vector<double> uncertainties;
    uncertainties.reserve(num_features_);
    for (int i = 0; i < num_features_; ++i) {
        uncertainties.push_back(std::sqrt(sigma_(i, i)));
    }
    
    return {means, uncertainties};
}

BayesianTradingModel::ModelStats BayesianTradingModel::get_stats() const {
    std::lock_guard<std::mutex> lock(model_mutex_);
    
    ModelStats stats;
    stats.num_features = num_features_;
    stats.num_updates = num_updates_;
    
    if (is_initialized_) {
        stats.total_uncertainty = sigma_.trace();
        
        // Feature statistics
        stats.feature_means.assign(feature_mean_.data(), feature_mean_.data() + feature_mean_.size());
        stats.feature_stds.assign(feature_std_.data(), feature_std_.data() + feature_std_.size());
        
        // Weight statistics
        stats.weight_means.assign(mu_.data(), mu_.data() + mu_.size());
        stats.weight_uncertainties.reserve(num_features_);
        for (int i = 0; i < num_features_; ++i) {
            stats.weight_uncertainties.push_back(std::sqrt(sigma_(i, i)));
        }
    }
    
    return stats;
}

void BayesianTradingModel::set_config(const ModelConfig& config) {
    std::lock_guard<std::mutex> lock(model_mutex_);
    config_ = config;
}

BayesianTradingModel::ModelConfig BayesianTradingModel::get_config() const {
    std::lock_guard<std::mutex> lock(model_mutex_);
    return config_;
}

bool BayesianTradingModel::save_model(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(model_mutex_);
    
    if (!is_initialized_) {
        std::cerr << "Cannot save uninitialized model" << std::endl;
        return false;
    }
    
    try {
        Json::Value root;
        
        // Model configuration
        Json::Value config_json;
        config_json["initial_sigma"] = config_.initial_sigma;
        config_json["beta"] = config_.beta;
        config_json["min_probability"] = config_.min_probability;
        config_json["base_position_size"] = config_.base_position_size;
        config_json["uncertainty_penalty"] = config_.uncertainty_penalty;
        config_json["use_expected_value"] = config_.use_expected_value;
        config_json["payoff_long"] = config_.payoff_long;
        config_json["payoff_short"] = config_.payoff_short;
        root["config"] = config_json;
        
        // Model state
        root["num_features"] = num_features_;
        root["num_updates"] = num_updates_;
        
        // Weights (mean)
        Json::Value mu_json(Json::arrayValue);
        for (int i = 0; i < num_features_; ++i) {
            mu_json.append(mu_(i));
        }
        root["mu"] = mu_json;
        
        // Covariance matrix (flatten)
        Json::Value sigma_json(Json::arrayValue);
        for (int i = 0; i < num_features_; ++i) {
            for (int j = 0; j < num_features_; ++j) {
                sigma_json.append(sigma_(i, j));
            }
        }
        root["sigma"] = sigma_json;
        
        // Feature normalization
        Json::Value feat_mean_json(Json::arrayValue);
        Json::Value feat_std_json(Json::arrayValue);
        Json::Value feat_m2_json(Json::arrayValue);
        
        for (int i = 0; i < num_features_; ++i) {
            feat_mean_json.append(feature_mean_(i));
            feat_std_json.append(feature_std_(i));
            feat_m2_json.append(feature_m2_(i));
        }
        
        root["feature_mean"] = feat_mean_json;
        root["feature_std"] = feat_std_json;
        root["feature_m2"] = feat_m2_json;
        
        // Write to file
        std::ofstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Cannot open file for writing: " << filepath << std::endl;
            return false;
        }
        
        file << root;
        file.close();
        
        std::cout << "[BayesianTradingModel] Model saved to: " << filepath << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error saving model: " << e.what() << std::endl;
        return false;
    }
}

bool BayesianTradingModel::load_model(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Cannot open file for reading: " << filepath << std::endl;
            return false;
        }
        
        Json::Value root;
        file >> root;
        file.close();
        
        std::lock_guard<std::mutex> lock(model_mutex_);
        
        // Load configuration
        const Json::Value& config_json = root["config"];
        config_.initial_sigma = config_json.get("initial_sigma", 1.0).asDouble();
        config_.beta = config_json.get("beta", 1.0).asDouble();
        config_.min_probability = config_json.get("min_probability", 0.6).asDouble();
        config_.base_position_size = config_json.get("base_position_size", 0.1).asDouble();
        config_.uncertainty_penalty = config_json.get("uncertainty_penalty", 0.5).asDouble();
        config_.use_expected_value = config_json.get("use_expected_value", true).asBool();
        config_.payoff_long = config_json.get("payoff_long", 1.0).asDouble();
        config_.payoff_short = config_json.get("payoff_short", 1.0).asDouble();
        
        // Load model state
        num_features_ = root["num_features"].asInt();
        num_updates_ = root["num_updates"].asInt();
        
        // Load weights
        const Json::Value& mu_json = root["mu"];
        mu_ = Eigen::VectorXd(num_features_);
        for (int i = 0; i < num_features_; ++i) {
            mu_(i) = mu_json[i].asDouble();
        }
        
        // Load covariance matrix
        const Json::Value& sigma_json = root["sigma"];
        sigma_ = Eigen::MatrixXd(num_features_, num_features_);
        for (int i = 0; i < num_features_; ++i) {
            for (int j = 0; j < num_features_; ++j) {
                sigma_(i, j) = sigma_json[i * num_features_ + j].asDouble();
            }
        }
        update_sigma_inverse();
        
        // Load feature normalization
        const Json::Value& feat_mean_json = root["feature_mean"];
        const Json::Value& feat_std_json = root["feature_std"];
        const Json::Value& feat_m2_json = root["feature_m2"];
        
        feature_mean_ = Eigen::VectorXd(num_features_);
        feature_std_ = Eigen::VectorXd(num_features_);
        feature_m2_ = Eigen::VectorXd(num_features_);
        
        for (int i = 0; i < num_features_; ++i) {
            feature_mean_(i) = feat_mean_json[i].asDouble();
            feature_std_(i) = feat_std_json[i].asDouble();
            feature_m2_(i) = feat_m2_json[i].asDouble();
        }
        
        is_initialized_ = true;
        
        std::cout << "[BayesianTradingModel] Model loaded from: " << filepath << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading model: " << e.what() << std::endl;
        return false;
    }
}

void BayesianTradingModel::reset() {
    std::lock_guard<std::mutex> lock(model_mutex_);
    is_initialized_ = false;
    num_features_ = 0;
    num_updates_ = 0;
}

bool BayesianTradingModel::is_ready() const {
    std::lock_guard<std::mutex> lock(model_mutex_);
    return is_initialized_ && num_updates_ > 10;  // Need some training data
}

void BayesianTradingModel::print_summary() const {
    // std::lock_guard<std::mutex> lock(model_mutex_);
    
    if (!is_initialized_) {
        std::cout << "[BayesianTradingModel] Model not initialized" << std::endl;
        return;
    }
    
    std::cout << "\n=== Bayesian Trading Model Summary ===" << std::endl;
    std::cout << "Features: " << num_features_ << std::endl;
    std::cout << "Updates: " << num_updates_ << std::endl;
    std::cout << "Total Uncertainty: " << sigma_.trace() << std::endl;
    std::cout << "Ready for trading: " << (is_ready() ? "Yes" : "No") << std::endl;
    
    std::cout << "\nWeight Summary:" << std::endl;
    for (int i = 0; i < num_features_; ++i) {
        std::cout << "  Feature " << i << ": " 
                  << mu_(i) << " ± " << std::sqrt(sigma_(i, i)) << std::endl;
    }
    
    std::cout << "\nConfig:" << std::endl;
    std::cout << "  Use Expected Value: " << config_.use_expected_value << std::endl;
    std::cout << "  Min Probability: " << config_.min_probability << std::endl;
    std::cout << "  Base Position Size: " << config_.base_position_size << std::endl;
    std::cout << "  Beta: " << config_.beta << std::endl;
    std::cout << "==============================\n" << std::endl;
}

// Utility functions
namespace BayesianTradingUtils {
    double calculate_sharpe_ratio(const std::vector<double>& returns) {
        if (returns.empty()) return 0.0;
        
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double sq_sum = std::inner_product(returns.begin(), returns.end(), returns.begin(), 0.0);
        double variance = sq_sum / returns.size() - mean * mean;
        double std_dev = std::sqrt(variance);
        
        return (std_dev > 1e-8) ? mean / std_dev : 0.0;
    }
    
    double calculate_accuracy(const std::vector<int>& predictions, const std::vector<int>& actual) {
        if (predictions.size() != actual.size() || predictions.empty()) {
            return 0.0;
        }
        
        int correct = 0;
        for (size_t i = 0; i < predictions.size(); ++i) {
            if (predictions[i] == actual[i]) {
                correct++;
            }
        }
        
        return static_cast<double>(correct) / predictions.size();
    }
    
    double calculate_information_ratio(const std::vector<double>& returns,
                                     const std::vector<double>& benchmark) {
        if (returns.size() != benchmark.size() || returns.empty()) {
            return 0.0;
        }
        
        std::vector<double> excess_returns;
        excess_returns.reserve(returns.size());
        
        for (size_t i = 0; i < returns.size(); ++i) {
            excess_returns.push_back(returns[i] - benchmark[i]);
        }
        
        return calculate_sharpe_ratio(excess_returns);
    }
}

    } // namespace perception
} // namespace trading