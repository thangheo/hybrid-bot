// exchange_calibration.cpp
#include "strategy/optimization/exchange_calibration.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <cmath>
#include <nlohmann/json.hpp>

namespace trading {
namespace strategy {

ExchangeCalibration::ExchangeCalibration() {
    // Initialize with empty parameters
}

ExchangeCalibration::ExchangeCalibration(const ExchangeCalibrationConfig& config) {
    // Initialize with configuration
    initialize(config);
}

bool ExchangeCalibration::initialize(const ExchangeCalibrationConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    baseParameters_ = config.baseParameters;

    for (const auto& exchange : config.exchanges) {
        exchangeProfiles_[exchange] = ExchangeMetrics();
        exchangeParameters_[exchange] = baseParameters_;
        calibrationTimes_[exchange] = std::chrono::system_clock::now();
    }

    return true;
}

void ExchangeCalibration::updateExchangeProfile(const ExchangeId& exchange, const ExchangeMetrics& metrics) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Store or update exchange metrics
    exchangeProfiles_[exchange] = metrics;
    
    // Update calibration time
    calibrationTimes_[exchange] = std::chrono::system_clock::now();
}

ParameterSet ExchangeCalibration::getExchangeParameters(const ExchangeId& exchange) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if we have parameters for this exchange
    auto it = exchangeParameters_.find(exchange);
    if (it != exchangeParameters_.end()) {
        return it->second;
    }
    
    // If not calibrated, return base parameters
    return baseParameters_;
}

void ExchangeCalibration::calibrateParameters(const ExchangeId& exchange) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if we have metrics for this exchange
    auto it = exchangeProfiles_.find(exchange);
    if (it == exchangeProfiles_.end()) {
        // Cannot calibrate without metrics
        return;
    }
    
    // Apply exchange-specific adjustments to base parameters
    ParameterSet calibratedParams = applyExchangeAdjustments(baseParameters_, it->second);
    
    // Store calibrated parameters
    exchangeParameters_[exchange] = calibratedParams;
    
    // Update calibration timestamp
    calibrationTimes_[exchange] = std::chrono::system_clock::now();
}

bool ExchangeCalibration::isExchangeCalibrated(const ExchangeId& exchange) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return exchangeParameters_.find(exchange) != exchangeParameters_.end();
}

std::chrono::system_clock::time_point ExchangeCalibration::getCalibrationTime(const ExchangeId& exchange) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = calibrationTimes_.find(exchange);
    if (it != calibrationTimes_.end()) {
        return it->second;
    }
    
    // Return epoch time if not calibrated
    return std::chrono::system_clock::time_point();
}

void ExchangeCalibration::setBaseParameters(const ParameterSet& baseParams) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    baseParameters_ = baseParams;
}

bool ExchangeCalibration::transferCalibration(const ExchangeId& source, const ExchangeId& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if source exchange is calibrated
    auto sourceParamsIt = exchangeParameters_.find(source);
    if (sourceParamsIt == exchangeParameters_.end()) {
        return false; // Source not calibrated
    }
    
    // Check similarity between exchanges
    auto sourceProfileIt = exchangeProfiles_.find(source);
    auto targetProfileIt = exchangeProfiles_.find(target);
    
    if (sourceProfileIt == exchangeProfiles_.end() || targetProfileIt == exchangeProfiles_.end()) {
        return false; // Missing exchange profiles
    }
    
    double similarity = calculateExchangeSimilarity(source, target);
    
    // Only transfer if exchanges are similar enough
    if (similarity < 0.7) {
        return false; // Not similar enough
    }
    
    // Transfer parameters with adjustments for differences
    ParameterSet targetParams = sourceParamsIt->second;
    
    // Apply adjustments based on differences between exchanges
    // Iterate through the numeric_params map in the parameter set
    for (auto& paramPair : targetParams.numeric_params) {
        // In a real implementation, this would be more sophisticated
        // Here we'll apply a simple scaling based on similarity
        paramPair.second = paramPair.second * (0.7 + 0.3 * similarity);
    }
    
    // Store calibrated parameters for target
    exchangeParameters_[target] = targetParams;
    
    // Update calibration timestamp
    calibrationTimes_[target] = std::chrono::system_clock::now();
    
    return true;
}

