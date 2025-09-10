/**
 * @file core/types.hpp
 * @brief Common typedefs for the strategy component
 * 
 * Basic type definitions and aliases used throughout the strategy component.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 #include <chrono>
 #include <functional>
 
//  #include <core/confidence.hpp>
// #include <core/exchange_types.hpp>
//     #include <core/market_types.hpp>
//     #include <core/market_state.hpp>
//     #include <core/price_level.hpp>
//     #include <core
 namespace trading {
 namespace core {
 
 using SignalId = std::string;
 using ExperimentId = std::string;
//  using Symbol = std::string;


 /**
  * Time-related typedefs
  */
 using Timestamp = std::chrono::system_clock::time_point;
 using Duration = std::chrono::system_clock::duration;
 /**
  * Common identifier types.
  */
 
/**
 * Struct for strategy identification
 */

struct StrategyId {
    std::string id;
    std::string version;
    // constructors
    StrategyId() : id("UNKNOWN"), version("1.0") {}
    //from string
    StrategyId(const std::string& strategyId, const std::string& ver = "1.0") 
        : id(strategyId), version(ver) {}
    bool operator==(const StrategyId& other) const {
        return id == other.id && version == other.version;
    }
    
    bool operator<(const StrategyId& other) const {
        return (id == other.id) ? (version < other.version) : (id < other.id);
    }
    // add helper to check if valid
    bool isValid() const {
        return !id.empty() && id != "UNKNOWN";
    }
    std::string toString() const {
        return id + ":" + version;
    }
    static StrategyId fromString(const std::string& str) {
        auto pos = str.find(':');
        if (pos != std::string::npos) {
            return StrategyId(str.substr(0, pos), str.substr(pos + 1));
        }
        return StrategyId(str);
    }
    // overload stream operator
    friend std::ostream& operator<<(std::ostream& os, const StrategyId& strategyId) {
        os << strategyId.toString();
        return os;
    }
    //isEmpty
    bool isEmpty() const {
        return id.empty();
    }
  
};

} // namespace core
} // namespace trading