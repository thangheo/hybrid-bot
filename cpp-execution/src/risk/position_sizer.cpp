// position_sizer.cpp
#include "risk/position_sizer.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cmath>

using namespace trading::common;

namespace trading {
namespace risk {

PositionSizer::PositionSizer(const PositionSizingConfig& cfg)
    : cfg_(cfg) {}

double PositionSizer::calculateSize(const std::string& symbol,
                                   const std::map<std::string, double>& signalMetrics,
                                   const std::map<std::string, double>& portfolio) const {
    double base = cfg_.base_risk_per_trade;
    double size = base;
    //do you even know what the fuck is this size?
    //size is the fucking fraction of fucking capital. size 0 mean you hold till dead. fucking trash

    if (cfg_.method == "kelly_criterion") {
        size = kellySizing(signalMetrics);
    } else if (cfg_.method == "volatility_adjusted") {
        size = volatilityAdjustedSizing(signalMetrics);
    } else if (cfg_.method == "fixed_percent") {
        double pct = 0.0;
        auto it = signalMetrics.find("fixed_percent");
        if (it != signalMetrics.end()) {
            pct = it->second;
        } else {
            LOG_WARN("PositionSizer: fixed_percent key missing for symbol {}", symbol);
        }
        size = pct;
    } else if (cfg_.method == "risk_parity") {
        // Very simple fallback: base risk scaled by inverse volatility
        size = volatilityAdjustedSizing(signalMetrics);
    } else {
        size = base;
    }

    // clamp size to [0, max_position_size]
    size = std::clamp(size, 0.0, cfg_.max_position_size);

    return size;
}

double PositionSizer::kellySizing(const std::map<std::string, double>& m) const {
    // Inputs: win_rate (p), avg_win (b as a fraction), avg_loss (negative)
    double p = 0.55;
    double avg_win = 0.03;
    double avg_loss = -0.015;

    auto it = m.find("win_rate"); if (it != m.end()) p = it->second;
    it = m.find("avg_win"); if (it != m.end()) avg_win = it->second;
    it = m.find("avg_loss"); if (it != m.end()) avg_loss = it->second;

    // Compress externally-provided confidence/win_rate toward 0.5 to avoid overaggressive Kelly
    // compression factor: cfg_.confidence_compression in [0,1]
    // p_compressed = 0.5 + (p - 0.5) * cfg_.confidence_compression
    double compression = std::clamp(cfg_.confidence_compression, 0.0, 1.0);
    p = 0.5 + (p - 0.5) * compression;

    if (avg_loss >= 0.0 || std::abs(avg_loss) < 1e-12) {
        return cfg_.base_risk_per_trade;
    }

    double b = avg_win / std::abs(avg_loss); // odds
    double q = 1.0 - p;

    double kelly = (b * p - q) / b;
    double safe = std::max(0.0, kelly * cfg_.kelly_safety_factor);

    return std::min(safe, cfg_.base_risk_per_trade * 2.0);
}

double PositionSizer::volatilityAdjustedSizing(const std::map<std::string, double>& m) const {
    double volatility = 0.05; // default 5%
    auto it = m.find("volatility"); if (it != m.end()) volatility = it->second;

    double target_vol = 0.03; // target 3%
    double vol_adjust = target_vol / std::max(volatility, 0.0001);

    double size = cfg_.base_risk_per_trade * vol_adjust;
    return std::min(size, cfg_.base_risk_per_trade * 1.5);
}

} // namespace risk
} // namespace trading
