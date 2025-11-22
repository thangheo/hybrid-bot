#include "market_intelligence/adaptive_intelligence.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <google/protobuf/arena.h>

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

CoinProfile* AdaptiveMarketIntelligence::getOrCreateProfile(const core::Symbol& symbol) {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    auto symbolStr = symbol.toString();
    auto it = profiles_.find(symbolStr);
    if (it != profiles_.end()) {
        return it->second.get();
    }
    
    // Create default profile for unknown symbol
    std::string base_symbol = symbolStr.substr(0, symbolStr.find("USDT"));
    profiles_[symbolStr] = std::make_unique<CoinProfile>(symbolStr, "unknown", 2.0);
    return profiles_[symbolStr].get();
}

CoinProfile& AdaptiveMarketIntelligence::GetOrCreateProfile(const core::Symbol& symbol) {
    // Legacy method signature - delegate to new implementation
    return *getOrCreateProfile(symbol);
}


// ============================================================================
// BACKWARD COMPATIBILITY INTERFACE
// ============================================================================

trading::MarketIntelligence AdaptiveMarketIntelligence::GenerateAdaptiveMarketIntelligence(
    const core::Symbol& symbol,
    std::shared_ptr<OrderBook> orderbook) {

    // Get order book data
    auto bids_raw = orderbook->getBids(10);
    auto asks_raw = orderbook->getAsks(10);

    if (bids_raw.empty() || asks_raw.empty()) {
        MarketIntelligence intel;
        intel.set_symbol(symbol.toString());
        intel.set_topic("MARKET_INTEL");
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

    // Get flow monitor from order book
    auto flowMonitor = orderbook->getLiquidityFlowMonitor();

    if (flowMonitor) {
        auto metrics = flowMonitor->getLiquidityMetrics();
        auto flowMetrics = std::make_shared<strategy::LiquidityMetrics>(std::move(metrics));
        return generateIntelligence(symbol, bids, asks, flowMetrics);
    }
    return generateIntelligence(symbol, bids, asks, nullptr);
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

bool AdaptiveMarketIntelligence::hasEnoughData(const core::Symbol& symbol) const {
    std::lock_guard<std::mutex> lock(profiles_mutex_);
    
    auto it = profiles_.find(symbol.toString());
    if (it == profiles_.end()) return false;
    
    return it->second->hasEnoughData();
}

void AdaptiveMarketIntelligence::printStatistics(const core::Symbol& symbol) const {
    std::lock_guard<std::mutex> lock(profiles_mutex_);  // Now works with mutable mutex

    auto it = profiles_.find(symbol.toString());
    if (it == profiles_.end()) {
        std::cout << "No profile found for " << symbol.toString() << std::endl;
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

// OrderBookMetrics AdaptiveMarketIntelligence::CalculateOrderBookMetrics(
//     const std::vector<PriceLevel>& bids,
//     const std::vector<PriceLevel>& asks) {
    
//     // Convert and delegate to new method
//     std::vector<trading::PriceLevel> converted_bids, converted_asks;
    
//     for (const auto& bid : bids) {
//         trading::PriceLevel level;
//         level.set_price(bid.price());  // Add parentheses - it's a method call
//         level.set_quantity(bid.quantity());  // Add parentheses - it's a method call
//         converted_bids.push_back(level);
//     }
    
//     for (const auto& ask : asks) {
//         trading::PriceLevel level;
//         level.set_price(ask.price());  // Add parentheses - it's a method call
//         level.set_quantity(ask.quantity());  // Add parentheses - it's a method call
//         converted_asks.push_back(level);
//     }
    
//     return calculateOrderBookMetrics(converted_bids, converted_asks);
// }


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

double AdaptiveMarketIntelligence::EstimateVolatility(const core::Symbol& symbol, double spread,
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

// New optimized method using pre-calculated OrderBookMetrics
trading::MarketIntelligence AdaptiveMarketIntelligence::generateIntelligenceFromMetrics(
    const core::Symbol& symbol,
    const strategy::OrderBookMetrics& obMetrics,
    std::shared_ptr<strategy::LiquidityMetrics> flowMetrics) {

    google::protobuf::Arena arena;
    auto* ai = google::protobuf::Arena::CreateMessage<trading::MarketIntelligence>(&arena);

    ai->set_symbol(symbol.toString());
    ai->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    // Get profile for adaptive thresholds
    CoinProfile* profile = getOrCreateProfile(symbol);
    if (!profile) {
        trading::MarketIntelligence out;
        out.CopyFrom(*ai);
        return out;
    }

    // Use metrics directly from OrderBookAnalyzer instead of recalculating
    double relative_spread = obMetrics.spreadPercent / 100.0;  // Convert from percent
    double total_volume = obMetrics.totalBidVolume + obMetrics.totalAskVolume;

    // Update historical statistics for adaptive thresholds
    profile->updateStatistics(total_volume, relative_spread);

    double volume_percentile = profile->volume_stats.getVolumePercentile(total_volume);
    double spread_percentile = profile->spread_stats.getSpreadPercentile(relative_spread);

    // Populate intelligence using OrderBookAnalyzer metrics
    ai->set_bid_ask_spread(relative_spread);
    ai->set_order_book_imbalance(obMetrics.volumeImbalance);
    ai->set_liquidity_ratio(volume_percentile);
    ai->set_depth_imbalance(obMetrics.depthImbalance);
    ai->set_spread_stability(obMetrics.getMetric("spreadStability", 0.5));
    ai->set_realized_volatility_1m(obMetrics.realize_volatility_1m);
    ai->set_volume_ratio_to_avg(obMetrics.getMetric("volumeRatioToAvg", 1.0));
    ai->set_volume_5s(obMetrics.getMetric("volume5s", total_volume * 0.2));
    ai->set_volume_30s(obMetrics.getMetric("volume30s", total_volume * 0.8));

    // Use volatility metrics from OrderBookAnalyzer
    ai->set_realized_volatility_1m(obMetrics.realize_volatility_1m);
    ai->set_realized_volatility_5m(obMetrics.realize_volatility_1m * 1.2);
    ai->set_expected_volatility_5m(obMetrics.volatilityExpectation);

    // Flow metrics
    FlowMetrics flow;
    if (flowMetrics) {
        double imbalance_signal = flowMetrics->flow_imbalance;
        if (!std::isfinite(imbalance_signal)) imbalance_signal = obMetrics.volumeImbalance;

        flow.net_flow_5s = imbalance_signal * total_volume * 0.1;
        flow.net_flow_30s = imbalance_signal * total_volume * 0.03;

        double exec_delta = flowMetrics->bid_execution_rate - flowMetrics->ask_execution_rate;
        double cancel_delta = flowMetrics->ask_cancel_rate - flowMetrics->bid_cancel_rate;
        double toxicity_delta = flowMetrics->bid_toxicity - flowMetrics->ask_toxicity;

        double agg = 0.5 + (imbalance_signal * 0.5) + (exec_delta * 0.1) + (cancel_delta * 0.05) - (toxicity_delta * 0.05);
        agg = std::max(0.0, std::min(1.0, agg));
        flow.aggressive_buy_ratio = agg;
        flow.aggressive_sell_ratio = std::max(0.0, std::min(1.0, 1.0 - agg));

        double size_adj = 1.0 + (flowMetrics->large_order_ratio * 0.5) - (flowMetrics->cancellation_ratio * 0.2);
        size_adj = std::max(0.1, size_adj);
        flow.net_flow_5s *= size_adj;
        flow.net_flow_30s *= size_adj;
    } else {
        flow = estimateFlowMetrics(obMetrics.volumeImbalance, total_volume);
    }

    ai->set_aggressive_buy_ratio(flow.aggressive_buy_ratio);
    ai->set_aggressive_sell_ratio(flow.aggressive_sell_ratio);
    ai->set_net_flow_5s(flow.net_flow_5s);
    ai->set_net_flow_30s(flow.net_flow_30s);
    ai->set_net_flow_5m(flow.net_flow_30s * 10.0);
    ai->set_passive_buy_ratio(1.0 - flow.aggressive_buy_ratio);
    ai->set_passive_sell_ratio(1.0 - flow.aggressive_sell_ratio);
    ai->set_flow_momentum(calculateFlowMomentum(flow));

    // Price impact
    double price_impact = estimatePriceImpact(total_volume, relative_spread, profile);
    if (flowMetrics && (flowMetrics->pressure_index > 0.0 || flowMetrics->large_order_ratio > 0.0)) {
        double adj = 1.0 + std::min(2.0, flowMetrics->pressure_index * 0.5 + flowMetrics->large_order_ratio * 0.3);
        price_impact *= adj;
    }
    ai->set_price_impact_estimate(price_impact);

    // Regime classification using adaptive thresholds
    RegimeProbabilities probs = calculateRegimeProbabilities(
        volume_percentile, spread_percentile, obMetrics.volumeImbalance, profile->volatility_factor);
    auto [regime, confidence] = probs.getBestRegime();
    ai->set_current_regime(regime);
    ai->set_regime_confidence(confidence);
    ai->set_regime_stability(calculateRegimeStability(profile, regime));

    // Periodic saving
    update_counter_++;
    if (update_counter_ % save_frequency_ == 0) {
        profile->saveToFile();
    }

    trading::MarketIntelligence out;
    out.CopyFrom(*ai);
    return out;
}

// Legacy method - now delegates to new method after converting bids/asks
trading::MarketIntelligence AdaptiveMarketIntelligence::generateIntelligence(
    const core::Symbol& symbol,
    const std::vector<trading::PriceLevel>& bids,
    const std::vector<trading::PriceLevel>& asks,
    std::shared_ptr<strategy::LiquidityMetrics> flowMetrics) {

    // Use a short-lived protobuf Arena to allocate temporary strings/fields while
    // populating the message. This reduces per-field heap churn because
    // intermediate strings are allocated on the arena and freed together when
    // the arena is destroyed. We then CopyFrom into the returned message to
    // preserve the existing function ABI.
    google::protobuf::Arena arena;
    auto* ai = google::protobuf::Arena::CreateMessage<trading::MarketIntelligence>(&arena);

    ai->set_symbol(symbol.toString());
    ai->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    if (bids.empty() || asks.empty()) {
        trading::MarketIntelligence out;
        out.CopyFrom(*ai);
        return out;
    }

    // Get profile
    CoinProfile* profile = getOrCreateProfile(symbol);
    if (!profile) {
        trading::MarketIntelligence out;
        out.CopyFrom(*ai);
        return out;
    }

    // Calculate basic metrics
    double bestBid = bids[0].price();
    double bestAsk = asks[0].price();
    double spread = bestAsk - bestBid;
    double midPrice = (bestBid + bestAsk) / 2.0;
    double relative_spread = spread / midPrice;
    double volatility = estimateSimpleVolatility(relative_spread, profile->volatility_factor);
    OrderBookMetrics metrics = calculateOrderBookMetrics(bids, asks);

    // Update historical statistics
    profile->updateStatistics(metrics.total_volume, relative_spread);

    double volume_percentile = profile->volume_stats.getVolumePercentile(metrics.total_volume);
    double spread_percentile = profile->spread_stats.getSpreadPercentile(relative_spread);

    // Populate arena-backed message
    ai->set_bid_ask_spread(relative_spread);
    ai->set_order_book_imbalance(metrics.imbalance);
    ai->set_liquidity_ratio(volume_percentile);

    double depth_imbalance = calculateDepthImbalance(bids, asks, 5, 10);
    ai->set_depth_imbalance(depth_imbalance);

    double spread_stability = calculateSpreadStability(profile);
    ai->set_spread_stability(spread_stability);
    ai->set_realized_volatility_1m(volatility);

    double volume_ratio_to_avg = calculateVolumeRatio(metrics.total_volume, profile);
    ai->set_volume_ratio_to_avg(volume_ratio_to_avg);
    ai->set_volume_5s(metrics.total_volume * 0.2);
    ai->set_volume_30s(metrics.total_volume * 0.8);

    double volatility_trend = calculateVolatilityTrend(profile, relative_spread);
    ai->set_volatility_trend(volatility_trend);

    double price_impact = estimatePriceImpact(metrics.total_volume, relative_spread, profile);

    double volatility_1m = estimateSimpleVolatility(relative_spread, profile->volatility_factor);
    ai->set_realized_volatility_1m(volatility_1m);
    ai->set_realized_volatility_5m(volatility_1m * 1.2);
    ai->set_expected_volatility_5m(volatility_1m * 1.1);

    // If we have a richer liquidity monitor available, use it to populate flow metrics
    FlowMetrics flow;
    if (flowMetrics) {
        // Use provided liquidity metrics to form our simplified FlowMetrics
        double imbalance_signal = flowMetrics->flow_imbalance;
        if (!std::isfinite(imbalance_signal)) imbalance_signal = metrics.imbalance;

        // Base net flows on imbalance and observed total volume, scaled by windows
        flow.net_flow_5s = imbalance_signal * metrics.total_volume * 0.1;
        flow.net_flow_30s = imbalance_signal * metrics.total_volume * 0.03;

        // Aggressive ratio derived from execution/cancel/add rates and toxicity
        double exec_delta = flowMetrics->bid_execution_rate - flowMetrics->ask_execution_rate;
        double cancel_delta = flowMetrics->ask_cancel_rate - flowMetrics->bid_cancel_rate;
        double toxicity_delta = flowMetrics->bid_toxicity - flowMetrics->ask_toxicity;

        double agg = 0.5 + (imbalance_signal * 0.5) + (exec_delta * 0.1) + (cancel_delta * 0.05) - (toxicity_delta * 0.05);
        agg = std::max(0.0, std::min(1.0, agg));
        flow.aggressive_buy_ratio = agg;
        flow.aggressive_sell_ratio = std::max(0.0, std::min(1.0, 1.0 - agg));

        // Scale net flow by order size characteristics
        double size_adj = 1.0 + (flowMetrics->large_order_ratio * 0.5) - (flowMetrics->cancellation_ratio * 0.2);
        size_adj = std::max(0.1, size_adj);
        flow.net_flow_5s *= size_adj;
        flow.net_flow_30s *= size_adj;

        ai->set_aggressive_buy_ratio(flow.aggressive_buy_ratio);
        ai->set_aggressive_sell_ratio(flow.aggressive_sell_ratio);
        ai->set_net_flow_5s(flow.net_flow_5s);
        ai->set_net_flow_30s(flow.net_flow_30s);
        ai->set_net_flow_5m(flow.net_flow_30s * 10.0);
        ai->set_passive_buy_ratio(1.0 - flow.aggressive_buy_ratio);
        ai->set_passive_sell_ratio(1.0 - flow.aggressive_sell_ratio);
        ai->set_flow_momentum(flow.net_flow_5s - flow.net_flow_30s);

        // Adjust price impact using pressure index and large order presence
        if (flowMetrics->pressure_index > 0.0 || flowMetrics->large_order_ratio > 0.0) {
            double adj = 1.0 + std::min(2.0, flowMetrics->pressure_index * 0.5 + flowMetrics->large_order_ratio * 0.3);
            price_impact *= adj;
        }
    } else {
        flow = estimateFlowMetrics(metrics.imbalance, metrics.total_volume);
        ai->set_aggressive_buy_ratio(flow.aggressive_buy_ratio);
        ai->set_aggressive_sell_ratio(flow.aggressive_sell_ratio);
        ai->set_net_flow_5s(flow.net_flow_5s);
        ai->set_net_flow_30s(flow.net_flow_30s);
        ai->set_net_flow_5m(flow.net_flow_30s * 10.0);
        ai->set_passive_buy_ratio(1.0 - flow.aggressive_buy_ratio);
        ai->set_passive_sell_ratio(1.0 - flow.aggressive_sell_ratio);
        ai->set_flow_momentum(calculateFlowMomentum(flow));
    }

    ai->set_price_impact_estimate(price_impact);

    RegimeProbabilities probs = calculateRegimeProbabilities(
        volume_percentile, spread_percentile, metrics.imbalance, profile->volatility_factor);
    auto [regime, confidence] = probs.getBestRegime();
    ai->set_current_regime(regime);
    ai->set_regime_confidence(confidence);

    double regime_stability = calculateRegimeStability(profile, regime);
    ai->set_regime_stability(regime_stability);

    // Periodic saving
    update_counter_++;
    if (update_counter_ % save_frequency_ == 0) {
        profile->saveToFile();
    }

    // Copy out of arena-backed temporary into returned message (preserve ABI)
    trading::MarketIntelligence out;
    out.CopyFrom(*ai);
    return out;
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