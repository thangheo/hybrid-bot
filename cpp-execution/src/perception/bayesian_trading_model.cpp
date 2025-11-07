#include "perception/bayesian_trading_model.hpp"
#include <random>
#include <numeric>
// #include <json/json.h>  // For JSON serialization

namespace trading {
    namespace perception {

// PRODUCTION FIX: Use unified Direction enum instead of duplicate BayesianClass
// Direction enum now has explicit values: BUY=0, SELL=1, NEUTRAL=2
// This ensures perfect alignment with Bayesian model classes

// Utility functions for Direction enum
constexpr int direction_to_class_index(Direction dir) {
    return static_cast<int>(dir);
}

constexpr Direction class_index_to_direction(int class_idx) {
    return static_cast<Direction>(class_idx);
}
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
    // Allocate per-class parameters (one-vs-rest)
    mu_.assign(NUM_CLASSES, Eigen::VectorXd::Zero(num_features));
    sigma_diag_.assign(NUM_CLASSES, Eigen::VectorXd::Constant(num_features, config_.initial_sigma));
    sigma_inv_diag_.assign(NUM_CLASSES, Eigen::VectorXd::Constant(num_features, 1.0 / config_.initial_sigma));
    
    // Initialize feature normalization
    feature_mean_ = Eigen::VectorXd::Zero(num_features);
    feature_std_ = Eigen::VectorXd::Ones(num_features);
    feature_m2_ = Eigen::VectorXd::Zero(num_features);
    // initialize feature active mask (assume active until stats say otherwise)
    feature_active_.assign(num_features, 1);
    
    is_initialized_ = true;
    
