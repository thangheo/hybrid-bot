#include "risk/interfaces/market_data_processor.hpp"
#include "common/logger.hpp"
// #include "internal_orderbook.pb.h"
#include "../generated/cpp/internal_oderbook.pb.h"
#include <google/protobuf/message.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <algorithm>
#include <sstream>
#include <communication/zmq_server_client.hpp>
using namespace trading::communication;

namespace trading {
namespace risk {

// constexpr const char* ORDERBOOK_UPDATES_TOPIC = "orderbook.*";

MarketDataProcessor::MarketDataProcessor(const ProcessorConfig& config)
    : config_(config) {
    LOG_INFO("MarketDataProcessor initialized with max message size: {} bytes", 
             config_.maxMessageSize);
}

std::optional<core::MarketData> MarketDataProcessor::processMessage(
    const std::string& topic, const std::string& message) {
    // Entry log for diagnostics
    // LOG_DEBUG("Entering processMessage: topic='{}', message_size={}", topic, message.size());

    // Basic validation
    if (!isValidTopic(topic) || !isValidMessageSize(message)) {
        // LOG_DEBUG("Exiting processMessage: invalid topic/size for topic='{}', size={}", topic, message.size());
        return std::nullopt;
    }

    totalProcessed_.fetch_add(1, std::memory_order_relaxed);

    // Try protobuf first (more efficient), then JSON
    try {
        if (config_.enableProtobufProcessing) {
            // LOG_DEBUG("Attempting protobuf parse...");
            auto result = processProtobufOrderbook(topic, message);
            if (result.has_value()) {
                // LOG_INFO("processMessage END: protobuf success for topic='{}'", topic);
                return result;
            }
        }
        
        if (config_.enableJsonProcessing) {
            // LOG_DEBUG("Attempting JSON parse...");
            auto result = processJsonOrderbook(topic, message);
            // LOG_INFO("processMessage END: json {} for topic='{}'", 
            //          result.has_value() ? "success" : "failed", topic);
            return result;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("processMessage exception: {}", e.what());
    }

    LOG_WARN("No enabled processors for message on topic: {}", topic);
    LOG_DEBUG("Exiting processMessage: topic='{}', returned=0 (no processors enabled)", topic);
    return std::nullopt;
}

std::optional<core::MarketData> MarketDataProcessor::processJsonOrderbook(
    const std::string& topic, const std::string& message) {
    
    try {
        if (message.empty()) {
            LOG_ERROR("Empty JSON message for topic: {}", topic);
            ++errorStats_.jsonParseErrors;
            return std::nullopt;
        }

        nlohmann::json json_msg;
        try {
            json_msg = nlohmann::json::parse(message);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse outer JSON for topic {}: {}", topic, e.what());
            jsonParseErrors_.fetch_add(1, std::memory_order_relaxed);
            recordError();
            return std::nullopt;
        }

        // Handle embedded JSON data
        if (json_msg.contains("data") && json_msg["data"].is_string()) {
            try {
                const auto& inner_data = json_msg["data"].get_ref<const std::string&>();
                if (!inner_data.empty() && inner_data.front() == '{') {
                    json_msg = nlohmann::json::parse(inner_data);
                }
            } catch (const std::exception& e) {
                LOG_WARN("Failed to parse inner 'data' JSON for topic {}: {}", topic, e.what());
                // Continue with outer json_msg
            }
        }

        // Extract exchange and symbol
        auto [exchange, symbol] = extractExchangeSymbol(topic);
        
        // Override with JSON values if available
        if (json_msg.contains("exchange") && json_msg["exchange"].is_string()) {
            exchange = json_msg["exchange"].get<std::string>();
        }
        if (json_msg.contains("symbol") && json_msg["symbol"].is_string()) {
            symbol = json_msg["symbol"].get<std::string>();
        }

        if (exchange.empty() || symbol.empty()) {
            LOG_ERROR("Missing exchange or symbol for topic: {}", topic);
            ++errorStats_.invalidTopics;
            return std::nullopt;
        }

        // Extract best bid/ask prices
        std::optional<double> bestBid;
        std::optional<double> bestAsk;

        // Check for bids
        if (json_msg.contains("bids") && json_msg["bids"].is_array() && !json_msg["bids"].empty()) {
            bestBid = extractPriceFromLevel(json_msg["bids"][0]);
        } else if (json_msg.contains("b") && json_msg["b"].is_array() && !json_msg["b"].empty()) {
            bestBid = extractPriceFromLevel(json_msg["b"][0]);
        }

        // Check for asks
        if (json_msg.contains("asks") && json_msg["asks"].is_array() && !json_msg["asks"].empty()) {
            bestAsk = extractPriceFromLevel(json_msg["asks"][0]);
        } else if (json_msg.contains("a") && json_msg["a"].is_array() && !json_msg["a"].empty()) {
            bestAsk = extractPriceFromLevel(json_msg["a"][0]);
        }

        // Get price from JSON if available
        double price = 0.0;
        if (json_msg.contains("price") && json_msg["price"].is_number()) {
            price = json_msg["price"].get<double>();
        }

        return createMarketData(exchange, symbol, 
                               bestBid.value_or(0.0), 
                               bestAsk.value_or(0.0), 
                               price);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in processJsonOrderbook for topic {}: {}", topic, e.what());
    jsonParseErrors_.fetch_add(1, std::memory_order_relaxed);
    recordError();
        return std::nullopt;
    }
}

std::optional<core::MarketData> MarketDataProcessor::processProtobufOrderbook(
    const std::string& topic, const std::string& message) {
    try {
        // LOG_DEBUG("processProtobufOrderbook: entry topic='{}', size={}", topic, message.size());
        if (message.empty()) {
            LOG_ERROR("Empty protobuf message for topic: {}", topic);
            ++errorStats_.protobufParseErrors;
            // LOG_DEBUG("processProtobufOrderbook: exiting early empty message for topic='{}'", topic);
            return std::nullopt;
        }

        orderbook::OrderbookMessage orderbook_msg;
        // LOG_DEBUG("Protobuf parse start: topic='{}', size={}", topic, message.size());
        try {
            // Use CodedInputStream for better memory control
            google::protobuf::io::ArrayInputStream array_input(
                message.data(), static_cast<int>(message.size()));
            google::protobuf::io::CodedInputStream coded_input(&array_input);
            
            coded_input.SetTotalBytesLimit(static_cast<int>(config_.maxMessageSize));
            
            if (!orderbook_msg.ParseFromCodedStream(&coded_input)) {
                LOG_ERROR("Failed to parse protobuf message for topic: {}", topic);
                protobufParseErrors_.fetch_add(1, std::memory_order_relaxed);
                recordError();
                LOG_DEBUG("Protobuf parse failed for topic='{}'", topic);
                return std::nullopt;
            }
            // LOG_DEBUG("Protobuf parse success: topic='{}'", topic);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception parsing protobuf for topic {}: {}", topic, e.what());
            protobufParseErrors_.fetch_add(1, std::memory_order_relaxed);
            recordError();
            LOG_DEBUG("Protobuf parse exception for topic='{}': {}", topic, e.what());
            return std::nullopt;
        }

        // Verify we have the main data
        if (!orderbook_msg.has_data()) {
            LOG_ERROR("Protobuf message missing data field for topic: {}", topic);
            protobufParseErrors_.fetch_add(1, std::memory_order_relaxed);
            LOG_DEBUG("processProtobufOrderbook: missing data for topic='{}'", topic);
            return std::nullopt;
        }

        const auto& data = orderbook_msg.data();

        // Extract exchange and symbol
        auto [exchange, symbol] = extractExchangeSymbol(topic);
        
        // Override with protobuf values if available
        if (!data.exchange().empty()) {
            exchange = data.exchange();
        }
        if (!data.symbol().empty()) {
            symbol = data.symbol();
            // Convert '_' to '/' for symbol format
            std::replace(symbol.begin(), symbol.end(), '_', '/');
        }

        if (exchange.empty() || symbol.empty()) {
            LOG_ERROR("Missing exchange or symbol for protobuf topic: {}", topic);
            invalidTopics_.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }

        // Extract pricing data
        double bestBid = 0.0;
        double bestAsk = 0.0;
        double price = data.price();

        // Check bids array first, then fallback to single bid field
        if (data.bids_size() > 0) {
            bestBid = data.bids(0).price();
        } else if (data.bid() != 0.0) {
            bestBid = data.bid();
        }

        // Check asks array first, then fallback to single ask field
        if (data.asks_size() > 0) {
            bestAsk = data.asks(0).price();
        } else if (data.ask() != 0.0) {
            bestAsk = data.ask();
        }

        auto marketData = createMarketData(exchange, symbol, bestBid, bestAsk, price);
        
        // Set additional fields from protobuf
        marketData.volume = data.volume();
        marketData.high24h = data.high24h();
        marketData.low24h = data.low24h();

    LOG_DEBUG("processProtobufOrderbook: created market data for topic='{}' exchange='{}' symbol='{}'", topic, exchange, symbol);
    return marketData;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in processProtobufOrderbook for topic {}: {}", topic, e.what());
        protobufParseErrors_.fetch_add(1, std::memory_order_relaxed);
        recordError();
        return std::nullopt;
    }
}

std::pair<std::string, std::string> MarketDataProcessor::extractExchangeSymbol(
    const std::string& topic) {
    
    std::string exchange;
    std::string symbol;
    
    // Parse topic format: "orderbook.{exchange}.{symbol}"
    // Robust parsing: locate the prefix then the next dot separator between exchange and symbol.
    std::string_view topic_view(topic);
    const std::string_view prefix("orderbook.");
    if (topic_view.rfind(prefix, 0) == 0) {
        // start after the prefix
        size_t start = prefix.size();
        auto dot_pos = topic_view.find('.', start);
        if (dot_pos != std::string_view::npos) {
            exchange = std::string(topic_view.substr(start, dot_pos - start));
            symbol = std::string(topic_view.substr(dot_pos + 1));
            // Convert '_' to '/' in symbol if needed
            std::replace(symbol.begin(), symbol.end(), '_', '/');
        } else {
            LOG_DEBUG("Topic '{}' does not contain exchange/symbol separator after prefix", topic);
        }
    } else {
        // Fallback: try to split by dots and take last two components
        std::vector<std::string> parts;
        std::string tmp(topic);
        size_t pos = 0;
        while (true) {
            auto p = tmp.find('.');
            if (p == std::string::npos) {
                parts.push_back(tmp);
                break;
            }
            parts.push_back(tmp.substr(0, p));
            tmp = tmp.substr(p + 1);
        }
        if (parts.size() >= 3) {
            exchange = parts[parts.size() - 2];
            symbol = parts.back();
            std::replace(symbol.begin(), symbol.end(), '_', '/');
        } else {
            LOG_DEBUG("Unable to parse exchange/symbol from topic '{}', parts found: {}", topic, parts.size());
        }
    }
    
    // Use default exchange if not found
    if (exchange.empty()) {
        exchange = config_.defaultExchange;
    }
    
    return {exchange, symbol};
}

void MarketDataProcessor::resetStats() {
    // Reset atomic counters
    jsonParseErrors_.store(0, std::memory_order_relaxed);
    protobufParseErrors_.store(0, std::memory_order_relaxed);
    oversizedMessages_.store(0, std::memory_order_relaxed);
    invalidTopics_.store(0, std::memory_order_relaxed);
    totalProcessed_.store(0, std::memory_order_relaxed);
    totalErrors_.store(0, std::memory_order_relaxed);
    lastError_ = std::chrono::steady_clock::time_point{};

    // Update snapshot struct as well
    errorStats_.jsonParseErrors = 0;
    errorStats_.protobufParseErrors = 0;
    errorStats_.oversizedMessages = 0;
    errorStats_.invalidTopics = 0;
    errorStats_.totalProcessed = 0;
    errorStats_.totalErrors = 0;
    errorStats_.lastError = lastError_;
}

ProcessorErrorStats MarketDataProcessor::getStats() const {
    ProcessorErrorStats stats;
    stats.jsonParseErrors = jsonParseErrors_.load(std::memory_order_relaxed);
    stats.protobufParseErrors = protobufParseErrors_.load(std::memory_order_relaxed);
    stats.oversizedMessages = oversizedMessages_.load(std::memory_order_relaxed);
    stats.invalidTopics = invalidTopics_.load(std::memory_order_relaxed);
    stats.totalProcessed = totalProcessed_.load(std::memory_order_relaxed);
    stats.totalErrors = totalErrors_.load(std::memory_order_relaxed);
    stats.lastError = lastError_;
    return stats;
}

bool MarketDataProcessor::isValidTopic(const std::string& topic) const {
    // Check if topic starts with expected prefix
    // if (topic.size() < 10 || topic.compare(0, 10, "orderbook.") != 0) {
    //     invalidTopics_.fetch_add(1, std::memory_order_relaxed);
    //     return false;
    // }
    return true;
}

bool MarketDataProcessor::isValidMessageSize(const std::string& message) const {
    if (message.size() > config_.maxMessageSize) {
        oversizedMessages_.fetch_add(1, std::memory_order_relaxed);
        LOG_WARN("Dropping oversized message (size={} bytes, max={})", 
                message.size(), config_.maxMessageSize);
        return false;
    }
    return true;
}

std::optional<double> MarketDataProcessor::extractPriceFromLevel(
    const nlohmann::json& level) {
    
    try {
        if (level.is_array() && level.size() >= 1) {
            const auto& price_elem = level[0];
            if (price_elem.is_string()) {
                return std::stod(price_elem.get<std::string>());
            } else if (price_elem.is_number()) {
                return price_elem.get<double>();
            }
        }
    } catch (const std::exception& e) {
        LOG_DEBUG("Failed to extract price from level: {}", e.what());
    }
    return std::nullopt;
}

core::MarketData MarketDataProcessor::createMarketData(
    const std::string& exchange, const std::string& symbol,
    double bid, double ask, double price) {
    
    core::MarketData data;
    data.exchangeId = core::ExchangeId::fromString(exchange);
    data.tradingPair = core::Symbol::fromString(symbol);
    data.timestamp = std::chrono::system_clock::now();
    data.bid = bid;
    data.ask = ask;
    
    // Calculate mid price if both bid and ask are available
    if (bid > 0.0 && ask > 0.0) {
        data.price = (bid + ask) / 2.0;
    } else if (price > 0.0) {
        data.price = price;
    } else if (bid > 0.0) {
        data.price = bid;
    } else if (ask > 0.0) {
        data.price = ask;
    }
    
    return data;
}

} // namespace risk
} // namespace trading
