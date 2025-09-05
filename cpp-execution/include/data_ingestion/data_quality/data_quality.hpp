// include/data_ingestion/data_quality/data_quality.hpp
#pragma once

#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/data_quality/filter.hpp"
#include "data_ingestion/data_quality/validator.hpp"
#include "data_ingestion/config/config.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace trading {
namespace data_ingestion {

// Main quality control class that combines validation and anomaly detection
class DataQualityControl {
public:
    DataQualityControl();
    // configure with config file
    void configure(const DataQualityConfig& config);

    // Add a custom data filter
    void addFilter(std::shared_ptr<DataQualityFilter> filter);
    
    // Add an anomaly filter
    void addAnomalyFilter(std::shared_ptr<AnomalyFilter> filter);
    
    // Add a validation rule
    void addValidationRule(const std::string& name, ValidationRule rule);
    
    // Process and validate an update
    bool validateUpdate(OrderBookUpdate& update);
    
    // Get validation errors from last validation
    const std::vector<std::string>& getValidationErrors() const;
    
private:
    // Data validation
    std::shared_ptr<DataQualityFilter> first_filter_;
    std::shared_ptr<DataQualityFilter> last_filter_;
    
    // Anomaly detection
    std::shared_ptr<AnomalyDetector> anomaly_detector_;
    
    // Data validation
    std::shared_ptr<DataValidator> validator_;
    ValidationContext validation_context_;
};

} // namespace data_ingestion
} // namespace trading