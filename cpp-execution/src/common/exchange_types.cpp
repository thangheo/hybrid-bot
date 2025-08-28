#include "common/exchange_types.hpp"
#include <stdexcept>

namespace trading {
namespace common {

const std::unordered_set<std::string> ExchangeId::supportedExchanges_ = {
    "BINANCE", "COINBASE", "KRAKEN", "FTX", "BITFINEX"
};

ExchangeId ExchangeId::fromString(const std::string& exchangeStr) {
    if (supportedExchanges_.find(exchangeStr) == supportedExchanges_.end()) {
        throw std::invalid_argument("Unsupported exchange: " + exchangeStr);
    }
    return ExchangeId(exchangeStr);
}

//exchangeIdToString
std::unordered_set<ExchangeId> ExchangeId::getAllSupported() {
    std::unordered_set<ExchangeId> result;
    for (const auto& exchange : supportedExchanges_) {
        result.insert(ExchangeId(exchange));
    }
    return result;
}

} // namespace common
} // namespace trading