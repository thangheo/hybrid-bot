#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>

namespace trading {
namespace common {

class ExchangeId {
public:
    // Default constructor for container usage
    ExchangeId() : id_("UNKNOWN") {}
    //for unkown exchange
    static const ExchangeId& unknown() { 
        static const ExchangeId instance("UNKNOWN"); 
        return instance; 
    }
    // Static factory methods for known exchanges
    static const ExchangeId& binance() { 
        static const ExchangeId instance("BINANCE"); 
        return instance; 
    }
    
    static const ExchangeId& coinbase() { 
        static const ExchangeId instance("COINBASE"); 
        return instance; 
    }
    
    static const ExchangeId& kraken() { 
        static const ExchangeId instance("KRAKEN"); 
        return instance; 
    }
    
    static const ExchangeId& ftx() { 
        static const ExchangeId instance("FTX"); 
        return instance; 
    }
    
    static const ExchangeId& bitfinex() { 
        static const ExchangeId instance("BITFINEX"); 
        return instance; 
    }
    //all exchanges
    static const ExchangeId& all() { 
        static const ExchangeId instance("ALL"); 
        return instance; 
    }
    
    // Create from string (validates against known exchanges)
    static ExchangeId fromString(const std::string& exchangeStr);
    
    // Get all supported exchanges
    static std::unordered_set<ExchangeId> getAllSupported();
    
    // Check if valid (not UNKNOWN)
    bool isValid() const { return id_ != "UNKNOWN"; }
    
    // Accessors
    const std::string& toString() const { return id_; }
    
    // Operators
    bool operator==(const ExchangeId& other) const { return id_ == other.id_; }
    bool operator!=(const ExchangeId& other) const { return id_ != other.id_; }
    bool operator<(const ExchangeId& other) const { return id_ < other.id_; }
    
    // Hash support for unordered containers
    struct Hash {
        std::size_t operator()(const ExchangeId& exchangeId) const {
            return std::hash<std::string>{}(exchangeId.id_);
        }
    };

private:
    explicit ExchangeId(const std::string& id) : id_(id) {}
    std::string id_;
    
    static const std::unordered_set<std::string> supportedExchanges_;
};

} // namespace common
} // namespace trading

// Hash specialization for std::unordered_map/set
namespace std {
template<>
struct hash<trading::common::ExchangeId> {
    std::size_t operator()(const trading::common::ExchangeId& exchangeId) const {
        return trading::common::ExchangeId::Hash{}(exchangeId);
    }
};
}