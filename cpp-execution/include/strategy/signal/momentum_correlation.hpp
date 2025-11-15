/**
 * @file momentum_correlation.hpp
 * @brief Correlates order book imbalances with price movements
 * 
 * @details
 * This component analyzes the correlation between detected order book imbalances
 * and subsequent price movements to improve signal quality and prediction accuracy.
 * 
 * Created on: May 6, 2025
 */
#pragma once

#include "strategy/types.hpp"
#include <common/circular_buffer.hpp>
#include <common/vector_pool.hpp>
#include <map>
#include <deque>
#include <vector>


using namespace trading::common;  // For CircularBuffer
   // Track metric statistics for normalization
   struct MetricStatistics {
       double mean = 0.0;
       double stddev = 1.0;
       size_t sampleCount = 0;
   };
  
   


namespace trading {
namespace strategy {

/**
 * @class MomentumCorrelation
 * @brief Correlates order book imbalances with price movements
 */
class MomentumCorrelation {
public:
    /**
     * @brief Constructor
     * @param window_size Size of the rolling window for correlation analysis
     */
    explicit MomentumCorrelation(size_t window_size = 100);
    MomentumCorrelation(const MomentumCorrelationConfig& config);

    /**
     * @brief Destructor
     */
    virtual ~MomentumCorrelation() = default;
    
    /**
     * @brief Update the correlation model with new data
     * @param metrics Order book metrics
     * @param movement Subsequent price movement
     */
    virtual void updateCorrelationModel(const OrderBookMetrics& metrics, const PriceMovement& movement);
    
    /**
     * @brief Predict price movement based on an imbalance event
     * @param event The imbalance event to analyze
     * @return Predicted price movement
     */
    virtual double predictPriceMovement(const ImbalanceEvent& event);
    
    /**
     * @brief Get correlation statistics for all tracked metrics
     * @return Structure containing correlation statistics
     */
    virtual CorrelationStats getCorrelationStats() const;
    
    /**
     * @brief Configure the correlation parameters
     * @param params Correlation configuration parameters
     */
    void configure(const CorrelationParameters& params);
    
    /**
     * @brief Reset correlation data and start fresh
     */
    void reset();
    
    /**
     * @brief Detect the current market regime
     * @return Identified market regime
     */
    MarketRegimeType detectMarketRegime() const;

    /**
     * @brief Get correlation coefficient for a specific metric
     * @param metric_name Name of the metric
     * @return Correlation coefficient (-1.0 to 1.0)
     */
    double getCorrelationCoefficient(const std::string& metric_name) const;
    
    /**
     * @brief Get feature importance for imbalance metrics
     * @return Map of metric names to importance scores
     */
    std::map<std::string, double> getFeatureImportance() const;
   //updateParameters
    /**
     * @brief Update parameters for the correlation model
     * @param params New parameters
     */
    // void updateParameters(const CorrelationParameters& params);
    void updateParameters(const ParameterSet& params);
    
    /**
     * @brief Get the number of data points used in correlation analysis
     * @return Number of data points
     */
    size_t getDataPointCount() const;
    /**
     * @brief Get the current market regime
     * @return Current market regime
     */
    MarketRegimeType getCurrentMarketRegime() const;
    /**
     * @brief Get the current correlation parameters
     * @return Current correlation parameters
     */
    CorrelationParameters getCurrentParameters() const;
    /**
     * @brief Get the current correlation coefficients
     * @return Map of metric names to correlation coefficients
     */
    // updateHighestCorrelatedMetrics
    void updateHighestCorrelatedMetrics();
private:
    size_t window_size_=200; // Rolling window size for correlation
    size_t data_point_limit_ = 2000; // Hard cap on data points to prevent excessive memory use
    CorrelationParameters params_;
    //mutex correlation_mutex_;
    // Mutex for thread safety
    mutable std::mutex mutex_;
    // Mutex for thread safety
    // Mutex for thread safety
    mutable std::mutex correlation_mutex_;
    // highest_correlated_metrics_
    std::vector<std::pair<std::string, double>> highest_correlated_metrics_;
    // Data storage for correlation analysis
    struct DataPoint {
        OrderBookMetrics metrics;
        PriceMovement price_movement;
        double volume_ratio = 1.0; // If available
        double spread_ratio = 1.0; // If available
        core::Timestamp timestamp;
    };
    // mutable std::unordered_map<std::string, MetricStatistics> metricStatistics_;
    std::map<std::string, MetricStatistics> metricStatistics_;
    // std::deque<DataPoint> data_points_;
    common::CircularBuffer<DataPoint> data_points_{5000};