bool ExchangeCalibration::saveCalibration(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        nlohmann::json j;
        
        // Save base parameters
        j["baseParameters"] = nlohmann::json::object();
        j["baseParameters"]["version"] = baseParameters_.version;
        j["baseParameters"]["exchangeId"] = baseParameters_.exchangeId.toString();
        
        // Save numeric parameters
        j["baseParameters"]["numeric_params"] = nlohmann::json::object();
        for (const auto& param : baseParameters_.numeric_params) {
            j["baseParameters"]["numeric_params"][param.first] = param.second;
        }
        
        // Save string parameters
        j["baseParameters"]["string_params"] = nlohmann::json::object();
        for (const auto& param : baseParameters_.string_params) {
            j["baseParameters"]["string_params"][param.first] = param.second;
        }
        
        // Save boolean parameters
        j["baseParameters"]["bool_params"] = nlohmann::json::object();
        for (const auto& param : baseParameters_.bool_params) {
            j["baseParameters"]["bool_params"][param.first] = param.second;
        }
        
        // Save array parameters
        j["baseParameters"]["array_params"] = nlohmann::json::object();
        for (const auto& param : baseParameters_.array_params) {
            j["baseParameters"]["array_params"][param.first] = param.second;
        }
        
        // Save exchange parameters
        j["exchangeParameters"] = nlohmann::json::object();
        for (const auto& exchange : exchangeParameters_) {
            std::string exchangeStr = exchange.first.toString();
            j["exchangeParameters"][exchangeStr] = nlohmann::json::object();
            
            const auto& params = exchange.second;
            j["exchangeParameters"][exchangeStr]["version"] = params.version;
            j["exchangeParameters"][exchangeStr]["exchangeId"] = params.exchangeId.toString();
            
            // Save numeric parameters
            j["exchangeParameters"][exchangeStr]["numeric_params"] = nlohmann::json::object();
            for (const auto& param : params.numeric_params) {
                j["exchangeParameters"][exchangeStr]["numeric_params"][param.first] = param.second;
            }
            
            // Save string parameters
            j["exchangeParameters"][exchangeStr]["string_params"] = nlohmann::json::object();
            for (const auto& param : params.string_params) {
                j["exchangeParameters"][exchangeStr]["string_params"][param.first] = param.second;
            }
            
            // Save boolean parameters
            j["exchangeParameters"][exchangeStr]["bool_params"] = nlohmann::json::object();
            for (const auto& param : params.bool_params) {
                j["exchangeParameters"][exchangeStr]["bool_params"][param.first] = param.second;
            }
            
            // Save array parameters
            j["exchangeParameters"][exchangeStr]["array_params"] = nlohmann::json::object();
            for (const auto& param : params.array_params) {
                j["exchangeParameters"][exchangeStr]["array_params"][param.first] = param.second;
            }
        }
        
        // Save calibration times
        j["calibrationTimes"] = nlohmann::json::object();
        for (const auto& time : calibrationTimes_) {
            std::string exchangeStr = time.first.toString();
            // Convert time_point to time_t
            auto time_t_val = std::chrono::system_clock::to_time_t(time.second);
            j["calibrationTimes"][exchangeStr] = time_t_val;
        }
        
        // Write to file
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << j.dump(4); // Pretty print with 4-space indent
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving calibration: " << e.what() << std::endl;
        return false;
    }
}

