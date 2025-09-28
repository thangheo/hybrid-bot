// risk/interfaces/market_data_cache.hpp
#include "risk/risk_types.hpp"
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
    //rules of five
    MarketDataCache() = default;
    ~MarketDataCache() = default;
    MarketDataCache(const MarketDataCache&) = delete;
    MarketDataCache& operator=(const MarketDataCache&) = delete;
    MarketDataCache(MarketDataCache&&) = delete;
    MarketDataCache& operator=(MarketDataCache&&) = delete;
    
    void updatePrice(const core::Symbol& symbol, const MarketData& data);
    std::optional<MarketData> getPrice(const core::Symbol& symbol) const;
    bool isStale(const core::Symbol& symbol, std::chrono::milliseconds maxAge) const;
};
} // namespace risk
} // namespace trading