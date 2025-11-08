/**
 * @file imbalance_detector_interface.hpp
 * @brief Interface and base class for imbalance detectors
 * 
 * @details
 * Defines the common interface that all imbalance detectors must implement.
 * This allows for modular, testable, and maintainable detector implementations.
 * 
 * Created on: November 8, 2025
 */
#pragma once

#include "strategy/types.hpp"
#include "strategy/pattern/analyzer.hpp"
#include <memory>
#include <string>

namespace trading {
namespace strategy {
namespace detectors {

/**
 * @brief Result from a detector analysis
 */
struct DetectorAnalysisResult {
    bool hasSignal = false;
    double confidence = 0.0;
    double strength = 0.0;
    Direction direction = Direction::BUY;
    ImbalanceType type = ImbalanceType::VOLUME_IMBALANCE;
    double rawValue = 0.0;
    std::string reason;
    
    // Validation
    bool isValid() const {
        return confidence >= 0.0 && confidence <= 1.0 && 
               strength >= 0.0 && std::isfinite(rawValue);
    }
};

/**
 * @brief Context data passed to detectors
 */
struct DetectorContext {
    const OrderBookMetrics* metrics = nullptr;
    const ThresholdSet* thresholds = nullptr;
    int currentRegime = 0;
    double currentVolatility = 0.0;
    uint64_t timestamp = 0;
    
    bool isValid() const {
        return metrics != nullptr && thresholds != nullptr;
    }
};

/**
 * @interface IImbalanceDetector
 * @brief Pure interface for all imbalance detectors
 */
class IImbalanceDetector {
public:
    virtual ~IImbalanceDetector() = default;
    
    /**
     * @brief Analyze order book metrics for imbalances
     * @param context Detection context with metrics and configuration
     * @return Analysis result with signal information
     */
    virtual DetectorAnalysisResult analyze(const DetectorContext& context) = 0;
    
    /**
     * @brief Get detector type identifier
     */
    virtual ImbalanceType getDetectorType() const = 0;
    
    /**
     * @brief Get detector name for logging/debugging
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Check if detector is enabled
     */
    virtual bool isEnabled() const = 0;
    
    /**
     * @brief Enable/disable detector
     */
    virtual void setEnabled(bool enabled) = 0;
    
    /**
     * @brief Update detector-specific configuration
     */
    virtual void updateConfig(const std::map<std::string, double>& params) = 0;
};

/**
 * @class AbstractDetector
 * @brief Base implementation providing common functionality
 */
class AbstractDetector : public IImbalanceDetector {
public:
    explicit AbstractDetector(const std::string& name, ImbalanceType type, bool enabled = true);
    virtual ~AbstractDetector() = default;
    
    // IImbalanceDetector interface
    ImbalanceType getDetectorType() const override { return detectorType_; }
    std::string getName() const override { return name_; }
    bool isEnabled() const override { return enabled_; }
    void setEnabled(bool enabled) override { enabled_ = enabled; }
    
    // Template method pattern - subclasses implement detectImpl
    DetectorAnalysisResult analyze(const DetectorContext& context) final;
    
protected:
    /**
     * @brief Core detection logic - implemented by subclasses
     */
    virtual DetectorAnalysisResult detectImpl(const DetectorContext& context) = 0;
    
    /**
     * @brief Validate context before analysis
     */
    virtual bool validateContext(const DetectorContext& context) const;
    
    /**
     * @brief Apply common post-processing to results
     */
    virtual void postProcessResult(DetectorAnalysisResult& result, const DetectorContext& context) const;
    
    // Helper methods
    bool isValidThreshold(double value) const;
    double clampConfidence(double confidence) const;
    
private:
    std::string name_;
    ImbalanceType detectorType_;
    bool enabled_;
    
    // Statistics for monitoring
    mutable size_t totalAnalyses_ = 0;
    mutable size_t successfulDetections_ = 0;
};

} // namespace detectors
} // namespace strategy  
} // namespace trading