#include "execution/filter/signal_dedup.hpp"
#include "core/trading_signal.hpp"
#include <iostream>

int main() {
    trading::execution::SignalDeduplicator::Config cfg;
    cfg.includeExchangeInKey = true;
    trading::execution::SignalDeduplicator dedup(cfg);
    trading::core::TradingSignal s;
    s.id = "sig-1";
    s.symbol = trading::core::Symbol("BTC","USDT");
    s.direction = trading::core::Direction::BUY;
    s.strength = 0.6;
    s.generationTime = trading::core::now();
    s.ttl = std::chrono::milliseconds(2000);

    bool first = dedup.shouldProcessSignal(s);
    std::cout << "first=" << first << "\n";

    // identical weak duplicate
    trading::core::TradingSignal s2 = s;
    s2.id = "sig-2";
    bool second = dedup.shouldProcessSignal(s2);
    std::cout << "second=" << second << "\n";

    // stronger same direction
    trading::core::TradingSignal s3 = s;
    s3.id = "sig-3";
    s3.strength = 0.75;
    bool third = dedup.shouldProcessSignal(s3);
    std::cout << "third=" << third << "\n";

    return 0;
}
