/**
 * @file spread_imbalance_detector.hpp
 * @brief Detector for abnormal spread conditions
 */
#pragma once

#include "strategy/pattern/detectors/imbalance_detector_interface.hpp"
#include <map>
#include <mutex>

namespace trading {
namespace strategy {
namespace detectors {

/**
 * @class SpreadImbalanceDetector
 * @brief Detects abnormal spread conditions in the order book
 * 
 * This detector analyzes the bid-ask spread relative to historical averages
 * to identify unusual market conditions that may indicate liquidity issues
 * or other market stress.
 */
class SpreadImbalanceDetector : public AbstractDetector {
public:
    SpreadImbalanceDetector();
    virtual ~SpreadImbalanceDetector() = default;
    
    // IImbalanceDetector interface
    void updateConfig(const std::map<std::string, double>& params) override;
    
protected:
    // AbstractDetector interface
    DetectorAnalysisResult detectImpl(const DetectorContext& context) override;
    bool validateContext(const DetectorContext& context) const override;
    
private:
    // Configuration parameters
    double minStrengthThreshold_ = 0.2;
    double volumeBiasThreshold_ = 1.2;  // For directional determination
    
    // Helper methods
    double calculateSpreadRatio(const OrderBookMetrics& metrics) const;
    Direction determineDirection(const OrderBookMetrics& metrics) const;
};

} // namespace detectors
} // namespace strategy
} // namespace trading