bool ExchangeCalibration::loadCalibration(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Read file
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        nlohmann::json j;
        file >> j;
        
        // Load base parameters
        if (j.contains("baseParameters") && j["baseParameters"].is_object()) {
            baseParameters_.version = j["baseParameters"].value("version", "1.0.0");
            baseParameters_.exchangeId = ExchangeId::fromString(j["baseParameters"].value("exchangeId", ""));
            baseParameters_.last_updated = std::chrono::system_clock::now();
            
            // Load numeric parameters
            if (j["baseParameters"].contains("numeric_params") && j["baseParameters"]["numeric_params"].is_object()) {
                baseParameters_.numeric_params.clear();
                for (auto it = j["baseParameters"]["numeric_params"].begin(); it != j["baseParameters"]["numeric_params"].end(); ++it) {
                    baseParameters_.numeric_params[it.key()] = it.value().get<double>();
                }
            }
            
            // Load string parameters
            if (j["baseParameters"].contains("string_params") && j["baseParameters"]["string_params"].is_object()) {
                baseParameters_.string_params.clear();
                for (auto it = j["baseParameters"]["string_params"].begin(); it != j["baseParameters"]["string_params"].end(); ++it) {
                    baseParameters_.string_params[it.key()] = it.value().get<std::string>();
                }
            }
            
            // Load boolean parameters
            if (j["baseParameters"].contains("bool_params") && j["baseParameters"]["bool_params"].is_object()) {
                baseParameters_.bool_params.clear();
                for (auto it = j["baseParameters"]["bool_params"].begin(); it != j["baseParameters"]["bool_params"].end(); ++it) {
                    baseParameters_.bool_params[it.key()] = it.value().get<bool>();
                }
            }
            
            // Load array parameters
            if (j["baseParameters"].contains("array_params") && j["baseParameters"]["array_params"].is_object()) {
                baseParameters_.array_params.clear();
                for (auto it = j["baseParameters"]["array_params"].begin(); it != j["baseParameters"]["array_params"].end(); ++it) {
                    baseParameters_.array_params[it.key()] = it.value().get<std::vector<double>>();
                }
            }
        }
        
        // Load exchange parameters
        exchangeParameters_.clear();
        if (j.contains("exchangeParameters") && j["exchangeParameters"].is_object()) {
            for (auto exchangeIt = j["exchangeParameters"].begin(); exchangeIt != j["exchangeParameters"].end(); ++exchangeIt) {
                ExchangeId exchangeId = ExchangeId::fromString(exchangeIt.key());
                
                if (exchangeIt.value().is_object()) {
                    ParameterSet params;
                    params.version = exchangeIt.value().value("version", "1.0.0");
                    params.exchangeId = exchangeId;
                    params.last_updated = std::chrono::system_clock::now();
                    
                    // Load numeric parameters
                    if (exchangeIt.value().contains("numeric_params") && exchangeIt.value()["numeric_params"].is_object()) {
                        for (auto paramIt = exchangeIt.value()["numeric_params"].begin(); paramIt != exchangeIt.value()["numeric_params"].end(); ++paramIt) {
                            params.numeric_params[paramIt.key()] = paramIt.value().get<double>();
                        }
                    }
                    
                    // Load string parameters
                    if (exchangeIt.value().contains("string_params") && exchangeIt.value()["string_params"].is_object()) {
                        for (auto paramIt = exchangeIt.value()["string_params"].begin(); paramIt != exchangeIt.value()["string_params"].end(); ++paramIt) {
                            params.string_params[paramIt.key()] = paramIt.value().get<std::string>();
                        }
                    }
                    
                    // Load boolean parameters
                    if (exchangeIt.value().contains("bool_params") && exchangeIt.value()["bool_params"].is_object()) {
                        for (auto paramIt = exchangeIt.value()["bool_params"].begin(); paramIt != exchangeIt.value()["bool_params"].end(); ++paramIt) {
                            params.bool_params[paramIt.key()] = paramIt.value().get<bool>();
                        }
                    }
                    
                    // Load array parameters
                    if (exchangeIt.value().contains("array_params") && exchangeIt.value()["array_params"].is_object()) {
                        for (auto paramIt = exchangeIt.value()["array_params"].begin(); paramIt != exchangeIt.value()["array_params"].end(); ++paramIt) {
                            params.array_params[paramIt.key()] = paramIt.value().get<std::vector<double>>();
                        }
                    }
                    
                    exchangeParameters_[exchangeId] = params;
                }
            }
        }
        
        // Load calibration times
        calibrationTimes_.clear();
        if (j.contains("calibrationTimes") && j["calibrationTimes"].is_object()) {
            for (auto timeIt = j["calibrationTimes"].begin(); timeIt != j["calibrationTimes"].end(); ++timeIt) {
                ExchangeId exchangeId = ExchangeId::fromString(timeIt.key());
                std::time_t time_t_val = timeIt.value().get<std::time_t>();
                calibrationTimes_[exchangeId] = std::chrono::system_clock::from_time_t(time_t_val);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading calibration: " << e.what() << std::endl;
        return false;
    }
}

double ExchangeCalibration::calculateExchangeSimilarity(const ExchangeId& exchange1, const ExchangeId& exchange2) const {
    auto it1 = exchangeProfiles_.find(exchange1);
    auto it2 = exchangeProfiles_.find(exchange2);
    
    if (it1 == exchangeProfiles_.end() || it2 == exchangeProfiles_.end()) {
        return 0.0; // No profiles, no similarity
    }
    
    const ExchangeMetrics& metrics1 = it1->second;
    const ExchangeMetrics& metrics2 = it2->second;
    
    // Calculate similarity based on key metrics
    // This is a simplified version - in reality this would be more sophisticated
    
    // Compare update frequency
    double freqSimilarity = 1.0 - std::min(1.0, std::abs(metrics1.updateFrequency - metrics2.updateFrequency) / metrics1.updateFrequency);
    
    // Compare spread patterns
    double spreadSimilarity = 1.0 - std::min(1.0, std::abs(metrics1.averageSpread - metrics2.averageSpread) / metrics1.averageSpread);
    
    // Compare order book depth
    double depthSimilarity = 1.0 - std::min(1.0, std::abs(metrics1.bookDepth - metrics2.bookDepth) / metrics1.bookDepth);
    
    // Compare liquidity
    double liquiditySimilarity = 1.0 - std::min(1.0, std::abs(metrics1.averageLiquidity - metrics2.averageLiquidity) / metrics1.averageLiquidity);
    
    // Calculate weighted average similarity
    return 0.3 * freqSimilarity + 0.3 * spreadSimilarity + 0.2 * depthSimilarity + 0.2 * liquiditySimilarity;
}

ExchangeFingerprint ExchangeCalibration::generateFingerprint(const ExchangeMetrics& metrics) const {
    ExchangeFingerprint fingerprint;
    
    // Create a fingerprint based on key characteristics
    fingerprint.updateFrequencyBucket = std::floor(metrics.updateFrequency / 10.0);  // Bucket by 10ms
    fingerprint.spreadBucket = std::floor(metrics.averageSpread * 1000.0);  // Bucket in basis points
    fingerprint.depthBucket = std::floor(metrics.bookDepth / 5.0);  // Bucket by groups of 5 levels
    fingerprint.liquidityBucket = std::floor(std::log10(metrics.averageLiquidity));  // Log scale buckets
    
    return fingerprint;
}

ParameterSet ExchangeCalibration::applyExchangeAdjustments(const ParameterSet& base, const ExchangeMetrics& metrics) const {
    ParameterSet adjusted = base;
    
    // Apply adjustments based on exchange characteristics
    // This is a simplified version - in reality this would be more sophisticated
    
    // Adjust imbalance threshold based on update frequency
    // More frequent updates may need higher thresholds to avoid noise
    if (adjusted.numeric_params.find("imbalanceThreshold") != adjusted.numeric_params.end()) {
        double frequencyFactor = std::sqrt(100.0 / std::max(1.0, metrics.updateFrequency));
        adjusted.numeric_params["imbalanceThreshold"] *= frequencyFactor;
    }
    
    // Adjust volume threshold based on exchange liquidity
    if (adjusted.numeric_params.find("volumeThreshold") != adjusted.numeric_params.end()) {
        double liquidityFactor = std::log10(metrics.averageLiquidity) / 6.0;  // Normalize to ~0.5-1.5 range
        adjusted.numeric_params["volumeThreshold"] *= liquidityFactor;
    }
    
    // Adjust signal duration based on market impact model
    if (adjusted.numeric_params.find("signalDuration") != adjusted.numeric_params.end()) {
        double impactFactor = 1.0 + (metrics.marketImpact - 0.5) * 0.5;  // Scale by market impact
        adjusted.numeric_params["signalDuration"] *= impactFactor;
    }
    
    // Adjust depth consideration based on book depth
    if (adjusted.numeric_params.find("considerationDepth") != adjusted.numeric_params.end()) {
        double depthFactor = std::min(1.5, std::max(0.5, metrics.bookDepth / 20.0));
        adjusted.numeric_params["considerationDepth"] *= depthFactor;
    }
    
    return adjusted;
}

} // namespace strategy
} // namespace trading