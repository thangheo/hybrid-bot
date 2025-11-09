/**
 * Position Sync Types Implementation
 * 
 * File: position_sync_types.cpp
 * Created: 2025-11-09
 */
#include "risk/sync/position_sync_types.hpp"
#include <sstream>
#include <iomanip>
#ifdef USE_OPENSSL_SHA256
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#else
#include <crypto++/sha.h>
#include <crypto++/hex.h>
#endif

namespace trading {
namespace risk {
namespace sync {

// PositionEvent implementation
nlohmann::json PositionEvent::toJson() const {
    nlohmann::json j;
    j["eventId"] = eventId;
    j["sequenceNumber"] = sequenceNumber;
    j["eventType"] = static_cast<int>(eventType);
    j["priority"] = static_cast<int>(priority);
    j["symbol"] = symbol;
    j["exchangeId"] = exchangeId;
    j["strategyId"] = strategyId;
    j["quantity"] = quantity;
    j["avgPrice"] = avgPrice;
    j["lastPrice"] = lastPrice;
    j["unrealizedPnl"] = unrealizedPnl;
    j["realizedPnl"] = realizedPnl;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
    j["lastUpdated"] = std::chrono::duration_cast<std::chrono::milliseconds>(lastUpdated.time_since_epoch()).count();
    j["metadata"] = metadata;
    j["source"] = source;
    j["correlationId"] = correlationId;
    return j;
}

PositionEvent PositionEvent::fromJson(const nlohmann::json& j) {
    PositionEvent event;
    event.eventId = j.at("eventId").get<std::string>();
    event.sequenceNumber = j.at("sequenceNumber").get<uint64_t>();
    event.eventType = static_cast<PositionEventType>(j.at("eventType").get<int>());
    event.priority = static_cast<PositionEventPriority>(j.at("priority").get<int>());
    event.symbol = j.at("symbol").get<std::string>();
    event.exchangeId = j.at("exchangeId").get<std::string>();
    event.strategyId = j.at("strategyId").get<std::string>();
    event.quantity = j.at("quantity").get<double>();
    event.avgPrice = j.at("avgPrice").get<double>();
    event.lastPrice = j.at("lastPrice").get<double>();
    event.unrealizedPnl = j.at("unrealizedPnl").get<double>();
    event.realizedPnl = j.at("realizedPnl").get<double>();
    
    auto timestampMs = j.at("timestamp").get<int64_t>();
    event.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(timestampMs));
    
    auto lastUpdatedMs = j.at("lastUpdated").get<int64_t>();
    event.lastUpdated = std::chrono::system_clock::time_point(std::chrono::milliseconds(lastUpdatedMs));
    
    event.metadata = j.at("metadata").get<std::string>();
    event.source = j.at("source").get<std::string>();
    event.correlationId = j.at("correlationId").get<std::string>();
    
    return event;
}

bool PositionEvent::isValid() const {
    return !eventId.empty() && 
           !symbol.empty() && 
           !exchangeId.empty() &&
           sequenceNumber > 0;
}

std::string PositionEvent::getPositionKey() const {
    return symbol + ":" + exchangeId;
}

// PositionSnapshot implementation
nlohmann::json PositionSnapshot::toJson() const {
    nlohmann::json j;
    j["snapshotId"] = snapshotId;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
    j["checksum"] = checksum;
    
    nlohmann::json positionsJson = nlohmann::json::array();
    for (const auto& pos : positions) {
        nlohmann::json posJson;
        posJson["symbol"] = pos.getTradingPair().toString();
        posJson["exchangeId"] = pos.getExchangeId().toString();
        posJson["strategyId"] = pos.getStrategyId();
        posJson["quantity"] = pos.getQuantity();
        posJson["avgPrice"] = pos.getAverageEntryPrice();
        posJson["currentPrice"] = pos.getCurrentPrice();
        posJson["unrealizedPnl"] = pos.getUnrealizedPnl();
        posJson["realizedPnl"] = pos.getRealizedPnl();
        positionsJson.push_back(posJson);
    }
    j["positions"] = positionsJson;
    
    return j;
}

PositionSnapshot PositionSnapshot::fromJson(const nlohmann::json& j) {
    PositionSnapshot snapshot;
    snapshot.snapshotId = j.at("snapshotId").get<std::string>();
    
    auto timestampMs = j.at("timestamp").get<int64_t>();
    snapshot.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(timestampMs));
    
