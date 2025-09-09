#include "market_intelligence/adaptive_intelligence.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>

namespace trading {
namespace intelligence {

// ============================================================================
// VolumeStatistics Implementation
// ============================================================================

void VolumeStatistics::updateStatistics() {
    if (volume_history.size() < 10) return;
    
    std::vector<double> sorted_volumes(volume_history.begin(), volume_history.end());
    std::sort(sorted_volumes.begin(), sorted_volumes.end());
    
    size_t size = sorted_volumes.size();
    percentile_10 = sorted_volumes[static_cast<size_t>(size * 0.1)];
    percentile_50 = sorted_volumes[static_cast<size_t>(size * 0.5)];
    percentile_90 = sorted_volumes[static_cast<size_t>(size * 0.9)];
}

void VolumeStatistics::addVolume(double volume) {
    if (volume <= 0) return;  // Skip invalid volumes
    
    volume_history.push_back(volume);
    if (volume_history.size() > max_history_size) {
        volume_history.pop_front();
    }
    updateStatistics();
}

double VolumeStatistics::getVolumePercentile(double current_volume) const {
    if (volume_history.size() < 10) return 0.5;
    
    size_t count_below = 0;
    for (double vol : volume_history) {
        if (vol < current_volume) count_below++;
    }
    return static_cast<double>(count_below) / volume_history.size();
}

void VolumeStatistics::saveToStream(std::ostream& stream) const {
    size_t size = volume_history.size();
    stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
    for (double vol : volume_history) {
        stream.write(reinterpret_cast<const char*>(&vol), sizeof(vol));
    }
}

void VolumeStatistics::loadFromStream(std::istream& stream) {
    size_t size;
    stream.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (size > max_history_size) size = max_history_size;
    
    volume_history.clear();
    for (size_t i = 0; i < size; ++i) {
        double vol;
        stream.read(reinterpret_cast<char*>(&vol), sizeof(vol));
        volume_history.push_back(vol);
    }
    updateStatistics();
}

// ============================================================================
// SpreadStatistics Implementation
// ============================================================================

void SpreadStatistics::updateStatistics() {
    if (spread_history.size() < 10) return;
    
    std::vector<double> sorted_spreads(spread_history.begin(), spread_history.end());
    std::sort(sorted_spreads.begin(), sorted_spreads.end());
    
    size_t size = sorted_spreads.size();
    percentile_25 = sorted_spreads[static_cast<size_t>(size * 0.25)];
    percentile_75 = sorted_spreads[static_cast<size_t>(size * 0.75)];
}

void SpreadStatistics::addSpread(double spread) {
    if (spread <= 0) return;  // Skip invalid spreads
    
    spread_history.push_back(spread);
    if (spread_history.size() > max_history_size) {
        spread_history.pop_front();
    }
    updateStatistics();
}

double SpreadStatistics::getSpreadPercentile(double current_spread) const {
    if (spread_history.size() < 10) return 0.5;
    
    size_t count_below = 0;
    for (double spread : spread_history) {
        if (spread < current_spread) count_below++;
    }
    return static_cast<double>(count_below) / spread_history.size();
}

void SpreadStatistics::saveToStream(std::ostream& stream) const {
    size_t size = spread_history.size();
    stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
    for (double spread : spread_history) {
        stream.write(reinterpret_cast<const char*>(&spread), sizeof(spread));
    }
}

void SpreadStatistics::loadFromStream(std::istream& stream) {
    size_t size;
    stream.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (size > max_history_size) size = max_history_size;
    
    spread_history.clear();
    for (size_t i = 0; i < size; ++i) {
        double spread;
        stream.read(reinterpret_cast<char*>(&spread), sizeof(spread));
        spread_history.push_back(spread);
    }
    updateStatistics();
}

// ============================================================================
// RegimeProbabilities Implementation
// ============================================================================

void RegimeProbabilities::normalize() {
    double total = liquid_prob + normal_prob + thin_prob + volatile_prob;
    if (total > 0) {
        liquid_prob /= total;
        normal_prob /= total;
        thin_prob /= total;
        volatile_prob /= total;
    } else {
        normal_prob = 1.0;  // Default to normal
    }
}

std::pair<trading::OrderBookRegime, double> RegimeProbabilities::getBestRegime() const {
    double max_prob = std::max({liquid_prob, normal_prob, thin_prob, volatile_prob});
    
    if (max_prob == liquid_prob) {
        return {trading::OB_LIQUID, liquid_prob};
    } else if (max_prob == thin_prob) {
        return {trading::OB_THIN, thin_prob};
    } else if (max_prob == volatile_prob) {
        return {trading::OB_VOLATILE, volatile_prob};
    } else {
        return {trading::OB_NORMAL, normal_prob};
    }
}

// ============================================================================
// CoinProfile Implementation
// ============================================================================

CoinProfile::CoinProfile(const std::string& sym, const std::string& tier, double vol_factor) 
    : symbol(sym), market_tier(tier), volatility_factor(vol_factor) {
    persistence_file = "data/adaptive_" + symbol + ".dat";
    
    // Create data directory if it doesn't exist
    std::filesystem::create_directories("data");
    
    loadFromFile();
}

void CoinProfile::saveToFile() const {
    try {
        std::ofstream file(persistence_file, std::ios::binary);
        if (!file) return;
        
        // Save volume statistics
        volume_stats.saveToStream(file);
        
        // Save spread statistics
        spread_stats.saveToStream(file);
        
        // Save metadata
        file.write(reinterpret_cast<const char*>(&volatility_factor), sizeof(volatility_factor));
        
    } catch (const std::exception& e) {
        std::cerr << "Error saving profile for " << symbol << ": " << e.what() << std::endl;
    }
}

void CoinProfile::loadFromFile() {
    try {
        std::ifstream file(persistence_file, std::ios::binary);
        if (!file) return;  // File doesn't exist, start fresh
        
        // Load volume statistics
        volume_stats.loadFromStream(file);
        
        // Load spread statistics
        spread_stats.loadFromStream(file);
        
        // Load metadata
        file.read(reinterpret_cast<char*>(&volatility_factor), sizeof(volatility_factor));
        
    } catch (const std::exception& e) {
        // If loading fails, start fresh
        volume_stats.volume_history.clear();
        spread_stats.spread_history.clear();
    }
}
// Trong CoinProfile struct/class
bool CoinProfile::hasEnoughData() const {
    return volume_stats.volume_history.size() >= 10 &&
           spread_stats.spread_history.size() >= 10;
}
void CoinProfile::updateStatistics(double volume, double spread) {
    volume_stats.addVolume(volume);
    spread_stats.addSpread(spread);
    
    // Update legacy fields for backward compatibility
    sample_count = std::max(volume_stats.volume_history.size(), 
                           spread_stats.spread_history.size());
    
    if (volume_stats.volume_history.size() > 0) {
        volume_ema = volume_stats.percentile_50;  // Use median as EMA approximation
    }
    if (spread_stats.spread_history.size() > 0) {
        spread_ema = (spread_stats.percentile_25 + spread_stats.percentile_75) / 2.0;
    }
}

// ============================================================================
// AdaptiveMarketIntelligence Implementation
// ============================================================================

AdaptiveMarketIntelligence::AdaptiveMarketIntelligence() {
    initializeProfiles();
}

AdaptiveMarketIntelligence::~AdaptiveMarketIntelligence() {
    saveAllProfiles();
}

void AdaptiveMarketIntelligence::initializeProfiles() {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    
    // Tier 1: Major caps
    profiles_["BTCUSDT"] = std::make_unique<CoinProfile>("BTCUSDT", "major", 1.0);
    profiles_["ETHUSDT"] = std::make_unique<CoinProfile>("ETHUSDT", "major", 1.2);
    
    // Tier 2: Large caps  
    profiles_["SOLUSDT"] = std::make_unique<CoinProfile>("SOLUSDT", "large", 1.4);
    profiles_["XRPUSDT"] = std::make_unique<CoinProfile>("XRPUSDT", "large", 1.3);
    profiles_["ADAUSDT"] = std::make_unique<CoinProfile>("ADAUSDT", "alt", 1.6);
    
    // Tier 3: Mid caps
    profiles_["AVAXUSDT"] = std::make_unique<CoinProfile>("AVAXUSDT", "alt", 1.7);
    profiles_["DOTUSDT"] = std::make_unique<CoinProfile>("DOTUSDT", "alt", 1.6);
    profiles_["LINKUSDT"] = std::make_unique<CoinProfile>("LINKUSDT", "alt", 1.5);
    profiles_["UNIUSDT"] = std::make_unique<CoinProfile>("UNIUSDT", "defi", 2.0);
    
    // Additional symbols
    profiles_["ATOMUSDT"] = std::make_unique<CoinProfile>("ATOMUSDT", "alt", 1.8);
    profiles_["ALGOUSDT"] = std::make_unique<CoinProfile>("ALGOUSDT", "alt", 1.9);
    profiles_["AAVEUSDT"] = std::make_unique<CoinProfile>("AAVEUSDT", "defi", 2.2);
    profiles_["ARBUSDT"] = std::make_unique<CoinProfile>("ARBUSDT", "l2", 2.0);
    profiles_["OPUSDT"] = std::make_unique<CoinProfile>("OPUSDT", "l2", 2.1);
    profiles_["INJUSDT"] = std::make_unique<CoinProfile>("INJUSDT", "defi", 2.3);
    profiles_["SUIUSDT"] = std::make_unique<CoinProfile>("SUIUSDT", "l2", 2.0);
    profiles_["TRBUSDT"] = std::make_unique<CoinProfile>("TRBUSDT", "oracle", 2.8);
    profiles_["BANDUSDT"] = std::make_unique<CoinProfile>("BANDUSDT", "oracle", 2.7);
    profiles_["CHZUSDT"] = std::make_unique<CoinProfile>("CHZUSDT", "utility", 2.5);
    profiles_["ENJUSDT"] = std::make_unique<CoinProfile>("ENJUSDT", "utility", 2.4);
    profiles_["YFIUSDT"] = std::make_unique<CoinProfile>("YFIUSDT", "defi", 2.8);
    profiles_["1INCHUSDT"] = std::make_unique<CoinProfile>("1INCHUSDT", "defi", 2.6);
    profiles_["DOGEUSDT"] = std::make_unique<CoinProfile>("DOGEUSDT", "meme", 3.5);
    profiles_["WIFUSDT"] = std::make_unique<CoinProfile>("WIFUSDT", "meme", 4.0);
}

void AdaptiveMarketIntelligence::InitializeDefaultProfiles() {
    // Legacy method name - delegate to new implementation
    initializeProfiles();
}

CoinProfile* AdaptiveMarketIntelligence::getOrCreateProfile(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    
    auto it = profiles_.find(symbol);
    if (it != profiles_.end()) {
        return it->second.get();
    }
    
    // Create default profile for unknown symbol
    std::string base_symbol = symbol.substr(0, symbol.find("USDT"));
    profiles_[symbol] = std::make_unique<CoinProfile>(symbol, "unknown", 2.0);
    return profiles_[symbol].get();
}

CoinProfile& AdaptiveMarketIntelligence::GetOrCreateProfile(const std::string& symbol) {
    // Legacy method signature - delegate to new implementation
    return *getOrCreateProfile(symbol);
}

// ============================================================================
// NEW PRIMARY INTERFACE
// ============================================================================

// trading::MarketIntelligence AdaptiveMarketIntelligence::generateIntelligence(
//     const std::string& symbol,
//     const std::vector<trading::PriceLevel>& bids,
//     const std::vector<trading::PriceLevel>& asks) {
    
//     MarketIntelligence intel;
//     intel.set_symbol(symbol);
//     intel.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
//         std::chrono::system_clock::now().time_since_epoch()).count());
    
//     if (bids.empty() || asks.empty()) return intel;
    
//     // Get profile
//     CoinProfile* profile = getOrCreateProfile(symbol);
//     if (!profile) return intel;
    
//     // Calculate basic metrics
//     double bestBid = bids[0].price();
//     double bestAsk = asks[0].price();
//     double spread = bestAsk - bestBid;
//     double midPrice = (bestBid + bestAsk) / 2.0;
//     double relative_spread = spread / midPrice;
    
//     // Calculate volume metrics
//     OrderBookMetrics metrics = calculateOrderBookMetrics(bids, asks);
    
//     // Update historical statistics
//     profile->updateStatistics(metrics.total_volume, relative_spread);
    
//     // Calculate percentile-based metrics
//     double volume_percentile = profile->volume_stats.getVolumePercentile(metrics.total_volume);
//     double spread_percentile = profile->spread_stats.getSpreadPercentile(relative_spread);
    
//     // Set basic intelligence fields
//     intel.set_bid_ask_spread(relative_spread);
//     intel.set_order_book_imbalance(metrics.imbalance);
//     intel.set_liquidity_ratio(volume_percentile);
    
//     // Estimate flow metrics
//     FlowMetrics flow = estimateFlowMetrics(metrics.imbalance, metrics.total_volume);
//     intel.set_aggressive_buy_ratio(flow.aggressive_buy_ratio);
//     intel.set_aggressive_sell_ratio(flow.aggressive_sell_ratio);
//     intel.set_net_flow_5s(flow.net_flow_5s);
//     intel.set_net_flow_30s(flow.net_flow_30s);
    
//     // Volatility estimation
//     double volatility = estimateSimpleVolatility(relative_spread, profile->volatility_factor);
//     intel.set_realized_volatility_1m(volatility);
    
//     // Regime classification using probabilities
//     RegimeProbabilities probs = calculateRegimeProbabilities(
//         volume_percentile, spread_percentile, metrics.imbalance, profile->volatility_factor);
    
//     auto [regime, confidence] = probs.getBestRegime();
//     intel.set_current_regime(regime);
//     intel.set_regime_confidence(confidence);
    
//     // Periodic saving
//     update_counter_++;
//     if (update_counter_ % save_frequency_ == 0) {
//         profile->saveToFile();
//     }
    
//     return intel;
// }

// ============================================================================
// BACKWARD COMPATIBILITY INTERFACE
// ============================================================================

trading::MarketIntelligence AdaptiveMarketIntelligence::GenerateAdaptiveMarketIntelligence(
    const std::string& symbol,
    std::shared_ptr<OrderBook> orderbook) {
    
    // Get order book data
    auto bids_raw = orderbook->getBids(10);
    auto asks_raw = orderbook->getAsks(10);
    
    if (bids_raw.empty() || asks_raw.empty()) {
        MarketIntelligence intel;
        intel.set_symbol(symbol);
        return intel;
    }
    
    // Convert to new format
    std::vector<trading::PriceLevel> bids, asks;
    
    for (const auto& bid : bids_raw) {
        trading::PriceLevel level;
        level.set_price(bid.price);
        level.set_quantity(bid.quantity);
        bids.push_back(level);
    }
    
    for (const auto& ask : asks_raw) {
        trading::PriceLevel level;
        level.set_price(ask.price);
        level.set_quantity(ask.quantity);
        asks.push_back(level);
    }
    
    // Delegate to new implementation
    return generateIntelligence(symbol, bids, asks);
}

// ============================================================================
// UTILITY METHODS
// ============================================================================

OrderBookMetrics AdaptiveMarketIntelligence::calculateOrderBookMetrics(
    const std::vector<trading::PriceLevel>& bids,
    const std::vector<trading::PriceLevel>& asks) const {
    
    OrderBookMetrics metrics;
    
    // Calculate volumes at top 5 levels
    for (size_t i = 0; i < std::min(size_t(5), bids.size()); ++i) {
        metrics.bid_volume_5 += bids[i].quantity();
    }
    
    for (size_t i = 0; i < std::min(size_t(5), asks.size()); ++i) {
        metrics.ask_volume_5 += asks[i].quantity();
    }
    
    metrics.total_volume = metrics.bid_volume_5 + metrics.ask_volume_5;
    
    // Surface imbalance
    if (metrics.total_volume > 0) {
        metrics.imbalance = (metrics.bid_volume_5 - metrics.ask_volume_5) / metrics.total_volume;
    }
    
    return metrics;
}

RegimeProbabilities AdaptiveMarketIntelligence::calculateRegimeProbabilities(
    double volume_percentile,
    double spread_percentile, 
    double imbalance,
    double volatility_factor) const {
    
    RegimeProbabilities probs;
    
    // Volume-based probabilities
    if (volume_percentile > 0.8) {
        probs.liquid_prob += 0.4;
    } else if (volume_percentile < 0.2) {
        probs.thin_prob += 0.4;
    } else {
        probs.normal_prob += 0.3;
    }
    
    // Spread-based probabilities
    if (spread_percentile < 0.3) { // Tight spread
        probs.liquid_prob += 0.3;
    } else if (spread_percentile > 0.7) { // Wide spread
        probs.thin_prob += 0.3;
    } else {
        probs.normal_prob += 0.2;
    }
    
    // Imbalance-based probabilities
    double abs_imbalance = std::abs(imbalance);
    if (abs_imbalance > 0.4) {
        probs.volatile_prob += 0.4;
    } else if (abs_imbalance < 0.1) {
        probs.liquid_prob += 0.2;
        probs.normal_prob += 0.1;
    }
    
    // Volatility factor adjustment
    if (volatility_factor > 2.5) {
        probs.volatile_prob += 0.2;
        probs.thin_prob += 0.1;
    } else if (volatility_factor < 1.5) {
        probs.liquid_prob += 0.1;
        probs.normal_prob += 0.1;
    }
    
    probs.normalize();
    return probs;
}

double AdaptiveMarketIntelligence::calculateLiquidityRatio(double volume, const CoinProfile& profile) const {
    // Use percentile-based calculation
    return profile.volume_stats.getVolumePercentile(volume);
}

double AdaptiveMarketIntelligence::estimateSimpleVolatility(double spread, double volatility_factor) const {
    return spread * volatility_factor * 5.0;  // Simple volatility estimate
}

FlowMetrics AdaptiveMarketIntelligence::estimateFlowMetrics(double imbalance, double total_volume) const {
    FlowMetrics flow;
    
    flow.net_flow_5s = imbalance * total_volume * 0.1;
    flow.net_flow_30s = imbalance * total_volume * 0.05;
    flow.aggressive_buy_ratio = std::max(0.0, std::min(1.0, 0.5 + imbalance * 0.4));
    flow.aggressive_sell_ratio = std::max(0.0, std::min(1.0, 0.5 - imbalance * 0.4));
    
    return flow;
}

// ============================================================================
// UTILITY AND DEBUG METHODS
// ============================================================================
bool AdaptiveMarketIntelligence::hasEnoughData() const {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    
    for (const auto& [symbol, profile] : profiles_) {
        if (!profile->hasEnoughData()) return false;
    }
    return true;
}

bool AdaptiveMarketIntelligence::hasEnoughData(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    
    auto it = profiles_.find(symbol);
    if (it == profiles_.end()) return false;
    
    return it->second->hasEnoughData();
}

void AdaptiveMarketIntelligence::printStatistics(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(profiles_mutex_);  // Now works with mutable mutex
    
    auto it = profiles_.find(symbol);
    if (it == profiles_.end()) {
        std::cout << "No profile found for " << symbol << std::endl;
        return;
    }
    
    const CoinProfile* profile = it->second.get();
    std::cout << "\n=== " << symbol << " Statistics ===" << std::endl;
    std::cout << "Market Tier: " << profile->market_tier << std::endl;
    std::cout << "Volatility Factor: " << profile->volatility_factor << std::endl;
    std::cout << "Volume history size: " << profile->volume_stats.volume_history.size() << std::endl;
    std::cout << "Volume P10/P50/P90: " << profile->volume_stats.percentile_10 
              << "/" << profile->volume_stats.percentile_50 
              << "/" << profile->volume_stats.percentile_90 << std::endl;
    std::cout << "Spread history size: " << profile->spread_stats.spread_history.size() << std::endl;
    std::cout << "Spread P25/P75: " << profile->spread_stats.percentile_25 
              << "/" << profile->spread_stats.percentile_75 << std::endl;
    std::cout << "Has enough data: " << (profile->hasEnoughData() ? "YES" : "NO") << std::endl;
    std::cout << "===============================" << std::endl;
}


void AdaptiveMarketIntelligence::saveAllProfiles() {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    
    for (auto& [symbol, profile] : profiles_) {
        profile->saveToFile();
    }
}

// ============================================================================
// LEGACY METHODS FOR BACKWARD COMPATIBILITY
// ============================================================================

OrderBookMetrics AdaptiveMarketIntelligence::CalculateOrderBookMetrics(
    const std::vector<PriceLevel>& bids,
    const std::vector<PriceLevel>& asks) {
    
    // Convert and delegate to new method
    std::vector<trading::PriceLevel> converted_bids, converted_asks;
    
    for (const auto& bid : bids) {
        trading::PriceLevel level;
        level.set_price(bid.price());  // Add parentheses - it's a method call
        level.set_quantity(bid.quantity());  // Add parentheses - it's a method call
        converted_bids.push_back(level);
    }
    
    for (const auto& ask : asks) {
        trading::PriceLevel level;
        level.set_price(ask.price());  // Add parentheses - it's a method call
        level.set_quantity(ask.quantity());  // Add parentheses - it's a method call
        converted_asks.push_back(level);
    }
    
    return calculateOrderBookMetrics(converted_bids, converted_asks);
}


double AdaptiveMarketIntelligence::CalculateAdaptiveLiquidityRatio(double volume, const CoinProfile& profile) {
    return calculateLiquidityRatio(volume, profile);
}

void AdaptiveMarketIntelligence::DebugSampleCounts() {
    std::cout << "\n=== ADAPTIVE INTELLIGENCE DEBUG ===" << std::endl;
    
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    for (const auto& [symbol, profile] : profiles_) {
        std::cout << symbol << ":" << std::endl;
        std::cout << "  Volume history: " << profile->volume_stats.volume_history.size() << std::endl;
        std::cout << "  Spread history: " << profile->spread_stats.spread_history.size() << std::endl;
        std::cout << "  Has enough data: " << (profile->hasEnoughData() ? "YES" : "NO") << std::endl;
        std::cout << "  Legacy sample count: " << profile->sample_count << std::endl;
        std::cout << "  ---" << std::endl;
    }
    std::cout << "======================================" << std::endl;
}

void AdaptiveMarketIntelligence::ForceAdaptiveMode() {
    std::cout << "🚨 FORCING SUFFICIENT DATA FOR ALL SYMBOLS" << std::endl;
    
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    for (auto& [symbol, profile] : profiles_) {
        // Add dummy data if insufficient
        while (profile->volume_stats.volume_history.size() < 50) {
            profile->volume_stats.addVolume(1000000 * (1.0 + (rand() % 100) / 100.0));
        }
        while (profile->spread_stats.spread_history.size() < 50) {
            profile->spread_stats.addSpread(0.001 * (1.0 + (rand() % 50) / 100.0));
        }
        
        profile->sample_count = std::max(profile->volume_stats.volume_history.size(),
                                        profile->spread_stats.spread_history.size());
        
        std::cout << "  " << symbol << " - Forced sufficient data" << std::endl;
    }
}

void AdaptiveMarketIntelligence::ApplyEmergencyStaticThresholds() {
    std::cout << "🚨 Note: This method is deprecated in the new implementation" << std::endl;
    std::cout << "   The new system uses dynamic percentile-based thresholds" << std::endl;
}

// Placeholder implementations for other legacy methods
void AdaptiveMarketIntelligence::UpdateProfile(CoinProfile& profile, double volume, double spread, double depth) {
    profile.updateStatistics(volume, spread);
}

void AdaptiveMarketIntelligence::AdaptThresholds(CoinProfile& profile) {
    // No-op in new implementation - thresholds are always adaptive
}

double AdaptiveMarketIntelligence::EstimateVolatility(const std::string& symbol, double spread,
                     std::shared_ptr<trading::data_ingestion::OrderBook> orderbook) {
    auto profile = getOrCreateProfile(symbol);
    return estimateSimpleVolatility(spread, profile->volatility_factor);
}

FlowMetrics AdaptiveMarketIntelligence::AnalyzeFlowMetrics(double imbalance, double volatility) {
    return estimateFlowMetrics(imbalance, volatility * 1000);  // Rough volume estimate
}

RegimeResult AdaptiveMarketIntelligence::ClassifyOrderBookRegimeAdaptive(
    const OrderBookMetrics& metrics, double spread, const CoinProfile& profile) {
    
    // Use new probability-based classification
    double volume_percentile = profile.volume_stats.getVolumePercentile(metrics.total_volume);
    double spread_percentile = profile.spread_stats.getSpreadPercentile(spread);
    
    RegimeProbabilities probs = calculateRegimeProbabilities(
        volume_percentile, spread_percentile, metrics.imbalance, profile.volatility_factor);
    
    auto [regime, confidence] = probs.getBestRegime();
    
    RegimeResult result;
    result.regime = regime;
    result.confidence = confidence;
    return result;
}

std::vector<trading::PriceLevel> AdaptiveMarketIntelligence::ConvertPriceLevels(
    const std::vector<trading::data_ingestion::PriceLevel>& input) {
    
    std::vector<trading::PriceLevel> result;
    result.reserve(input.size());
    
    for (const auto& level : input) {
        trading::PriceLevel converted;
        converted.set_price(level.price);
        converted.set_quantity(level.quantity);
        result.push_back(converted);
    }
    
    return result;
}
trading::MarketIntelligence AdaptiveMarketIntelligence::generateIntelligence(
    const std::string& symbol,
    const std::vector<trading::PriceLevel>& bids,
    const std::vector<trading::PriceLevel>& asks) {
    
    MarketIntelligence intel;
    intel.set_symbol(symbol);
    intel.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    if (bids.empty() || asks.empty()) return intel;
    
    // Get profile
    CoinProfile* profile = getOrCreateProfile(symbol);
    if (!profile) return intel;
    
    // Calculate basic metrics
    double bestBid = bids[0].price();
    double bestAsk = asks[0].price();
    double spread = bestAsk - bestBid;
    double midPrice = (bestBid + bestAsk) / 2.0;
    double relative_spread = spread / midPrice;
    // volatility estimate
    double volatility = estimateSimpleVolatility(relative_spread, profile->volatility_factor);
    // Calculate volume metrics
    OrderBookMetrics metrics = calculateOrderBookMetrics(bids, asks);
    
    // Update historical statistics
    profile->updateStatistics(metrics.total_volume, relative_spread);
    
    // Calculate percentile-based metrics
    double volume_percentile = profile->volume_stats.getVolumePercentile(metrics.total_volume);
    double spread_percentile = profile->spread_stats.getSpreadPercentile(relative_spread);
    
    // === SET ALL BASIC FIELDS ===
    intel.set_bid_ask_spread(relative_spread);
    intel.set_order_book_imbalance(metrics.imbalance);
    intel.set_liquidity_ratio(volume_percentile);
    
    // === NEW: SET MISSING CRITICAL FIELDS ===
    
    // Depth imbalance (imbalance in deeper levels 6-10)
    double depth_imbalance = calculateDepthImbalance(bids, asks, 5, 10);
    intel.set_depth_imbalance(depth_imbalance);  // Field 5
    
    // Spread stability (how stable the spread has been)
    double spread_stability = calculateSpreadStability(profile);
    intel.set_spread_stability(spread_stability);  // Field 7
    intel.set_realized_volatility_1m(volatility);  // Field 15
    // Volume analysis
    double volume_ratio_to_avg = calculateVolumeRatio(metrics.total_volume, profile);
    intel.set_volume_ratio_to_avg(volume_ratio_to_avg);  // Field 21
    intel.set_volume_5s(metrics.total_volume * 0.2);     // Field 19 - estimated
    intel.set_volume_30s(metrics.total_volume * 0.8);    // Field 20 - estimated
    
    // === CRITICAL FIX: SET volatility_trend ===
    double volatility_trend = calculateVolatilityTrend(profile, relative_spread);
    intel.set_volatility_trend(volatility_trend);  // Field 18 - REQUIRED BY PYTHON!
    
    // Price impact estimate
    double price_impact = estimatePriceImpact(metrics.total_volume, relative_spread, profile);
    intel.set_price_impact_estimate(price_impact);  // Field 17
    
    // Extended volatility metrics
    double volatility_1m = estimateSimpleVolatility(relative_spread, profile->volatility_factor);
    intel.set_realized_volatility_1m(volatility_1m);     // Field 15
    intel.set_realized_volatility_5m(volatility_1m * 1.2); // Field 16 - estimated
    intel.set_expected_volatility_5m(volatility_1m * 1.1); // Field 25 - estimated
    
    // Flow metrics
    FlowMetrics flow = estimateFlowMetrics(metrics.imbalance, metrics.total_volume);
    intel.set_aggressive_buy_ratio(flow.aggressive_buy_ratio);   // Field 11
    intel.set_aggressive_sell_ratio(flow.aggressive_sell_ratio); // Field 12
    intel.set_net_flow_5s(flow.net_flow_5s);                    // Field 8
    intel.set_net_flow_30s(flow.net_flow_30s);                  // Field 9
    
    // Extended flow metrics
    intel.set_net_flow_5m(flow.net_flow_30s * 10.0);            // Field 10 - estimated
    intel.set_passive_buy_ratio(1.0 - flow.aggressive_buy_ratio); // Field 13
    intel.set_passive_sell_ratio(1.0 - flow.aggressive_sell_ratio); // Field 14
    intel.set_flow_momentum(calculateFlowMomentum(flow));        // Field 26
    
    // Regime classification
    RegimeProbabilities probs = calculateRegimeProbabilities(
        volume_percentile, spread_percentile, metrics.imbalance, profile->volatility_factor);
    
    auto [regime, confidence] = probs.getBestRegime();
    intel.set_current_regime(regime);           // Field 22
    intel.set_regime_confidence(confidence);    // Field 23 - CRITICAL FOR PYTHON!
    
    // Regime stability
    double regime_stability = calculateRegimeStability(profile, regime);
    intel.set_regime_stability(regime_stability); // Field 24
    
    // Periodic saving
    update_counter_++;
    if (update_counter_ % save_frequency_ == 0) {
        profile->saveToFile();
    }
    
    return intel;
}

// ============================================================================
// NEW HELPER METHODS TO IMPLEMENT
// ============================================================================

double AdaptiveMarketIntelligence::calculateDepthImbalance(
    const std::vector<trading::PriceLevel>& bids,
    const std::vector<trading::PriceLevel>& asks,
    size_t start_level, size_t end_level) const {
    
    double bid_depth = 0.0, ask_depth = 0.0;
    
    for (size_t i = start_level; i < std::min(end_level, bids.size()); ++i) {
        bid_depth += bids[i].quantity();
    }
    for (size_t i = start_level; i < std::min(end_level, asks.size()); ++i) {
        ask_depth += asks[i].quantity();
    }
    
    double total_depth = bid_depth + ask_depth;
    return (total_depth > 0) ? (bid_depth - ask_depth) / total_depth : 0.0;
}

double AdaptiveMarketIntelligence::calculateSpreadStability(CoinProfile* profile) const {
    if (profile->spread_stats.spread_history.size() < 10) {
        return 0.5; // Default stability
    }
    
    // Calculate coefficient of variation of recent spreads
    auto& spreads = profile->spread_stats.spread_history;
    double sum = 0.0, sum_sq = 0.0;
    size_t count = 0;
    
    // Use last 20 spreads
    auto it = spreads.rbegin();
    for (size_t i = 0; i < 20 && it != spreads.rend(); ++i, ++it) {
        sum += *it;
        sum_sq += (*it) * (*it);
        count++;
    }
    
    if (count < 5) return 0.5;
    
    double mean = sum / count;
    double variance = (sum_sq / count) - (mean * mean);
    double std_dev = std::sqrt(variance);
    
    // Stability = 1 - coefficient_of_variation (capped at 1.0)
    double cv = (mean > 0) ? (std_dev / mean) : 0.0;
    return std::max(0.0, std::min(1.0, 1.0 - cv));
}

double AdaptiveMarketIntelligence::calculateVolatilityTrend(CoinProfile* profile, double current_spread) const {
    // CRITICAL: This field is REQUIRED by Python
    if (profile->spread_stats.spread_history.size() < 5) {
        return current_spread * profile->volatility_factor; // Default
    }
    
    // Simple trend: compare current spread to recent average
    auto& spreads = profile->spread_stats.spread_history;
    double recent_avg = 0.0;
    size_t count = 0;
    
    auto it = spreads.rbegin();
    for (size_t i = 0; i < 10 && it != spreads.rend(); ++i, ++it) {
        recent_avg += *it;
        count++;
    }
    
    if (count > 0) {
        recent_avg /= count;
        // Return trend as ratio: current/recent_avg
        return (recent_avg > 0) ? (current_spread / recent_avg) : 1.0;
    }
    
    return current_spread * profile->volatility_factor * 0.1; // Fallback
}

double AdaptiveMarketIntelligence::calculateVolumeRatio(double current_volume, CoinProfile* profile) const {
    if (profile->volume_stats.volume_history.empty()) {
        return 1.0; // Default ratio
    }
    
    // Compare current volume to median historical volume
    double median_volume = profile->volume_stats.percentile_50;
    return (median_volume > 0) ? (current_volume / median_volume) : 1.0;
}

double AdaptiveMarketIntelligence::estimatePriceImpact(double volume, double spread, CoinProfile* profile) const {
    // Simple price impact estimate: spread * volume_factor * volatility_factor
    double base_impact = spread * profile->volatility_factor * 0.1;
    
    // Adjust for volume (higher volume = higher impact)
    double volume_factor = 1.0;
    if (volume > profile->volume_stats.percentile_90) {
        volume_factor = 1.5;
    } else if (volume < profile->volume_stats.percentile_10) {
        volume_factor = 0.5;
    }
    
    return base_impact * volume_factor;
}

double AdaptiveMarketIntelligence::calculateFlowMomentum(const FlowMetrics& flow) const {
    // Flow momentum = difference between short-term and long-term flow
    return flow.net_flow_5s - flow.net_flow_30s;
}

double AdaptiveMarketIntelligence::calculateRegimeStability(CoinProfile* profile, trading::OrderBookRegime current_regime) const {
    // For now, return moderate stability
    // In full implementation, this would track regime changes over time
    return 0.7; // Moderate stability
}
} // namespace intelligence
} // namespace trading