    std::cout << "[BayesianTradingModel] Initialized with " << num_features << " features" << std::endl;
}

void BayesianTradingModel::update_feature_stats(const Eigen::VectorXd& features) {
    // Welford's online algorithm for mean and variance
    num_updates_++;
    
    for (int i = 0; i < num_features_; ++i) {
        double raw_val = features(i);
        
        // Apply same log transformation during stats update as in normalize_features
        bool should_log_transform = config_.enable_log_transform && 
                                   raw_val > config_.log_transform_threshold &&
                                   std::find(config_.log_transform_indices.begin(), 
                                           config_.log_transform_indices.end(), i) != 
                                   config_.log_transform_indices.end();
        
        if (should_log_transform) {
            raw_val = std::log1p(std::abs(raw_val));
            
            // Additional safety for extreme cases
            if (!std::isfinite(raw_val)) {
                raw_val = 0.0;
            }
        }
        
        double delta = raw_val - feature_mean_(i);
        feature_mean_(i) += delta / num_updates_;
        double delta2 = raw_val - feature_mean_(i);
        feature_m2_(i) += delta * delta2;
        
        if (num_updates_ > 1) {
            feature_std_(i) = std::sqrt(feature_m2_(i) / (num_updates_ - 1));
            // If variance is effectively zero, mark feature as inactive and set std to 1.0
            if (!std::isfinite(feature_std_(i)) || feature_std_(i) < 1e-12) {
                feature_std_(i) = 1.0;
                feature_active_[i] = 0; // ignore constant feature
            }
        }
    }
}

Eigen::VectorXd BayesianTradingModel::normalize_features(const Eigen::VectorXd& features) const {
    
    if (num_updates_ <= 1) {
        Eigen::VectorXd normalized = features;
        normalized.setZero();  // treat as zero-mean start
        return normalized;
    }

    Eigen::VectorXd normalized(num_features_);
    for (int i = 0; i < num_features_; ++i) {
        double raw_val = features(i);
        
        // Apply log transformation for configured features to prevent overflow
        // and handle extremely skewed distributions (like FLOKI feature 2: ~10^17)
        bool should_log_transform = config_.enable_log_transform && 
                                   raw_val > config_.log_transform_threshold &&
                                   std::find(config_.log_transform_indices.begin(), 
                                           config_.log_transform_indices.end(), i) != 
                                   config_.log_transform_indices.end();
        
        if (should_log_transform) {
            // Use log1p for numerical stability: log(1 + x)
            // This handles extreme cases like FLOKI (mean=202k, std=3.6M)
            raw_val = std::log1p(std::abs(raw_val));
            
            // Additional safety: handle potential -inf/inf cases
            if (!std::isfinite(raw_val)) {
                raw_val = 0.0;
            }
        }        // Standard normalization with overflow protection
        double mean_val = feature_mean_(i);
        double std_val = feature_std_(i);
        
        // Add small epsilon to prevent division by zero
        if (std_val < 1e-8) std_val = 1e-8;

        // If feature is inactive (constant), set normalized to zero and skip
        if (i < static_cast<int>(feature_active_.size()) && feature_active_[i] == 0) {
            normalized(i) = 0.0;
        } else {
            normalized(i) = (raw_val - mean_val) / std_val;
        }
        
        // Clip outliers to [-3, 3] sigma for numerical stability
        normalized(i) = std::max(-3.0, std::min(3.0, normalized(i)));
    }
    
    return normalized;
}

void BayesianTradingModel::update_bayesian_weights(const Eigen::VectorXd& normalized_features, int label) {
    // One-vs-rest Bayesian update per class using diagonal covariance approximation
    // Label is 0,1,2. Create one-hot targets for each class: t_c = 1 if label==c else 0
    for (int c = 0; c < NUM_CLASSES; ++c) {
        double t = (label == c) ? 1.0 : 0.0;

        // Using diagonal approximation: sigma_inv_new = sigma_inv + beta * x^2 (elementwise)
        // Zero-out inactive features so they don't affect precision updates
        Eigen::VectorXd x2 = normalized_features.array().square();
        for (int i = 0; i < x2.size(); ++i) {
            if (i < static_cast<int>(feature_active_.size()) && feature_active_[i] == 0) {
                x2(i) = 0.0;
            }
        }

        // Apply forgetting: discount previous precision (larger precision => more certain).
        // If forgetting_factor < 1, we reduce the contribution of the previous precision to allow
        // the posterior to be more influenced by recent data. We implement forgetting by
        // scaling the previous inverse-variance (precision) by forgetting_factor.
        double ff = std::clamp(config_.forgetting_factor, 0.0, 1.0);
        Eigen::VectorXd prev_sigma_inv = sigma_inv_diag_[c] * ff; 

        Eigen::VectorXd sigma_inv_new = prev_sigma_inv.array() + config_.beta * x2.array();
        
        // Validate sigma_inv_new to prevent numerical issues
        for (int i = 0; i < sigma_inv_new.size(); ++i) {
            if (!std::isfinite(sigma_inv_new(i)) || sigma_inv_new(i) <= 0.0) {
                sigma_inv_new(i) = 1.0 / config_.initial_sigma; // Reset to initial value
            }
            // Prevent extremely large precision (small variance)
            const double MAX_PRECISION = 1e4; // tightened from 1e6
            if (sigma_inv_new(i) > MAX_PRECISION) {
                sigma_inv_new(i) = MAX_PRECISION;
            }
            // If feature inactive, keep precision at prior
            if (i < static_cast<int>(feature_active_.size()) && feature_active_[i] == 0) {
                sigma_inv_new(i) = 1.0 / config_.initial_sigma;
            }
        }

        // Update mean: mu_new = mu + config_.beta * (sigma_new * x) * (t - sigmoid(mu.dot(x)))
        // sigma_new (diag) = 1 / sigma_inv_new
        Eigen::VectorXd sigma_new = sigma_inv_new.array().inverse();

        // Process noise: inflate diagonal covariance slightly to model parameter drift.
        if (config_.process_noise > 0.0) {
            sigma_new.array() += config_.process_noise;
            // Validate after adding process noise
            for (int i = 0; i < sigma_new.size(); ++i) {
                if (!std::isfinite(sigma_new(i)) || sigma_new(i) <= 0.0) {
                    sigma_new(i) = config_.initial_sigma;
                }
            }
            // Recompute inverse after adding process noise
            sigma_inv_new = sigma_new.array().inverse();
        }

        double pred_logit = mu_[c].dot(normalized_features);
        
        // Validate pred_logit before sigmoid
        if (!std::isfinite(pred_logit)) {
            std::cerr << "[BayesianTradingModel] Warning: pred_logit NaN/Inf for class " << c << " - skipping update" << std::endl;
            continue;
        }
        
        double pred = sigmoid(pred_logit);
        double error = t - pred;
        
        // Validate error before computing delta
        if (!std::isfinite(error)) {
            std::cerr << "[BayesianTradingModel] Warning: error NaN/Inf for class " << c << " - skipping update" << std::endl;
            continue;
        }

        // delta_mu = sigma_new * (config_.beta * x * error) elementwise
        Eigen::VectorXd delta = (sigma_new.array() * (config_.beta * normalized_features.array() * error)).matrix();
        
        // Validate delta before applying
        bool delta_valid = true;
        for (int i = 0; i < delta.size(); ++i) {
            if (!std::isfinite(delta(i))) {
                delta_valid = false;
                break;
            }
        }
        
        if (!delta_valid) {
            std::cerr << "[BayesianTradingModel] Warning: delta contains NaN/Inf for class " << c << " - skipping update" << std::endl;
            continue;
        }
        
        // Clamp delta to prevent explosive updates
        const double DELTA_CLAMP = 10.0;
        for (int i = 0; i < delta.size(); ++i) {
            delta(i) = std::max(-DELTA_CLAMP, std::min(DELTA_CLAMP, delta(i)));
        }
        
        mu_[c] += delta;
        
        // Clamp mu_ weights to prevent extreme values
        const double MU_CLAMP = 100.0;
        for (int i = 0; i < mu_[c].size(); ++i) {
            if (!std::isfinite(mu_[c](i))) {
                mu_[c](i) = 0.0;
            } else {
                mu_[c](i) = std::max(-MU_CLAMP, std::min(MU_CLAMP, mu_[c](i)));
            }
        }

        // Store back diagonal covariances and inverses
        sigma_diag_[c] = sigma_new;
        sigma_inv_diag_[c] = sigma_inv_new;
    }
}
//signmoid or logistic function
double BayesianTradingModel::sigmoid(double x) const {
    // Numerical stable sigmoid
    if (x > 500) return 1.0;
    if (x < -500) return 0.0;
    return 1.0 / (1.0 + std::exp(-x));
}

void BayesianTradingModel::update_sigma_inverse() {
    // No-op for diagonal implementation; kept for compatibility
}

std::tuple<double, double, double> BayesianTradingModel::predict_multiclass(const std::vector<double>& features) {
    if (features.empty()) {
        throw std::invalid_argument("Features vector cannot be empty");
    }

    if (!is_initialized_) {
        initialize_model(features.size());
    }

    if (features.size() != static_cast<size_t>(num_features_)) {
        throw std::invalid_argument("Feature dimension mismatch");
    }

    std::lock_guard<std::mutex> lock(model_mutex_);
    Eigen::VectorXd feature_vec = Eigen::Map<const Eigen::VectorXd>(features.data(), features.size());
    Eigen::VectorXd normalized = normalize_features(feature_vec);

    // Defensive: ensure normalized features are finite. If not, log and fallback.
    for (int i = 0; i < normalized.size(); ++i) {
        if (!std::isfinite(normalized(i))) {
            std::cerr << "[BayesianTradingModel] Warning: normalized features contain NaN/Inf - zeroing features and returning uniform probs" << std::endl;
            return {1.0/3.0, 1.0/3.0, 1.0/3.0};
        }
    }

    // Compute logits for each class
    Eigen::VectorXd logits(NUM_CLASSES);
    bool logits_invalid = false;
    
    for (int c = 0; c < NUM_CLASSES; ++c) {
        // Check if mu_[c] contains any non-finite values
        bool mu_valid = true;
        for (int i = 0; i < mu_[c].size(); ++i) {
            if (!std::isfinite(mu_[c](i))) {
                mu_valid = false;
                break;
            }
        }
        
        if (!mu_valid) {
            std::cerr << "[BayesianTradingModel] Warning: mu_[" << c << "] contains NaN/Inf values - zeroing" << std::endl;
            logits(c) = 0.0;
            logits_invalid = true;
            continue;
        }
        
        double val = mu_[c].dot(normalized);
        if (!std::isfinite(val)) {
            std::cerr << "[BayesianTradingModel] Warning: dot product for class " << c << " is NaN/Inf (" << val << ") - zeroing" << std::endl;
            val = 0.0;
            logits_invalid = true;
        }
        logits(c) = val;
    }
    
    // If any logits are problematic, return uniform probabilities immediately
    if (logits_invalid) {
        std::cerr << "[BayesianTradingModel] Warning: logits computation unstable - returning uniform probs" << std::endl;
        return {1.0/3.0, 1.0/3.0, 1.0/3.0};
    }

    // Clamp logits to a safe numeric range before softmax to avoid overflow/NaN
    const double LOGIT_CLAMP = 700.0; // exp(700) is huge but still below overflow on double
    for (int c = 0; c < NUM_CLASSES; ++c) {
        if (!std::isfinite(logits(c))) logits(c) = 0.0;
        if (logits(c) > LOGIT_CLAMP) logits(c) = LOGIT_CLAMP;
        if (logits(c) < -LOGIT_CLAMP) logits(c) = -LOGIT_CLAMP;
    }

    // Softmax to probabilities (stable)
    double max_logit = logits.maxCoeff();
    
    // Additional safety: ensure max_logit is finite
    if (!std::isfinite(max_logit)) {
        std::cerr << "[BayesianTradingModel] Warning: max_logit is NaN/Inf (" << max_logit << ") - using uniform probs" << std::endl;
        return {1.0/3.0, 1.0/3.0, 1.0/3.0};
    }
    
    Eigen::VectorXd exps = (logits.array() - max_logit).exp();
    double sum_exps = exps.sum();
    Eigen::VectorXd probs(NUM_CLASSES);

    // Defensive checks: handle numerical edge cases where exps may contain inf/NaN
    bool any_nan = false;
    for (int i = 0; i < NUM_CLASSES; ++i) {
        if (!std::isfinite(exps(i))) { any_nan = true; break; }
    }

    if (any_nan || !std::isfinite(sum_exps) || sum_exps <= 0.0) {
        // Log warning and fallback to uniform probabilities to avoid crash
        std::cerr << "[BayesianTradingModel] Warning: unstable softmax computation (sum_exps=" << sum_exps << ") - falling back to uniform probs" << std::endl;
        probs = Eigen::VectorXd::Constant(NUM_CLASSES, 1.0 / static_cast<double>(NUM_CLASSES));
    } else {
        probs = exps / sum_exps;

        // Additional guard: clip negatives / NaNs then renormalize
        double sum_clean = 0.0;
        for (int i = 0; i < NUM_CLASSES; ++i) {
            if (!std::isfinite(probs(i)) || probs(i) < 0.0) probs(i) = 0.0;
            sum_clean += probs(i);
        }

        if (sum_clean <= 0.0 || !std::isfinite(sum_clean)) {
            std::cerr << "[BayesianTradingModel] Warning: cleaned probability sum invalid (" << sum_clean << ") - using uniform probs" << std::endl;
            probs = Eigen::VectorXd::Constant(NUM_CLASSES, 1.0 / static_cast<double>(NUM_CLASSES));
        } else if (std::abs(sum_clean - 1.0) > 1e-12) {
            probs = probs / sum_clean; // renormalize
        }
    }

    return {probs(0), probs(1), probs(2)};
}
 

BayesianTradingModel::TradingSignal BayesianTradingModel::predict(const std::vector<double>& features) {
    auto [prob_class0, prob_class1, prob_class2] = predict_multiclass(features);
    
    // Validate probability distribution (defensive: do not abort on numerical issues)
    double prob_sum = prob_class0 + prob_class1 + prob_class2;
    if (!std::isfinite(prob_sum) || std::abs(prob_sum - 1.0) > 1e-6) {
        std::cerr << "[BayesianTradingModel] Warning: probability sum invalid (" << prob_sum << ") - continuing with normalized probs" << std::endl;
        // Normalize if possible
        double denom = (std::isfinite(prob_sum) && prob_sum > 0.0) ? prob_sum : 1.0;
        prob_class0 = std::isfinite(prob_class0) ? prob_class0 / denom : 1.0/3.0;
        prob_class1 = std::isfinite(prob_class1) ? prob_class1 / denom : 1.0/3.0;
        prob_class2 = std::isfinite(prob_class2) ? prob_class2 / denom : 1.0/3.0;
    }
    
    TradingSignal signal;
    
    // OPTIMAL EXIT STRATEGY: Clean 3-class prediction
    // Let exit strategy handle position management, not model prediction
    // Class mapping: 0=SELL, 1=NEUTRAL, 2=BUY (clean separation)
    
    // High neutral probability suggests uncertainty - reduce confidence
    if (prob_class1 > neutral_veto_threshold_) {
        return createNeutralSignal();
    }
    
    // Find the class with highest probability (excluding neutral if below threshold)
    Direction predicted_direction = Direction::NEUTRAL;
    double max_prob = 0.0;
    
    if (prob_class0 > max_prob && prob_class0 > min_confidence_threshold_) {
        predicted_direction = Direction::SELL;  // Class 0 = SELL
        max_prob = prob_class0;
    }
    
    if (prob_class2 > max_prob && prob_class2 > min_confidence_threshold_) {
        predicted_direction = Direction::BUY;   // Class 2 = BUY
        max_prob = prob_class2;
    }
    
    // Check if we have a confident prediction
    if (max_prob <= min_confidence_threshold_) {
        return createNeutralSignal();
    }
    
    // Direct assignment - no conversion needed
    signal.direction = predicted_direction;
    signal.should_trade = true;
    signal.probability = max_prob;
    
    // Clean position sizing based on prediction confidence
    signal.position_size = calculatePositionSize(prob_class2, prob_class0); // BUY vs SELL probabilities
    
    // Calculate entropy-based uncertainty (order doesn't matter for entropy)
    signal.uncertainty = calculateEntropy(prob_class0, prob_class1, prob_class2);
    
    return signal;
}


void BayesianTradingModel::update(const std::vector<double>& features, int label) {
    if (features.empty()) {
        throw std::invalid_argument("Features vector cannot be empty");
    }
    
    if (label < 0 || label >= NUM_CLASSES) {
        throw std::invalid_argument("Label must be 0 (SELL), 1 (NEUTRAL) or 2 (BUY)");
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
    // Return flattened means for all classes then uncertainties (sqrt of diag variances)
    std::vector<double> means;
    std::vector<double> uncertainties;
    means.reserve(NUM_CLASSES * num_features_);
    uncertainties.reserve(NUM_CLASSES * num_features_);

    for (int c = 0; c < NUM_CLASSES; ++c) {
        for (int i = 0; i < num_features_; ++i) {
            means.push_back(mu_[c](i));
            uncertainties.push_back(std::sqrt(sigma_diag_[c](i)));
        }
    }

    return {means, uncertainties};
}

BayesianTradingModel::ModelStats BayesianTradingModel::get_stats() const {
    std::lock_guard<std::mutex> lock(model_mutex_);
    
    ModelStats stats;
    stats.num_features = num_features_;
    stats.num_updates = num_updates_;
    
    if (is_initialized_) {
        // Total uncertainty as sum of traces of per-class covariance
        double trace = 0.0;
        for (int c = 0; c < NUM_CLASSES; ++c) {
            trace += sigma_diag_[c].sum();
        }
        stats.total_uncertainty = trace;
        
        // Feature statistics
        stats.feature_means.assign(feature_mean_.data(), feature_mean_.data() + feature_mean_.size());
        stats.feature_stds.assign(feature_std_.data(), feature_std_.data() + feature_std_.size());
        
        // Weight statistics
        stats.weight_means.clear();
        stats.weight_uncertainties.clear();
        for (int c = 0; c < NUM_CLASSES; ++c) {
            for (int i = 0; i < num_features_; ++i) {
                stats.weight_means.push_back(mu_[c](i));
                stats.weight_uncertainties.push_back(std::sqrt(sigma_diag_[c](i)));
            }
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
    config_json["forgetting_factor"] = config_.forgetting_factor;
    config_json["process_noise"] = config_.process_noise;
        
        // Log transformation settings
        config_json["enable_log_transform"] = config_.enable_log_transform;
        config_json["log_transform_threshold"] = config_.log_transform_threshold;
        Json::Value log_indices_json(Json::arrayValue);
        for (int idx : config_.log_transform_indices) {
            log_indices_json.append(idx);
        }
        config_json["log_transform_indices"] = log_indices_json;
        
        root["config"] = config_json;
        
        // Model state
        root["num_features"] = num_features_;
        root["num_updates"] = num_updates_;
        
        // Weights (mean) per class
        Json::Value mu_json(Json::arrayValue);
        for (int c = 0; c < NUM_CLASSES; ++c) {
            Json::Value class_arr(Json::arrayValue);
            for (int i = 0; i < num_features_; ++i) {
                class_arr.append(mu_[c](i));
            }
            mu_json.append(class_arr);
        }
        root["mu"] = mu_json;

        // Diagonal covariance per class (flatten class-major)
        Json::Value sigma_diag_json(Json::arrayValue);
        for (int c = 0; c < NUM_CLASSES; ++c) {
            Json::Value class_arr(Json::arrayValue);
            for (int i = 0; i < num_features_; ++i) {
                class_arr.append(sigma_diag_[c](i));
            }
            sigma_diag_json.append(class_arr);
        }
        root["sigma_diag"] = sigma_diag_json;
        
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
    config_.beta = config_json.get("beta", 0.1).asDouble();
        config_.min_probability = config_json.get("min_probability", 0.6).asDouble();
        config_.base_position_size = config_json.get("base_position_size", 0.1).asDouble();
        config_.uncertainty_penalty = config_json.get("uncertainty_penalty", 0.5).asDouble();
        config_.use_expected_value = config_json.get("use_expected_value", true).asBool();
        config_.payoff_long = config_json.get("payoff_long", 1.0).asDouble();
        config_.payoff_short = config_json.get("payoff_short", 1.0).asDouble();
        config_.forgetting_factor = config_json.get("forgetting_factor", 1.0).asDouble();
    config_.process_noise = config_json.get("process_noise", 0.0).asDouble();
        
        // Load log transformation settings
        config_.enable_log_transform = config_json.get("enable_log_transform", true).asBool();
        config_.log_transform_threshold = config_json.get("log_transform_threshold", 0.0).asDouble();
        config_.log_transform_indices.clear();
        if (config_json.isMember("log_transform_indices")) {
            const Json::Value& indices_json = config_json["log_transform_indices"];
            for (const auto& idx : indices_json) {
                config_.log_transform_indices.push_back(idx.asInt());
            }
        } else {
            // Default to feature 2 for backward compatibility
            config_.log_transform_indices = {2};
        }
        
        // Load model state
        num_features_ = root["num_features"].asInt();
        num_updates_ = root["num_updates"].asInt();
        
        // Load weights per class
        const Json::Value& mu_json = root["mu"];
        mu_.clear();
        mu_.reserve(NUM_CLASSES);
        for (int c = 0; c < NUM_CLASSES; ++c) {
            Eigen::VectorXd v(num_features_);
            const Json::Value& class_arr = mu_json[c];
            for (int i = 0; i < num_features_; ++i) {
                v(i) = class_arr[i].asDouble();
            }
            mu_.push_back(v);
        }

        // Load diagonal covariance per class
        const Json::Value& sigma_diag_json = root["sigma_diag"];
        sigma_diag_.clear();
        sigma_inv_diag_.clear();
        sigma_diag_.reserve(NUM_CLASSES);
        sigma_inv_diag_.reserve(NUM_CLASSES);
        for (int c = 0; c < NUM_CLASSES; ++c) {
            Eigen::VectorXd v(num_features_);
            const Json::Value& class_arr = sigma_diag_json[c];
            for (int i = 0; i < num_features_; ++i) {
                v(i) = class_arr[i].asDouble();
            }
            // Sanitize loaded variances: clamp to reasonable range
            for (int i = 0; i < v.size(); ++i) {
                if (!std::isfinite(v(i)) || v(i) <= 0.0) v(i) = config_.initial_sigma;
                const double MAX_VARIANCE = 1e6; // prevent astronomic variances
                if (v(i) > MAX_VARIANCE) v(i) = MAX_VARIANCE;
            }
            sigma_diag_.push_back(v);
            Eigen::VectorXd inv = v.array().inverse();
            sigma_inv_diag_.push_back(inv);
        }
        
        // Load feature normalization
        const Json::Value& feat_mean_json = root["feature_mean"];
        const Json::Value& feat_std_json = root["feature_std"];
        const Json::Value& feat_m2_json = root["feature_m2"];

        feature_mean_ = Eigen::VectorXd(num_features_);
        feature_std_ = Eigen::VectorXd(num_features_);
        feature_m2_ = Eigen::VectorXd(num_features_);

        // initialize feature_active_ mask and sanitize feature stds
        feature_active_.assign(num_features_, 1);

        for (int i = 0; i < num_features_; ++i) {
            feature_mean_(i) = feat_mean_json[i].asDouble();
            feature_std_(i) = feat_std_json[i].asDouble();
            feature_m2_(i) = feat_m2_json[i].asDouble();

            if (!std::isfinite(feature_std_(i)) || feature_std_(i) < 1e-12) {
                feature_std_(i) = 1.0;
                feature_active_[i] = 0;
            }
        }
        
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
    // Total uncertainty (sum of diag across classes)
    double total_uncert = 0.0;
    for (int c = 0; c < NUM_CLASSES; ++c) total_uncert += sigma_diag_[c].sum();
    std::cout << "Total Uncertainty: " << total_uncert << std::endl;
    std::cout << "Ready for trading: " << (is_ready() ? "Yes" : "No") << std::endl;
    
    std::cout << "\nWeight Summary:" << std::endl;
    for (int c = 0; c < NUM_CLASSES; ++c) {
        std::cout << "Class " << c << " weights:\n";
        for (int i = 0; i < num_features_; ++i) {
            std::cout << "  Feature " << i << ": " 
                      << mu_[c](i) << " ± " << std::sqrt(std::max(0.0, sigma_diag_[c](i))) << std::endl;
        }
    }
    
    std::cout << "\nConfig:" << std::endl;
    std::cout << "  Use Expected Value: " << config_.use_expected_value << std::endl;
    std::cout << "  Min Probability: " << config_.min_probability << std::endl;
    std::cout << "  Base Position Size: " << config_.base_position_size << std::endl;
    std::cout << "  Beta: " << config_.beta << std::endl;
    std::cout << "  Forgetting Factor: " << config_.forgetting_factor << std::endl;
    std::cout << "  Process Noise: " << config_.process_noise << std::endl;
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