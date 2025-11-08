/**
 * @file volume_imbalance_detector.hpp
 * @brief Detector for volume imbalances between bid/ask sides
 */
#pragma once

#include "strategy/pattern/detectors/imbalance_detector_interface.hpp"
#include "core/types.hpp"
#include <map>
#include <mutex>

namespace trading {
namespace strategy {
namespace detectors {

/**
 * @class VolumeImbalanceDetector
 * @brief Detects significant imbalances in bid/ask volumes
 * 
 * This detector analyzes the ratio between bid and ask volumes to identify
 * potential directional pressure in the order book.
 */
class VolumeImbalanceDetector : public AbstractDetector {
public:
    VolumeImbalanceDetector();
    virtual ~VolumeImbalanceDetector() = default;
    
    // IImbalanceDetector interface
    void updateConfig(const std::map<std::string, double>& params) override;
    
protected:
    // AbstractDetector interface
    DetectorAnalysisResult detectImpl(const DetectorContext& context) override;
    bool validateContext(const DetectorContext& context) const override;
    
private:
    // Configuration parameters
    double emaBlendAlpha_ = 0.7;
    double minChangeThreshold_ = 0.01;
    double minStrengthThreshold_ = 0.2;
    
    // State tracking for change detection
    std::map<std::pair<core::ExchangeId, core::Symbol>, double> lastVolumeRatio_;
    mutable std::mutex volumeRatioMutex_;
    
    // Helper methods
    double calculateVolumeRatio(const OrderBookMetrics& metrics) const;
    double applyEmaSmoothing(double rawRatio, const OrderBookMetrics& metrics) const;
    double calculateStrength(double volumeRatio, double threshold) const;
    bool hasSignificantChange(double currentRatio, const core::ExchangeId& exchange, 
                             const core::Symbol& symbol);
};

} // namespace detectors
} // namespace strategy
} // namespace trading