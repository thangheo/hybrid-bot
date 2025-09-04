// src/data_ingestion/data_quality/filter.cpp
#include "data_ingestion/data_quality/filter.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace trading {
namespace data_ingestion {

// PriceJumpFilter implementation
PriceJumpFilter::PriceJumpFilter(double threshold_percent)
    : threshold_percent_(threshold_percent) {
}

bool PriceJumpFilter::detectAnomaly(OrderBookUpdate& update) {
    std::string key = update.exchangeId.toString() + ":" + update.tradingPair;
    
    // Calculate mid price
    double mid_price = calculateMidPrice(update);
    if (mid_price <= 0.0) {
        return true; // Skip empty order books
    }
    
    // Check for price jump
    auto it = last_mid_prices_.find(key);
    if (it != last_mid_prices_.end()) {
        double last_price = it->second;
        double percent_change = std::abs(mid_price - last_price) / last_price * 100.0;
        
        if (percent_change > threshold_percent_) {
            std::cerr << "Price jump detected for " << key << ": " 
                      << percent_change << "% (threshold: " << threshold_percent_ << "%)" << std::endl;
            update.has_anomaly = true;
            update.anomaly_type = "price_jump";
            update.anomaly_details = "Price jump of " + std::to_string(percent_change) + "%";
            return false;
        }
    }
    
    // Update last price
    last_mid_prices_[key] = mid_price;
    return true;
}

double PriceJumpFilter::calculateMidPrice(const OrderBookUpdate& update) const {
    if (update.bids.empty() || update.asks.empty()) {
        return 0.0;
    }
    
    // Get best bid and ask
    double best_bid = update.bids[0].price;
    if (best_bid <= 0.0) {
        return 0.0; // Avoid division by zero
    }
    double best_ask = update.asks[0].price;
    if (best_ask <= 0.0) {
        return 0.0; // Avoid division by zero
    }
    
    return (best_bid + best_ask) / 2.0;
}

// SpreadFilter implementation
SpreadFilter::SpreadFilter(double max_spread_percent)
    : max_spread_percent_(max_spread_percent) {
}

bool SpreadFilter::detectAnomaly(OrderBookUpdate& update) {
    if (update.bids.empty() || update.asks.empty()) {
        return true; // Skip empty order books
    }
    
    double spread_percent = calculateSpreadPercent(update);
    
    if (spread_percent > max_spread_percent_) {
        std::cerr << "Abnormal spread detected for " << update.exchangeId.toString() << ":" << update.tradingPair
                  << ": " << spread_percent << "% (threshold: " << max_spread_percent_ << "%)" << std::endl;
        update.has_anomaly = true;
        update.anomaly_type = "wide_spread";
        update.anomaly_details = "Spread of " + std::to_string(spread_percent) + "%";
        return false;
    }
    
    return true;
}

double SpreadFilter::calculateSpread(const OrderBookUpdate& update) const {
    if (update.bids.empty() || update.asks.empty()) {
        return 0.0;
    }
    // Get best bid and ask
    double best_bid =  update.bids[0].price;
    if (best_bid <= 0.0) {
        return 0.0; // Avoid division by zero
    }
    double best_ask = update.asks[0].price;
    if (best_ask <= 0.0) {
        return 0.0; // Avoid division by zero
    }
    // Calculate spread
    // Spread = Best Ask - Best Bid
    // Note: This is a simple spread calculation, you may want to consider other factors
    // depending on your requirements.
    // For example, you might want to consider the average of the best bid and ask prices
    // or use a more complex formula.
    // Here we just return the difference
    // between the best ask and best bid prices.
    return best_ask - best_bid;
}

double SpreadFilter::calculateSpreadPercent(const OrderBookUpdate& update) const {
    if (update.bids.empty() || update.asks.empty()) {
        return 0.0;
    }
    // Get best bid and ask
    double best_bid = update.bids[0].price;
  
    double best_ask = update.asks[0].price;
    if (best_ask <= 0.0) {
        return 0.0; // Avoid division by zero
    }
    if (best_bid <= 0.0) {
        return 100.0; // Avoid division by zero
    }
    
    return (best_ask - best_bid) / best_bid * 100.0;
}

// VolumeFilter implementation
VolumeFilter::VolumeFilter(double threshold_percent)
    : threshold_percent_(threshold_percent) {
}

bool VolumeFilter::detectAnomaly(OrderBookUpdate& update) {
    std::string key = update.exchangeId.toString() + ":" + update.tradingPair;
    
    // Calculate total volume
    double total_volume = calculateTotalVolume(update);
    
    // Check for volume spike
    auto it = last_volumes_.find(key);
    if (it != last_volumes_.end()) {
        double last_volume = it->second;
        
        if (last_volume > 0.0) {
            double percent_change = (total_volume - last_volume) / last_volume * 100.0;
            
            if (percent_change > threshold_percent_) {
                std::cerr << "Volume spike detected for " << key << ": " 
                          << percent_change << "% (threshold: " << threshold_percent_ << "%)" << std::endl;
                update.has_anomaly = true;
                update.anomaly_type = "volume_spike";
                update.anomaly_details = "Volume increase of " + std::to_string(percent_change) + "%";
                return false;
            }
        }
    }
    
    // Update last volume
    last_volumes_[key] = total_volume;
    return true;
}

double VolumeFilter::calculateTotalVolume(const OrderBookUpdate& update) const {
    double total_volume = 0.0;
    
    // Sum up bid volumes
    for (const auto& level : update.bids) {
        total_volume += level.quantity;
    }
    
    // Sum up ask volumes
    for (const auto& level : update.asks) {
        total_volume += level.quantity;
    }
    
    return total_volume;
}

// AnomalyDetector implementation
AnomalyDetector::AnomalyDetector() {
    // Set up standard filter chain
    auto price_filter = std::make_shared<PriceJumpFilter>();
    auto spread_filter = std::make_shared<SpreadFilter>();
    auto volume_filter = std::make_shared<VolumeFilter>();
    
    // Chain them together
    first_filter_ = price_filter;
    price_filter->setNext(spread_filter);
    spread_filter->setNext(volume_filter);
    last_filter_ = volume_filter;
}

void AnomalyDetector::addFilter(std::shared_ptr<AnomalyFilter> filter) {
    if (last_filter_) {
        last_filter_->setNext(filter);
        last_filter_ = filter;
    } else {
        first_filter_ = last_filter_ = filter;
    }
}

bool AnomalyDetector::detectAnomalies(OrderBookUpdate& update) {
    if (!first_filter_) return true;
    return first_filter_->process(update);
}

} // namespace data_ingestion
} // namespace trading