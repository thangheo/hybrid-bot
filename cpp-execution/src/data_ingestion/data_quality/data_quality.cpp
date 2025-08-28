// src/data_ingestion/data_quality/data_quality.cpp
#include "data_ingestion/data_quality/data_quality.hpp"
#include <iostream>
#include <sstream>

namespace trading {
namespace data_ingestion {

// DataQualityControl implementation
DataQualityControl::DataQualityControl() {
    // Set up standard filter chain
    auto checksum_filter = std::make_shared<ChecksumFilter>();
    auto price_range_filter = std::make_shared<PriceRangeFilter>();
    auto sequence_gap_filter = std::make_shared<SequenceGapFilter>();
    
    // Chain them together
    first_filter_ = checksum_filter;
    checksum_filter->setNext(price_range_filter);
    price_range_filter->setNext(sequence_gap_filter);
    last_filter_ = sequence_gap_filter;
    
    // Create anomaly detector
    anomaly_detector_ = std::make_shared<AnomalyDetector>();
    
    // Create validator
    validator_ = std::make_shared<DataValidator>();
}

void DataQualityControl::addFilter(std::shared_ptr<DataQualityFilter> filter) {
    if (last_filter_) {
        last_filter_->setNext(filter);
        last_filter_ = filter;
    } else {
        first_filter_ = last_filter_ = filter;
    }
}

void DataQualityControl::addAnomalyFilter(std::shared_ptr<AnomalyFilter> filter) {
    anomaly_detector_->addFilter(filter);
}

void DataQualityControl::addValidationRule(const std::string& name, ValidationRule rule) {
    validator_->addRule(name, rule);
}

bool DataQualityControl::validateUpdate(OrderBookUpdate& update) {
    // Step 1: Apply data filters
    if (!first_filter_ || !first_filter_->process(update)) {
        return false;
    }
    
    // Step 2: Validate data structure
    if (!validator_->validate(update, validation_context_)) {
        std::cerr << "Validation failed for " << update.exchangeId.toString() << ":" << update.tradingPair << std::endl;
        for (const auto& error : validation_context_.errors) {
            std::cerr << "  - " << error << std::endl;
        }
        return false;
    }
    
    // Step 3: Detect anomalies
    if (!anomaly_detector_->detectAnomalies(update)) {
        std::cerr << "Anomaly detected for " << update.exchangeId.toString() << ":" << update.tradingPair 
                  << ": " << update.anomaly_type << " - " << update.anomaly_details << std::endl;
        return false;
    }
    
    return true;
}

const std::vector<std::string>& DataQualityControl::getValidationErrors() const {
    return validation_context_.errors;
}

// ChecksumFilter implementation
bool ChecksumFilter::filter(OrderBookUpdate& update) {
    // Skip if no checksum provided
    if (update.checksum.empty()) return true;
    
    // In a real implementation, compute the checksum and validate
    // For now, always return true
    return true;
}

// PriceRangeFilter implementation
PriceRangeFilter::PriceRangeFilter(const std::unordered_map<std::string, double>& max_prices) 
    : max_prices_(max_prices) {
}

bool PriceRangeFilter::filter(OrderBookUpdate& update) {
    double reasonable_range = getReasonablePriceRange(update.tradingPair);
    
    // Check if any price is outside reasonable range
    for (const auto& level : update.bids) {
        double price = level.getPrice();
        if (price <= 0 || price > reasonable_range) {
            std::cerr << "Price range filter failed for bid: " << price 
                      << " (max: " << reasonable_range << ")" << std::endl;
            return false;
        }
    }
    
    for (const auto& level : update.asks) {
        double price = level.getPrice();
        if (price <= 0 || price > reasonable_range) {
            std::cerr << "Price range filter failed for ask: " << price 
                      << " (max: " << reasonable_range << ")" << std::endl;
            return false;
        }
    }
    
    return true;
}

double PriceRangeFilter::getReasonablePriceRange(const std::string& symbol) const {
    // First check if we have a specific limit for this symbol
    auto it = max_prices_.find(symbol);
    if (it != max_prices_.end()) {
        return it->second;
    }
    
    // Otherwise use default limits based on typical ranges
    if (symbol.find("BTC") != std::string::npos) return 100000.0;
    if (symbol.find("ETH") != std::string::npos) return 10000.0;
    
    // Default fallback - very high to avoid false positives
    return 1000000.0;
}

// SequenceGapFilter implementation
bool SequenceGapFilter::filter(OrderBookUpdate& update) {
    std::string key = update.exchangeId.toString() + ":" + update.tradingPair;
    
    auto it = last_sequence_numbers_.find(key);
    if (it == last_sequence_numbers_.end()) {
        // First update for this symbol
        last_sequence_numbers_[key] = update.sequence_number;
        return true;
    }
    
    // Check for sequence gap
    if (update.sequence_number <= it->second) {
        // Out of order or duplicate
        std::cerr << "Sequence number out of order: " << update.sequence_number 
                  << " <= " << it->second << std::endl;
        return false;
    }
    
    if (update.sequence_number > it->second + 1 && !update.is_snapshot) {
        // Gap detected - may need to request a snapshot
        std::cerr << "Sequence gap detected: " << it->second 
                  << " -> " << update.sequence_number << std::endl;
        // For now, we'll accept it but log the warning
    }
    
    last_sequence_numbers_[key] = update.sequence_number;
    return true;
}

} // namespace data_ingestion
} // namespace trading