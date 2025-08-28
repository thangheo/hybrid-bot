// risk/interfaces/market_data_cache.hpp
#include "../risk_types.hpp"
#include <string>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <shared_mutex>
#include <mutex>
namespace trading {
namespace risk {
/**
 * MarketDataCache is a thread-safe cache for market data.
 * It stores the latest market data for various symbols and provides
 * methods to update, retrieve, and check the staleness of the data.
 */

class MarketDataCache {
private:
    std::unordered_map<std::string, MarketData> priceCache_;
    mutable std::shared_mutex cacheMutex_;
    
public:
    void updatePrice(const std::string& symbol, const MarketData& data);
    std::optional<MarketData> getPrice(const std::string& symbol) const;
    bool isStale(const std::string& symbol, std::chrono::milliseconds maxAge) const;
};
} // namespace risk
} // namespace trading