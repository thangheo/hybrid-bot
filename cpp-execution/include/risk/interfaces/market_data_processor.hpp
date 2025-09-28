#pragma once

#include "market_data_cache.hpp"
#include "core/market_types.hpp"
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>

// Forward declaration for protobuf
namespace orderbook {
    class OrderbookMessage;
}

namespace trading {
namespace risk {

/**
 * Configuration for MarketDataProcessor
 */
struct ProcessorConfig {
    size_t maxMessageSize = 256 * 1024;  // 256KB max message size
    bool enableJsonProcessing = false;
    bool enableProtobufProcessing = true;
    std::string defaultExchange = "binance";
};

/**
 * Error statistics for the processor
 */
struct ProcessorErrorStats {
    uint64_t jsonParseErrors{0};
    uint64_t protobufParseErrors{0};
    uint64_t oversizedMessages{0};
    uint64_t invalidTopics{0};
    uint64_t totalProcessed{0};
    uint64_t totalErrors{0};
    std::chrono::steady_clock::time_point lastError;
    
    // Constructor from individual values
    ProcessorErrorStats() = default;
    
    ProcessorErrorStats(uint64_t jsonErrors, uint64_t protobufErrors, uint64_t oversized,
                       uint64_t invalidTopics, uint64_t processed, uint64_t errors,
                       std::chrono::steady_clock::time_point lastErr)
        : jsonParseErrors(jsonErrors), protobufParseErrors(protobufErrors)
        , oversizedMessages(oversized), invalidTopics(invalidTopics)
        , totalProcessed(processed), totalErrors(errors), lastError(lastErr) {}
};

/**
 * MarketDataProcessor - Handles message parsing and transformation
 * 
 * Responsibilities:
 * - Parse JSON and protobuf orderbook messages
 * - Extract market data from various message formats
 * - Transform raw data into normalized MarketData structures
 * - Handle message validation and error recovery
 * - Provide processing statistics
 */
class MarketDataProcessor {
public:
    explicit MarketDataProcessor(const ProcessorConfig& config = ProcessorConfig{});
    ~MarketDataProcessor() = default;
    
    // Disable copy/move
    MarketDataProcessor(const MarketDataProcessor&) = delete;
    MarketDataProcessor& operator=(const MarketDataProcessor&) = delete;
    MarketDataProcessor(MarketDataProcessor&&) = delete;
    MarketDataProcessor& operator=(MarketDataProcessor&&) = delete;
    
    /**
     * Process a raw message and extract MarketData
     * @param topic ZMQ topic
     * @param message Raw message content
     * @return Parsed MarketData if successful, nullopt otherwise
     */
    std::optional<core::MarketData> processMessage(const std::string& topic, 
                                                  const std::string& message);
    
    /**
     * Process JSON orderbook message
     */
    std::optional<core::MarketData> processJsonOrderbook(const std::string& topic, 
                                                        const std::string& message);
    
    /**
     * Process protobuf orderbook message
     */
    std::optional<core::MarketData> processProtobufOrderbook(const std::string& topic, 
                                                            const std::string& message);
    
    /**
     * Extract exchange and symbol from topic
     */
    std::pair<std::string, std::string> extractExchangeSymbol(const std::string& topic);
    
    /**
     * Get processing statistics (snapshot of current values)
     */
    ProcessorErrorStats getStats() const;
    
    /**
     * Reset statistics
     */
    void resetStats();

private:
    ProcessorConfig config_;
    // errorStats_
    mutable std::mutex statsMutex_;
    ProcessorErrorStats errorStats_;
    // Internal atomic statistics
    mutable std::atomic<uint64_t> jsonParseErrors_{0};
    mutable std::atomic<uint64_t> protobufParseErrors_{0};
    mutable std::atomic<uint64_t> oversizedMessages_{0};
    mutable std::atomic<uint64_t> invalidTopics_{0};
    mutable std::atomic<uint64_t> totalProcessed_{0};
    mutable std::atomic<uint64_t> totalErrors_{0};
    std::chrono::steady_clock::time_point lastError_;
    
    // Helper for recording errors
    void recordError() {
        totalErrors_.fetch_add(1, std::memory_order_relaxed);
        lastError_ = std::chrono::steady_clock::now();
    }
    
    // Helper methods
    bool isValidTopic(const std::string& topic) const;
    bool isValidMessageSize(const std::string& message) const;
    std::optional<double> extractPriceFromLevel(const nlohmann::json& level);
    core::MarketData createMarketData(const std::string& exchange, 
                                     const std::string& symbol,
                                     double bid = 0.0, 
                                     double ask = 0.0, 
                                     double price = 0.0);
};

} // namespace risk
} // namespace trading