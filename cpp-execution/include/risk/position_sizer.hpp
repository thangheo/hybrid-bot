// position_sizer.hpp
#pragma once

#include "risk_config.hpp"
#include <string>
#include <map>

namespace trading {
namespace risk {

class PositionSizer {
public:
    explicit PositionSizer(const PositionSizingConfig& cfg = PositionSizingConfig());

    // Calculate position size (fraction of portfolio, 0..1)
    // symbol: trading pair like "BTCUSDT"
    // signalMetrics: metrics like win_rate, avg_win, avg_loss, volatility, confidence
    // portfolio: map with keys like total_value_usdt, cash_usdt (optional)
    double calculateSize(const std::string& symbol,
                         const std::map<std::string, double>& signalMetrics,
                         const std::map<std::string, double>& portfolio) const;

    // Expose config
    const PositionSizingConfig& config() const { return cfg_; }

private:
    PositionSizingConfig cfg_;

    double kellySizing(const std::map<std::string, double>& m) const;
    double volatilityAdjustedSizing(const std::map<std::string, double>& m) const;
};

} // namespace risk
} // namespace trading
