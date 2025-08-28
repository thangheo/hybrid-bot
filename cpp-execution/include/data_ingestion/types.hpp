// data_ingestion/types.hpp
#pragma once
#include "core/market_types.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <optional>
#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>

namespace trading {
namespace data_ingestion {

// The following types are now defined in core/market_types.hpp
// and aliased into the data_ingestion namespace.
// using Timestamp = core::Timestamp;
// using ExchangeId = core::ExchangeId;
// using Symbol = core::Symbol;
// using PriceLevelBase = core::PriceLevel;
// using UpdateType = core::UpdateType;
// using ConnectionStatus = core::ConnectionStatus;
// using ExchangeData = core::ExchangeData;

// Any additional types specific to data_ingestion can be defined here
// but should not conflict with the aliases from market_types.hpp

} // namespace data_ingestion
} // namespace trading
