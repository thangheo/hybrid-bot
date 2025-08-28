// include/data_ingestion/data_quality/filter.hpp
#pragma once

#include "data_ingestion/orderbook/order_book_types.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace trading {
namespace data_ingestion {

// Base class for anomaly detection filters
class AnomalyFilter {
public:
    virtual ~AnomalyFilter() = default;
    
    // Set next filter in chain
    void setNext(std::shared_ptr<AnomalyFilter> next) {
        next_ = std::move(next);
    }
    
    // Process the update through the filter chain
    bool process(OrderBookUpdate& update) {
        // Apply this filter
        if (!detectAnomaly(update)) {
            return false;
        }
        
        // Apply next filter if it exists
        if (next_) {
            return next_->process(update);
        }
        
        return true;
    }
    
protected:
    // Implement this method in derived classes
    virtual bool detectAnomaly(OrderBookUpdate& update) = 0;
    
    std::shared_ptr<AnomalyFilter> next_;
};

// Detect large price movements
class PriceJumpFilter : public AnomalyFilter {
public:
    PriceJumpFilter(double threshold_percent = 5.0);
    
protected:
    bool detectAnomaly(OrderBookUpdate& update) override;
    
private:
    double threshold_percent_;
    std::unordered_map<std::string, double> last_mid_prices_;
    
    double calculateMidPrice(const OrderBookUpdate& update) const;
};

// Detect abnormal spread
class SpreadFilter : public AnomalyFilter {
public:
    SpreadFilter(double max_spread_percent = 5.0);
    
protected:
    bool detectAnomaly(OrderBookUpdate& update) override;
    
private:
    double max_spread_percent_;
    std::unordered_map<std::string, double> average_spreads_;
    
    double calculateSpread(const OrderBookUpdate& update) const;
    double calculateSpreadPercent(const OrderBookUpdate& update) const;
};

// Detect abnormal volume changes
class VolumeFilter : public AnomalyFilter {
public:
    VolumeFilter(double threshold_percent = 300.0);
    
protected:
    bool detectAnomaly(OrderBookUpdate& update) override;
    
private:
    double threshold_percent_;
    std::unordered_map<std::string, double> last_volumes_;
    
    double calculateTotalVolume(const OrderBookUpdate& update) const;
};

// Main anomaly detection manager
class AnomalyDetector {
public:
    AnomalyDetector();
    
    // Add a custom filter
    void addFilter(std::shared_ptr<AnomalyFilter> filter);
    
    // Detect anomalies in an update
    bool detectAnomalies(OrderBookUpdate& update);
    
private:
    std::shared_ptr<AnomalyFilter> first_filter_;
    std::shared_ptr<AnomalyFilter> last_filter_;
};
// Base class for data quality filters
class DataQualityFilter {
public:
    virtual ~DataQualityFilter() = default;
    
    // Set next filter in chain
    void setNext(std::shared_ptr<DataQualityFilter> next) {
        next_ = std::move(next);
    }
    
    // Process the update through the filter chain
    bool process(OrderBookUpdate& update) {
        // Apply this filter
        if (!filter(update)) {
            return false;
        }
        
        // Apply next filter if it exists
        if (next_) {
            return next_->process(update);
        }
        
        return true;
    }
protected:
    // Implement this method in derived classes
    virtual bool filter(OrderBookUpdate& update) = 0;
    std::shared_ptr<DataQualityFilter> next_;
};
// Checksum filter for data integrity
class ChecksumFilter : public DataQualityFilter {
public:
    bool filter(OrderBookUpdate& update) override;
};

    // SequenceGapFilter class
class SequenceGapFilter : public DataQualityFilter {
public:
    SequenceGapFilter(int max_gap = 10);
    bool filter(OrderBookUpdate& update) override;
private:
    int max_gap_;
    std::unordered_map<std::string, int> last_sequence_numbers_;
};
// In filter.hpp
class PriceRangeFilter : public DataQualityFilter {
    public:
        // Add a default constructor
        PriceRangeFilter() = default;
        // Keep existing constructor
        PriceRangeFilter(const std::unordered_map<std::string, double>& max_prices);
        bool filter(OrderBookUpdate& update) override;
    private:
        std::unordered_map<std::string, double> max_prices_;
        double getReasonablePriceRange(const std::string& symbol) const;
    };
} // namespace data_ingestion
} // namespace trading