    std::map<std::string, double> correlation_coefficients_;
    // correlation_strengths_
    std::map<std::string, double> correlation_strengths_;
    // Last update times (milliseconds since epoch) for EWMA smoothing per metric
    std::map<std::string, uint64_t> last_update_ms_;
    std::map<std::string, double> feature_importance_;
    // Make current_regime_ atomic so reads/writes can be done lock-free where
    // appropriate. Use .load()/.store() to access the value.
    std::atomic<MarketRegimeType> current_regime_{MarketRegimeType::NORMAL};
    // Per-regime consecutive detection counters to implement hysteresis
    mutable std::map<MarketRegimeType, int> regime_consecutive_counts_;
    // Required consecutive detections per regime before switching
    std::map<MarketRegimeType, int> regime_required_counts_;
    // Last detected regime (working detector) and a consecutive counter used
    // by the smoothing logic. Protected by correlation_mutex_ when mutated.
    mutable MarketRegimeType last_detection_ = MarketRegimeType::NORMAL;
    mutable int consecutive_count_ = 0;
    // Lock-free snapshots published for fast read access by predictPriceMovement()
    // Updated under correlation_mutex_ via std::atomic_store/std::atomic_load helpers
    mutable std::shared_ptr<std::map<std::string, double>> coeffs_snapshot_;
    mutable std::shared_ptr<std::map<std::string, double>> importance_snapshot_;
    double calculatePearsonCorrelation(const std::vector<double>& x, const std::vector<double>& y) const;
    // std::string imbalanceTypeToString(ImbalanceType type) const;

    /**
     * @brief Calculate correlation coefficients from stored data points
     */
    void calculateCorrelations();
    // double getMarketRegimeMultiplier(MarketRegime regime) const;

    /**
     * @brief Calculate feature importance using statistical methods
     */
    void calculateFeatureImportance();
    
    /**
     * @brief Check if the correlation is statistically significant
     * @param coefficient The correlation coefficient
     * @param sample_size Number of samples used
     * @return True if statistically significant
     */
    bool isStatisticallySignificant(double coefficient, size_t sample_size) const;
    
    /**
     * @brief Detect changes in market regime from correlation patterns
     */
    void updateMarketRegime();
    
    /**
     * @brief Apply weights to different metrics based on their predictive power
     * @param event The imbalance event to evaluate
     * @return Weighted prediction value
     */
    double calculateWeightedPrediction(const ImbalanceEvent& event) const;
    // calculateTrendPersistence
    double calculateTrendPersistence(const std::vector<double>& movements) const;
    // calculateVolatilityIndex
    double calculateVolatilityIndex() const;
    // calculateLiquidityIndex
    double calculateLiquidityIndex();
    // double calculateLowLiquidityIndicators(const std::vector<double>& recentMovements,
    //                                                        const std::vector<double>& recentVolumes,
    //                                                        const std::vector<double>& recentSpreads) const ;
    bool LowLiquidityRegimeDetector(std::vector<DataPoint> snapshot);
                                                           
    // calculateMeanAndStd
    // void calculateMeanAndStd(const std::vector<double>& data, double& mean, double& stddev) const;
    //prototype for  [movementMean, movementStd] = calculateMeanAndStd(recentMovements);
    std::pair<double, double> calculateMeanAndStd(const std::vector<double>& data) const;
    
    // calculateMomentumAcceleration
    double calculateMomentumAcceleration(const std::vector<double>& movements) const;
    // calculateMarketStress
    double calculateMarketStress(const std::vector<double>& movements, double volatility) const;
    // Take detectedRegime by value; the smoothing implementation will acquire
    // the correlation mutex and update `current_regime_` atomically when
    // sufficient consecutive detections accumulate.
    void applyRegimeSmoothing(MarketRegimeType detectedRegime);
    // regimeToString
    // std::string regimeToString(MarketRegime regime) const {
};

} // namespace strategy
} // namespace trading