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
// JSON support for StrategyId serialization
#include <nlohmann/json.hpp>
 
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

// nlohmann::json support for StrategyId (serializes as string "id:version")
namespace nlohmann {
template<>
struct adl_serializer<trading::core::StrategyId> {
    static trading::core::StrategyId from_json(const nlohmann::json& j) {
        if (j.is_string()) return trading::core::StrategyId::fromString(j.get<std::string>());
        // If object with id/version provided
        if (j.is_object()) {
            std::string id = j.value("id", "UNKNOWN");
            std::string ver = j.value("version", "1.0");
            return trading::core::StrategyId(id, ver);
        }
        return trading::core::StrategyId();
    }

    static void to_json(nlohmann::json& j, const trading::core::StrategyId& s) {
        j = s.toString();
    }
};
} // namespace nlohmann