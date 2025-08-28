/**
 * @file signal_decay_model.cpp
 * @brief Implementation of signal decay modeling
 */

 #include "strategy/signal/signal_decay_model.hpp"
 #include <cmath>
 #include <algorithm>
 #include <functional>
 
 namespace trading {
 namespace strategy {
 
 SignalDecayModel::SignalDecayModel(DecayModelType model_type)
     : model_type_(model_type) {
     // Initialize with default decay parameters
     params_.base_decay_rate = 0.5;
     params_.time_unit_ms = 1000;  // 1 second time units
     params_.step_intervals = {0.0, 0.5, 0.8, 1.0};
     params_.step_values = {1.0, 0.8, 0.4, 0.0};
     params_.min_ttl_ms = 500;  // Minimum signal life of 500ms
     params_.min_valid_strength = 0.2;  // Minimum strength to be valid
     
     // Set default type-specific decay rates
     type_specific_decay_rates_[ImbalanceType::VOLUME_IMBALANCE] = 0.7;       // Faster decay
     type_specific_decay_rates_[ImbalanceType::PRICE_LEVEL_IMBALANCE] = 0.5;  // Medium decay
     type_specific_decay_rates_[ImbalanceType::LIQUIDITY_IMBALANCE] = 0.4;    // Slower decay
     type_specific_decay_rates_[ImbalanceType::ABNORMAL_SPREAD] = 0.8;        // Very fast decay
     type_specific_decay_rates_[ImbalanceType::TREND_CONTINUATION] = 0.3;     // Very slow decay
     type_specific_decay_rates_[ImbalanceType::TREND_REVERSAL] = 0.4;         // Slow decay
     
     // Set default type-specific TTLs
     type_specific_ttl_[ImbalanceType::VOLUME_IMBALANCE] = 2000;        // 2 seconds
     type_specific_ttl_[ImbalanceType::PRICE_LEVEL_IMBALANCE] = 3000;   // 3 seconds
     type_specific_ttl_[ImbalanceType::LIQUIDITY_IMBALANCE] = 4000;     // 4 seconds
     type_specific_ttl_[ImbalanceType::ABNORMAL_SPREAD] = 1000;         // 1 second
     type_specific_ttl_[ImbalanceType::TREND_CONTINUATION] = 5000;      // 5 seconds
     type_specific_ttl_[ImbalanceType::TREND_REVERSAL] = 4000;          // 4 seconds
 }
 
 void SignalDecayModel::configureDecayParameters(const DecayParameters& params) {
     params_ = params;
 }
 
 double SignalDecayModel::calculateRemainingStrength(const TradingSignal& signal, uint64_t current_time) {
     // Get timestamp values in milliseconds for proper comparison
     auto signal_time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(signal.generation_time)
                               .time_since_epoch().count();
     auto expiration_time_ms = signal_time_ms + 
                               std::chrono::duration_cast<std::chrono::milliseconds>(signal.ttl).count();
     
     // Check if signal has already expired based on TTL
     if (current_time > expiration_time_ms) {
         return 0.0;
     }
     
     // Select decay function based on model type
     switch (model_type_) {
         case DecayModelType::EXPONENTIAL:
             return exponentialDecay(signal, current_time);
         case DecayModelType::LINEAR:
             return linearDecay(signal, current_time);
         case DecayModelType::STEP:
             return stepDecay(signal, current_time);
         case DecayModelType::CONDITIONAL:
             return conditionalDecay(signal, current_time);
         case DecayModelType::CUSTOM:
             if (custom_decay_func_) {
                 return custom_decay_func_(signal, current_time);
             }
             // Fall back to exponential if no custom function set
             return exponentialDecay(signal, current_time);
         default:
             return exponentialDecay(signal, current_time);
     }
 }
 
 bool SignalDecayModel::isSignalValid(const TradingSignal& signal, uint64_t current_time) {
     // Get timestamp values in milliseconds for proper comparison
     auto signal_time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(signal.generation_time)
                               .time_since_epoch().count();
     auto expiration_time_ms = signal_time_ms + 
                               std::chrono::duration_cast<std::chrono::milliseconds>(signal.ttl).count();
     
     // Check absolute expiration based on TTL
     if (current_time > expiration_time_ms) {
         return false;
     }
     
     // Check if market events have invalidated the signal
     if (isInvalidatedByMarketEvents(signal)) {
         return false;
     }
     
     // Check if remaining strength is above minimum threshold
     double remaining_strength = calculateRemainingStrength(signal, current_time);
     return remaining_strength >= params_.min_valid_strength;
 }
 
 void SignalDecayModel::setCustomDecayFunction(std::function<double(const TradingSignal&, uint64_t)> decay_func) {
     custom_decay_func_ = std::move(decay_func);
     model_type_ = DecayModelType::CUSTOM;
 }
 
 DecayModelType SignalDecayModel::getModelType() const {
     return model_type_;
 }
 
 void SignalDecayModel::setModelType(DecayModelType model_type) {
     model_type_ = model_type;
 }
 
 void SignalDecayModel::registerMarketEvent(MarketEventType event_type, const MarketEventData& event_data) {
     MarketEvent event;
     event.type = event_type;
     event.data = event_data;
     event.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
     
     // Add to recent events
     recent_market_events_.push_back(event);
     
     // Keep only recent events (last 10 seconds)
     const uint64_t max_age_ms = 10000;
     uint64_t oldest_valid_time = event.timestamp - max_age_ms;
     
     // Remove old events
     recent_market_events_.erase(
         std::remove_if(recent_market_events_.begin(), recent_market_events_.end(),
             [oldest_valid_time](const MarketEvent& e) {
                 return e.timestamp < oldest_valid_time;
             }),
         recent_market_events_.end());
 }
 
 void SignalDecayModel::calibrateFromHistory(const std::vector<SignalOutcome>& signal_history) {
     if (signal_history.empty()) {
         return;
     }
     
     // Maps to track decay rates and TTLs by imbalance type
     std::map<ImbalanceType, std::vector<double>> decayRatesByType;
     std::map<ImbalanceType, std::vector<uint64_t>> ttlsByType;
     
     // Process historical signals
     for (const auto& outcome : signal_history) {
         const auto& signal = outcome.signal;
         ImbalanceType type = signal.source_type;
         
         // Skip signals that didn't reach their target
         if (!outcome.reached_target) {
             continue;
         }
         
         // Calculate how long it took to reach the target
         auto signal_time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(signal.generation_time)
                                   .time_since_epoch().count();
         uint64_t time_to_target_ms = outcome.outcome_time - signal_time_ms;
         
         // If the signal worked, use its time to target to calibrate TTL
         if (time_to_target_ms > 0) {
             ttlsByType[type].push_back(time_to_target_ms);
             
             // Estimate decay rate from time to target and signal strength
             // Decay rate is inversely proportional to how long signal remains valid
             double implied_decay_rate = 1.0 / (time_to_target_ms / 1000.0);
             
             // Cap the decay rate at reasonable values
             implied_decay_rate = std::min(0.9, std::max(0.1, implied_decay_rate));
             
             decayRatesByType[type].push_back(implied_decay_rate);
         }
     }
     
     // Update type-specific decay rates and TTLs based on historical data
     for (auto& [type, decay_rates] : decayRatesByType) {
         if (decay_rates.empty()) {
             continue;
         }
         
         // Calculate median decay rate
         std::sort(decay_rates.begin(), decay_rates.end());
         double median_decay_rate = decay_rates[decay_rates.size() / 2];
         
         // Update with 80/20 blend of existing and new value
         type_specific_decay_rates_[type] = 0.8 * type_specific_decay_rates_[type] + 0.2 * median_decay_rate;
     }
     
     for (auto& [type, ttls] : ttlsByType) {
         if (ttls.empty()) {
             continue;
         }
         
         // Calculate median TTL
         std::sort(ttls.begin(), ttls.end());
         uint64_t median_ttl = ttls[ttls.size() / 2];
         
         // Add some buffer to the TTL (20% extra)
         median_ttl = static_cast<uint64_t>(median_ttl * 1.2);
         
         // Update with 80/20 blend of existing and new value
         type_specific_ttl_[type] = static_cast<uint64_t>(
             0.8 * type_specific_ttl_[type] + 0.2 * median_ttl);
     }
 }
//  updateParameters
    void SignalDecayModel::updateParameters(const ParameterSet& params) {
        // params_ = params;
        // Update decay parameters
        params_.base_decay_rate = params.getDouble("base_decay_rate", 0.5);
        params_.time_unit_ms = params.getInt("time_unit_ms", 1000);
        params_.min_ttl_ms = params.getInt("min_ttl_ms", 500);
        params_.min_valid_strength = params.getDouble("min_valid_strength", 0.2);
        params_.step_intervals = params.getArrayParam("step_intervals", {0.0, 0.5, 0.8, 1.0});
        params_.step_values = params.getArrayParam("step_values", {1.0, 0.8, 0.4, 0.0});
        
    }
    // calculateDecayRate
    double SignalDecayModel::calculateDecayRate(ImbalanceType imbalance_type) const {
        auto it = type_specific_decay_rates_.find(imbalance_type);
        if (it != type_specific_decay_rates_.end()) {
            return it->second;
        }
        return params_.base_decay_rate;  // Default decay rate
    }
    // getSignalTTL
    uint64_t SignalDecayModel::getSignalTTL(ImbalanceType imbalance_type) const {
        auto it = type_specific_ttl_.find(imbalance_type);
        if (it != type_specific_ttl_.end()) {
            return it->second;
        }
        return params_.min_ttl_ms;  // Default TTL
    }
 double SignalDecayModel::exponentialDecay(const TradingSignal& signal, uint64_t current_time) {
     // Convert timestamps to milliseconds for calculation
     auto signal_time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(signal.generation_time)
                               .time_since_epoch().count();
     uint64_t elapsed_ms = current_time - signal_time_ms;
     
     // Get type-specific decay rate
     double decay_rate = getTypeSpecificDecayRate(signal);
     
     // Calculate time units elapsed
     double time_units = static_cast<double>(elapsed_ms) / params_.time_unit_ms;
     
     // Calculate remaining strength using exponential decay formula
     return signal.strength * std::exp(-decay_rate * time_units);
 }
 
 double SignalDecayModel::linearDecay(const TradingSignal& signal, uint64_t current_time) {
     // Convert timestamps to milliseconds for calculation
     auto signal_time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(signal.generation_time)
                               .time_since_epoch().count();
     uint64_t elapsed_ms = current_time - signal_time_ms;
     
     // Get TTL in milliseconds
     uint64_t ttl_ms = std::chrono::duration_cast<std::chrono::milliseconds>(signal.ttl).count();
     
     // Normalize elapsed time as a fraction of TTL
     double elapsed_fraction = static_cast<double>(elapsed_ms) / ttl_ms;
     
     // Ensure elapsed_fraction is between 0 and 1
     elapsed_fraction = std::min(1.0, std::max(0.0, elapsed_fraction));
     
     // Linear decay from original strength to zero
     return signal.strength * (1.0 - elapsed_fraction);
 }
 
 double SignalDecayModel::stepDecay(const TradingSignal& signal, uint64_t current_time) {
     // Convert timestamps to milliseconds for calculation
     auto signal_time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(signal.generation_time)
                               .time_since_epoch().count();
     uint64_t elapsed_ms = current_time - signal_time_ms;
     
     // Get TTL in milliseconds
     uint64_t ttl_ms = std::chrono::duration_cast<std::chrono::milliseconds>(signal.ttl).count();
     
     // Normalize elapsed time as a fraction of TTL
     double elapsed_fraction = static_cast<double>(elapsed_ms) / ttl_ms;
     
     // Find appropriate step
     for (size_t i = 0; i < params_.step_intervals.size() - 1; ++i) {
         if (elapsed_fraction >= params_.step_intervals[i] && 
             elapsed_fraction < params_.step_intervals[i + 1]) {
             return signal.strength * params_.step_values[i];
         }
     }
     
     // If beyond last interval, use the last value
     if (!params_.step_values.empty() && elapsed_fraction >= params_.step_intervals.back()) {
         return signal.strength * params_.step_values.back();
     }
     
     // Default if something went wrong
     return 0.0;
 }
 
 double SignalDecayModel::conditionalDecay(const TradingSignal& signal, uint64_t current_time) {
     // Start with exponential decay as base
     double remaining_strength = exponentialDecay(signal, current_time);
     
     // Convert signal generation time to milliseconds for comparison
     auto signal_time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(signal.generation_time)
                               .time_since_epoch().count();
     
     // Check if market events should modify decay
     for (const auto& event : recent_market_events_) {
         // Skip events that happened before signal generation
         if (event.timestamp < signal_time_ms) {
             continue;
         }
         
         // Modify remaining strength based on event type
         switch (event.type) {
             case MarketEventType::PRICE_SPIKE:
                 // Price spikes accelerate decay
                 remaining_strength *= 0.7;
                 break;
                 
             case MarketEventType::LARGE_TRADE:
                 // Large trades accelerate decay
                 remaining_strength *= 0.8;
                 break;
                 
             case MarketEventType::LIQUIDITY_CHANGE:
                 // Significant liquidity changes can invalidate signals
                 if (event.data.count("percent_change") > 0) {
                     double percent_change = event.data.at("percent_change");
                     if (std::abs(percent_change) > 20.0) {
                         remaining_strength *= 0.5;
                     }
                 }
                 break;
                 
             case MarketEventType::TREND_CHANGE:
                 // Trend changes can invalidate certain signal types
                 if (signal.source_type == ImbalanceType::TREND_CONTINUATION ||
                     signal.source_type == ImbalanceType::TREND_REVERSAL) {
                     remaining_strength *= 0.3;
                 }
                 break;
                 
             default:
                 // No modification for other event types
                 break;
         }
     }
     
     return remaining_strength;
 }
 
 bool SignalDecayModel::isInvalidatedByMarketEvents(const TradingSignal& signal) {
     // Convert signal generation time to milliseconds for comparison
     auto signal_time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(signal.generation_time)
                               .time_since_epoch().count();
     
     // Check recent market events for invalidating conditions
     for (const auto& event : recent_market_events_) {
         // Skip events that happened before signal generation
         if (event.timestamp < signal_time_ms) {
             continue;
         }
         
         // Check for conditions that would completely invalidate the signal
         switch (event.type) {
             case MarketEventType::PRICE_SPIKE:
                 // Check if price moved significantly against signal direction
                 if (event.data.count("direction") > 0 && event.data.count("magnitude") > 0) {
                     bool spike_is_up = event.data.at("direction") > 0;
                     double magnitude = event.data.at("magnitude");
                     
                     // If large spike in opposite direction, invalidate
                     if ((signal.direction == Direction::BUY && !spike_is_up) ||
                         (signal.direction == Direction::SELL && spike_is_up)) {
                         if (magnitude > 0.5) { // Threshold for significant spike
                             return true;
                         }
                     }
                 }
                 break;
                 
             case MarketEventType::LARGE_TRADE:
                 // Very large trades in opposite direction can invalidate signal
                 if (event.data.count("direction") > 0 && event.data.count("relative_size") > 0) {
                     bool trade_is_buy = event.data.at("direction") > 0;
                     double relative_size = event.data.at("relative_size");
                     
                     // If large trade in opposite direction, invalidate
                     if ((signal.direction == Direction::BUY && !trade_is_buy) ||
                         (signal.direction == Direction::SELL && trade_is_buy)) {
                         if (relative_size > 2.0) { // Very large relative to normal size
                             return true;
                         }
                     }
                 }
                 break;
                 
             case MarketEventType::TREND_CHANGE:
                 // Trend changes invalidate trend-based signals
                 if (signal.source_type == ImbalanceType::TREND_CONTINUATION) {
                     return true;
                 }
                 break;
                 
             default:
                 // No invalidation for other event types
                 break;
         }
     }
     
     return false;
 }
 
 double SignalDecayModel::getTypeSpecificDecayRate(const TradingSignal& signal) {
     auto it = type_specific_decay_rates_.find(signal.source_type);
     if (it != type_specific_decay_rates_.end()) {
         return it->second;
     }
     
     // Default to base decay rate if type-specific rate not found
     return params_.base_decay_rate;
 }
 
 } // namespace strategy
 } // namespace trading