    snapshot.checksum = j.at("checksum").get<std::string>();
    
    for (const auto& posJson : j.at("positions")) {
        // Construct Position using public constructor
        core::ExchangeId exch = core::ExchangeId::fromString(posJson.at("exchangeId").get<std::string>());
        core::Symbol symbol = core::Symbol(posJson.at("symbol").get<std::string>());
        StrategyId sid = posJson.at("strategyId").get<std::string>();
        double qty = posJson.at("quantity").get<double>();
        double avg = posJson.at("avgPrice").get<double>();
        Position pos(exch, symbol, sid, qty, avg);
        pos.setCurrentPrice(posJson.at("currentPrice").get<double>());
        pos.setUnrealizedPnl(posJson.at("unrealizedPnl").get<double>());
        pos.setRealizedPnl(posJson.at("realizedPnl").get<double>());
        snapshot.positions.push_back(std::move(pos));
    }
    
    return snapshot;
}

bool PositionSnapshot::isValid() const {
    return !snapshotId.empty() && 
           !checksum.empty() &&
           calculateChecksum() == checksum;
}

std::string PositionSnapshot::calculateChecksum() const {
    std::stringstream ss;
    ss << snapshotId << ":";
    
    // Sort positions by key for consistent checksum
    std::vector<std::string> sortedKeys;
    for (const auto& pos : positions) {
        std::string key = pos.getTradingPair().toString() + ":" + pos.getExchangeId().toString();
        sortedKeys.push_back(key);
    }
    std::sort(sortedKeys.begin(), sortedKeys.end());
    
    for (const auto& key : sortedKeys) {
        // Find position by key
        for (const auto& pos : positions) {
            if ((pos.getTradingPair().toString() + ":" + pos.getExchangeId().toString()) == key) {
                ss << std::fixed << std::setprecision(8)
                   << pos.getQuantity() << ":" << pos.getAverageEntryPrice() << ";";
                break;
            }
        }
    }
    
    std::string data = ss.str();
    
    // Calculate SHA256 hash
#ifdef USE_OPENSSL_SHA256
    unsigned char hash[SHA256_DIGEST_LENGTH];
    if (!SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash)) {
        return std::string();
    }
    // convert to hex
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
#else
    CryptoPP::SHA256 hash;
    std::string digest;
    CryptoPP::StringSource(data, true,
        new CryptoPP::HashFilter(hash,
            new CryptoPP::HexEncoder(
                new CryptoPP::StringSink(digest))));
    return digest;
#endif
}

// PositionSyncStatus implementation  
nlohmann::json PositionSyncStatus::toJson() const {
    nlohmann::json j;
    j["isHealthy"] = isHealthy;
    j["lastSync"] = std::chrono::duration_cast<std::chrono::milliseconds>(lastSync.time_since_epoch()).count();
    j["lastSequenceNumber"] = lastSequenceNumber;
    j["pendingEvents"] = pendingEvents;
    j["processedEvents"] = processedEvents;
    j["errorCount"] = errorCount;
    j["lastError"] = lastError;
    j["cacheSize"] = cacheSize;
    j["cacheHits"] = cacheHits;
    j["cacheMisses"] = cacheMisses;
    return j;
}

// PositionSyncConfig implementation
nlohmann::json PositionSyncConfig::toJson() const {
    nlohmann::json j;
    j["publishEndpoint"] = publishEndpoint;
    j["publisherHwm"] = publisherHwm;
    j["batchSize"] = batchSize;
    j["batchTimeoutMs"] = batchTimeoutMs;
    j["enablePriority"] = enablePriority;
    j["subscribeEndpoints"] = subscribeEndpoints;
    j["subscriberHwm"] = subscriberHwm;
    j["cacheTimeout_ms"] = cacheTimeout_ms;
    j["maxQueueSize"] = maxQueueSize;
    j["reconciliationIntervalSeconds"] = reconciliationIntervalSeconds;
    j["reconciliationTimeoutMs"] = reconciliationTimeoutMs;
    j["maxRetries"] = maxRetries;
    j["retryDelayMs"] = retryDelayMs;
    j["healthCheckIntervalMs"] = healthCheckIntervalMs;
    j["maxSequenceGap"] = maxSequenceGap;
    j["maxEventAge_ms"] = maxEventAge_ms;
    return j;
}

