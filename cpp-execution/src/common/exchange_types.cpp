#include "common/exchange_types.hpp"
#include <stdexcept>
#include <algorithm>

namespace trading {
namespace common {

const std::unordered_set<std::string>& ExchangeId::supportedExchanges() {
    static const std::unordered_set<std::string> s = {"BINANCE", "COINBASE", "KRAKEN", "FTX", "BITFINEX"};
    return s;
}

ExchangeId ExchangeId::fromString(const std::string& exchangeStr) {
    //try with both lower and upper case
    std::string upperStr = exchangeStr;
    std::transform(upperStr.begin(), upperStr.end(), upperStr.begin(), ::toupper);
    if (supportedExchanges().find(upperStr) != supportedExchanges().end()) {
        return ExchangeId(upperStr);
    }
    if (supportedExchanges().find(exchangeStr) == supportedExchanges().end()) {
        throw std::invalid_argument("Unsupported exchange: " + exchangeStr);
    }
    return ExchangeId(exchangeStr);
}

//exchangeIdToString
std::unordered_set<ExchangeId> ExchangeId::getAllSupported() {
    std::unordered_set<ExchangeId> result;
    for (const auto& exchange : supportedExchanges()) {
        result.insert(ExchangeId(exchange));
    }
    return result;
}

} // namespace common
} // namespace trading