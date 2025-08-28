// exchange_calibration.hpp
#pragma once

#include "strategy/types.hpp"
#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include <memory>
#include <map>
#include <string>
#include <mutex>
#include <vector>
#include <chrono>

using namespace trading::data_ingestion;
namespace trading {
namespace strategy {

/**
 * Class responsible for calibrating strategy parameters for different exchanges
 */
class ExchangeCalibration {
public:
    // Constructor
    ExchangeCalibration();
    ~ExchangeCalibration() = default;
    // /usr/include/c++/13/bits/stl_construct.h:119:7: error: no matching function for call to ‘trading::strategy::ExchangeCalibration::ExchangeCalibration(trading::strategy::ExchangeCalibrationConfig&)’
    // Constructor with configuration
    ExchangeCalibration(const ExchangeCalibrationConfig& config);
    // Initialize the calibration engine with configuration
    bool initialize(const ExchangeCalibrationConfig& config);
    // Update exchange behavior profile
    void updateExchangeProfile(const ExchangeId& exchange, const ExchangeMetrics& metrics);
    
    // Get exchange-specific parameters
    ParameterSet getExchangeParameters(const ExchangeId& exchange) const;
    
    // Calibrate parameters for a specific exchange
    void calibrateParameters(const ExchangeId& exchange);
    
    // Check if exchange is calibrated
    bool isExchangeCalibrated(const ExchangeId& exchange) const;
    
    // Get calibration timestamp
    std::chrono::system_clock::time_point getCalibrationTime(const ExchangeId& exchange) const;
    
    // Set base parameters (used as starting point for calibration)
    void setBaseParameters(const ParameterSet& baseParams);
    
    // Transfer calibrated parameters from one exchange to another
    bool transferCalibration(const ExchangeId& source, const ExchangeId& target);
    
    // Save and load calibration data
    bool saveCalibration(const std::string& filename) const;
    bool loadCalibration(const std::string& filename);

private:
    // Base parameters (exchange-agnostic)
    ParameterSet baseParameters_;
    
    // Exchange-specific parameters
    std::map<ExchangeId, ParameterSet> exchangeParameters_;
    
    // Exchange behavior profiles
    std::map<ExchangeId, ExchangeMetrics> exchangeProfiles_;
    
    // Calibration timestamps
    std::map<ExchangeId, std::chrono::system_clock::time_point> calibrationTimes_;
    
    // Calculate similarity between exchanges
    double calculateExchangeSimilarity(const ExchangeId& exchange1, const ExchangeId& exchange2) const;
    
    // Generate exchange fingerprint
    ExchangeFingerprint generateFingerprint(const ExchangeMetrics& metrics) const;
    
    // Apply exchange-specific adjustments to base parameters
    ParameterSet applyExchangeAdjustments(const ParameterSet& base, const ExchangeMetrics& metrics) const;
    
    // Thread safety
    mutable std::mutex mutex_;
};
// struct ExchangeCalibrationConfig {
//     ParameterSet baseParameters;
//     std::vector<ExchangeId> exchanges;
// };
} // namespace strategy
} // namespace trading