PositionSyncConfig PositionSyncConfig::fromJson(const nlohmann::json& j) {
    PositionSyncConfig config;
    if (j.contains("publishEndpoint")) config.publishEndpoint = j["publishEndpoint"];
    if (j.contains("publisherHwm")) config.publisherHwm = j["publisherHwm"];
    if (j.contains("batchSize")) config.batchSize = j["batchSize"];
    if (j.contains("batchTimeoutMs")) config.batchTimeoutMs = j["batchTimeoutMs"];
    if (j.contains("enablePriority")) config.enablePriority = j["enablePriority"];
    if (j.contains("subscribeEndpoints")) config.subscribeEndpoints = j["subscribeEndpoints"];
    if (j.contains("subscriberHwm")) config.subscriberHwm = j["subscriberHwm"];
    if (j.contains("cacheTimeout_ms")) config.cacheTimeout_ms = j["cacheTimeout_ms"];
    if (j.contains("maxQueueSize")) config.maxQueueSize = j["maxQueueSize"];
    if (j.contains("reconciliationIntervalSeconds")) config.reconciliationIntervalSeconds = j["reconciliationIntervalSeconds"];
    if (j.contains("reconciliationTimeoutMs")) config.reconciliationTimeoutMs = j["reconciliationTimeoutMs"];
    if (j.contains("maxRetries")) config.maxRetries = j["maxRetries"];
    if (j.contains("retryDelayMs")) config.retryDelayMs = j["retryDelayMs"];
    if (j.contains("healthCheckIntervalMs")) config.healthCheckIntervalMs = j["healthCheckIntervalMs"];
    if (j.contains("maxSequenceGap")) config.maxSequenceGap = j["maxSequenceGap"];
    if (j.contains("maxEventAge_ms")) config.maxEventAge_ms = j["maxEventAge_ms"];
    return config;
}

// PositionRequest implementation
nlohmann::json PositionRequest::toJson() const {
    nlohmann::json j;
    j["requestId"] = requestId;
    j["requestType"] = static_cast<int>(requestType);
    j["symbol"] = symbol;
    j["exchangeId"] = exchangeId;
    j["strategyId"] = strategyId;
    return j;
}

PositionRequest PositionRequest::fromJson(const nlohmann::json& j) {
    PositionRequest request;
    request.requestId = j.at("requestId").get<std::string>();
    request.requestType = static_cast<PositionRequest::Type>(j.at("requestType").get<int>());
    request.symbol = j.at("symbol").get<std::string>();
    request.exchangeId = j.at("exchangeId").get<std::string>();
    request.strategyId = j.at("strategyId").get<std::string>();
    return request;
}

// PositionResponse implementation
nlohmann::json PositionResponse::toJson() const {
    nlohmann::json j;
    j["requestId"] = requestId;
    j["success"] = success;
    j["error"] = error;
    j["status"] = status.toJson();
    
    nlohmann::json positionsJson = nlohmann::json::array();
    for (const auto& pos : positions) {
        nlohmann::json posJson;
        posJson["symbol"] = pos.getTradingPair().toString();
        posJson["exchangeId"] = pos.getExchangeId().toString();
        posJson["strategyId"] = pos.getStrategyId();
        posJson["quantity"] = pos.getQuantity();
        posJson["avgPrice"] = pos.getAverageEntryPrice();
        positionsJson.push_back(posJson);
    }
    j["positions"] = positionsJson;
    
    return j;
}

PositionResponse PositionResponse::fromJson(const nlohmann::json& j) {
    PositionResponse response;
    response.requestId = j.at("requestId").get<std::string>();
    response.success = j.at("success").get<bool>();
    response.error = j.at("error").get<std::string>();
    // Note: status parsing would need PositionSyncStatus::fromJson implementation
    
    for (const auto& posJson : j.at("positions")) {
        core::ExchangeId exch = core::ExchangeId::fromString(posJson.at("exchangeId").get<std::string>());
        core::Symbol symbol = core::Symbol(posJson.at("symbol").get<std::string>());
        StrategyId sid = posJson.at("strategyId").get<std::string>();
        double qty = posJson.at("quantity").get<double>();
        double avg = posJson.at("avgPrice").get<double>();
        Position pos(exch, symbol, sid, qty, avg);
        response.positions.push_back(std::move(pos));
    }
    
    return response;
}

} // namespace sync
} // namespace risk
} // namespace trading