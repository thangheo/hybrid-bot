// experiment_results.cpp
// #include "strategy/optimization/experiment_results.hpp"
#include "strategy/types.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace trading {
namespace strategy {

/**
 * Calculate statistical significance between two variants
 * 
 * @param variant1 First variant
 * @param variant2 Second variant
 * @param metricName Name of the metric to compare
 * @return p-value of the comparison (lower is more significant)
 */
double calculateStatisticalSignificance(
    const VariantResult& variant1,
    const VariantResult& variant2,
    const std::string& metricName = "sharpeRatio") {
    
    // Need at least 30 data points for reliable significance testing
    if (variant1.metrics.size() < 30 || variant2.metrics.size() < 30) {
        return 1.0; // Not significant
    }
    
    // Get metric values
    std::vector<double> values1, values2;
    
    for (const auto& metric : variant1.metrics) {
        if (metricName == "sharpeRatio") {
            values1.push_back(metric.sharpeRatio);
        } else if (metricName == "totalReturn") {
            values1.push_back(metric.totalReturn);
        } else if (metricName == "winRate") {
            values1.push_back(metric.winRate);
        }
    }
    
    for (const auto& metric : variant2.metrics) {
        if (metricName == "sharpeRatio") {
            values2.push_back(metric.sharpeRatio);
        } else if (metricName == "totalReturn") {
            values2.push_back(metric.totalReturn);
        } else if (metricName == "winRate") {
            values2.push_back(metric.winRate);
        }
    }
    
    // Calculate means
    double mean1 = 0.0, mean2 = 0.0;
    for (double val : values1) mean1 += val;
    for (double val : values2) mean2 += val;
    mean1 /= values1.size();
    mean2 /= values2.size();
    
    // Calculate standard deviations
    double var1 = 0.0, var2 = 0.0;
    for (double val : values1) var1 += (val - mean1) * (val - mean1);
    for (double val : values2) var2 += (val - mean2) * (val - mean2);
    var1 /= (values1.size() - 1);
    var2 /= (values2.size() - 1);
    
    // Calculate standard error
    double se = std::sqrt(var1 / values1.size() + var2 / values2.size());
    
    // Calculate t-statistic
    double t = (mean1 - mean2) / se;
    
    // Simple approximation of p-value from t-statistic
    // This is a simplified calculation - in practice you'd use a proper
    // statistical library for more accurate p-values
    double p = 2.0 * (1.0 - std::min(1.0, std::abs(t) / 10.0));
    
    return p;
}

/**
 * Generate a detailed report for experiment results
 * 
 * @param results Experiment results
 * @return String containing the detailed report
 */
std::string generateExperimentReport(const ExperimentResults& results) {
    std::stringstream ss;
    
    // Header
    ss << "=======================================================\n";
    ss << "Experiment Report: " << results.experimentId << " - " << results.experimentName << "\n";
    ss << "=======================================================\n\n";
    
    // Summary
    ss << "Summary:\n";
    ss << "  Number of variants: " << results.variantResults.size() << "\n";
    
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(
        now - results.lastUpdateTime).count();
    
    ss << "  Last updated: " << duration << " hours ago\n\n";
    
    // Find the best variant by Sharpe ratio
    std::string bestVariantId;
    double bestSharpe = -std::numeric_limits<double>::infinity();
    
    for (const auto& variant : results.variantResults) {
        if (variant.second.aggregateMetrics.sharpeRatio > bestSharpe) {
            bestVariantId = variant.first;
            bestSharpe = variant.second.aggregateMetrics.sharpeRatio;
        }
    }
    
    if (!bestVariantId.empty()) {
        ss << "  Best variant: " << bestVariantId << " (Sharpe ratio: " 
           << std::fixed << std::setprecision(4) << bestSharpe << ")\n\n";
    }
    
    // Variant details
    ss << "Variant Details:\n";
    
    for (const auto& variant : results.variantResults) {
        const auto& result = variant.second;
        
        ss << "  " << variant.first << ":\n";
        ss << "    Impressions: " << result.impressions << "\n";
        ss << "    Conversions: " << result.conversions << "\n";
        ss << "    Conversion rate: " << std::fixed << std::setprecision(2) 
           << (result.conversionRate * 100.0) << "%\n";
        ss << "    Metrics:\n";
        ss << "      Sharpe ratio: " << std::fixed << std::setprecision(4) 
           << result.aggregateMetrics.sharpeRatio << "\n";
        ss << "      Total return: " << std::fixed << std::setprecision(2) 
           << (result.aggregateMetrics.totalReturn * 100.0) << "%\n";
        ss << "      Max drawdown: " << std::fixed << std::setprecision(2) 
           << (result.aggregateMetrics.maxDrawdown * 100.0) << "%\n";
        ss << "      Win rate: " << std::fixed << std::setprecision(2) 
           << (result.aggregateMetrics.winRate * 100.0) << "%\n";
        ss << "      Profit factor: " << std::fixed << std::setprecision(2) 
           << result.aggregateMetrics.profitFactor << "\n";
           
        // Compare to best variant
        if (variant.first != bestVariantId && !bestVariantId.empty()) {
            double pValue = calculateStatisticalSignificance(
                result, results.variantResults.at(bestVariantId));
            
            ss << "    Statistical significance vs. best variant: ";
            if (pValue < 0.01) {
                ss << "Very high (p < 0.01)\n";
            } else if (pValue < 0.05) {
                ss << "High (p < 0.05)\n";
            } else if (pValue < 0.1) {
                ss << "Moderate (p < 0.1)\n";
            } else {
                ss << "Low (p = " << std::fixed << std::setprecision(2) << pValue << ")\n";
            }
        }
        
        ss << "\n";
    }
    
    // Statistical analysis
    if (results.variantResults.size() >= 2) {
        ss << "Statistical Analysis:\n";
        
        std::vector<std::string> variantIds;
        for (const auto& variant : results.variantResults) {
            variantIds.push_back(variant.first);
        }
        
        for (size_t i = 0; i < variantIds.size(); ++i) {
            for (size_t j = i + 1; j < variantIds.size(); ++j) {
                const auto& variant1 = results.variantResults.at(variantIds[i]);
                const auto& variant2 = results.variantResults.at(variantIds[j]);
                
                double pValue = calculateStatisticalSignificance(variant1, variant2);
                
                ss << "  " << variantIds[i] << " vs. " << variantIds[j] << ": ";
                
                if (variant1.aggregateMetrics.sharpeRatio > variant2.aggregateMetrics.sharpeRatio) {
                    ss << variantIds[i] << " outperforms " << variantIds[j];
                } else {
                    ss << variantIds[j] << " outperforms " << variantIds[i];
                }
                
                ss << " (p = " << std::fixed << std::setprecision(3) << pValue << ")\n";
            }
        }
    }
    
    return ss.str();
}

/**
 * Create a JSON representation of experiment results for API responses
 * 
 * @param results Experiment results
 * @return JSON string representation
 */
std::string resultsToJson(const ExperimentResults& results) {
    std::stringstream ss;
    
    ss << "{\n";
    ss << "  \"experimentId\": \"" << results.experimentId << "\",\n";
    ss << "  \"experimentName\": \"" << results.experimentName << "\",\n";
    
    // Format last update time to ISO 8601
    auto time_t = std::chrono::system_clock::to_time_t(results.lastUpdateTime);
    char timeStr[100];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t));
    
    ss << "  \"lastUpdateTime\": \"" << timeStr << "\",\n";
    ss << "  \"variants\": [\n";
    
    bool firstVariant = true;
    for (const auto& variant : results.variantResults) {
        if (!firstVariant) {
            ss << ",\n";
        }
        firstVariant = false;
        
        ss << "    {\n";
        ss << "      \"variantId\": \"" << variant.first << "\",\n";
        ss << "      \"impressions\": " << variant.second.impressions << ",\n";
        ss << "      \"conversions\": " << variant.second.conversions << ",\n";
        ss << "      \"conversionRate\": " << std::fixed << std::setprecision(4) 
           << variant.second.conversionRate << ",\n";
        ss << "      \"metrics\": {\n";
        ss << "        \"sharpeRatio\": " << std::fixed << std::setprecision(4) 
           << variant.second.aggregateMetrics.sharpeRatio << ",\n";
        ss << "        \"totalReturn\": " << std::fixed << std::setprecision(4) 
           << variant.second.aggregateMetrics.totalReturn << ",\n";
        ss << "        \"maxDrawdown\": " << std::fixed << std::setprecision(4) 
           << variant.second.aggregateMetrics.maxDrawdown << ",\n";
        ss << "        \"winRate\": " << std::fixed << std::setprecision(4) 
           << variant.second.aggregateMetrics.winRate << ",\n";
        ss << "        \"profitFactor\": " << std::fixed << std::setprecision(4) 
           << variant.second.aggregateMetrics.profitFactor << "\n";
        ss << "      }\n";
        ss << "    }";
    }
    
    ss << "\n  ]\n}";
    
    return ss.str();
}

} // namespace strategy
} // namespace trading