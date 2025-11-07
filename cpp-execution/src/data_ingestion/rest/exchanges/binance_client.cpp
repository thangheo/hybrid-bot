/**
 * @file binance_client.cpp
 * @brief Implementation of Binance-specific REST client
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */

// TODO: Implement Implementation of Binance-specific REST client
#include "data_ingestion/rest/exchanges/binance_client.hpp"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
// #include <iomanip>
#include <chrono>
#include <sys/stat.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#include <iostream>
#include <cstdlib>
#include <mutex>


using namespace nlohmann;

namespace trading {
namespace data_ingestion {

// Static constants
const std::string BinanceClient::MAINNET_URL = "https://api.binance.com";
const std::string BinanceClient::TESTNET_URL = "https://testnet.binance.vision";


// Callback function for libcurl to write response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

namespace {
    // Ensure libcurl is initialized exactly once per process. We register
    // curl_global_cleanup via atexit so cleanup happens at program exit.
    static std::once_flag curlInitFlag;

    void ensureCurlInitialized() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::atexit([](){ curl_global_cleanup(); });
    }
}

BinanceClient::BinanceClient() : m_base_url(MAINNET_URL) , clockOffsetMs_(0), defaultRecvWindowMs_(5000) {
    std::call_once(curlInitFlag, ensureCurlInitialized);
    // Initialize libcurl globally once in a thread-safe manner
    std::call_once(curlInitFlag, ensureCurlInitialized);
    // Attempt to read API credentials & testnet flag from environment so the
    // client can be used without an explicit call to setApiKey in some
    // deployment scenarios (e.g., containers or CI).
    const char* env_key = std::getenv("BINANCE_API_KEY");
    const char* env_secret = std::getenv("BINANCE_API_SECRET");
    const char* env_testnet = std::getenv("BINANCE_TESTNET");

    // If BINANCE_TESTNET is set to 1/true, switch to the testnet URL.
    if (env_testnet) {
        std::string val(env_testnet);
        if (val == "1" || val == "true" || val == "TRUE" || val == "True") {
            m_base_url = TESTNET_URL;
            LOG_INFO("BinanceClient: using TESTNET URL {}", m_base_url);
        }
    }

    if (env_key && env_secret) {
        try {
            setApiKey(std::string(env_key), std::string(env_secret));
            LOG_INFO("BinanceClient: loaded API key/secret from environment");
        } catch (const std::exception& e) {
            LOG_WARN("BinanceClient: failed to set API key from environment: {}", e.what());
        }
    } else {
        LOG_INFO("BinanceClient: BINANCE_API_KEY/BINANCE_API_SECRET not set; private endpoints require explicit credentials");
    }

    // Configure periodic time resync interval from environment variable
    const char* env_resync = std::getenv("BINANCE_TIME_RESYNC_INTERVAL_SEC");
    if (env_resync) {
        try {
            int v = std::stoi(std::string(env_resync));
            if (v > 0) timeResyncIntervalSec_ = v;
        } catch (...) {
            // ignore invalid values and keep default
        }
    }

    // Start background thread for periodic server time resync
    timeResyncRunning_.store(true);
    timeResyncThread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lk(this->timeResyncMutex_);
        while (this->timeResyncRunning_.load()) {
            // Wait for interval or until signaled to stop
            auto now = std::chrono::system_clock::now();
            auto wake = now + std::chrono::seconds(this->timeResyncIntervalSec_);
            this->timeResyncCv_.wait_until(lk, wake, [this]() { return !this->timeResyncRunning_.load(); });
            if (!this->timeResyncRunning_.load()) break;

            try {
                if (!this->resyncServerTime()) {
                    LOG_WARN("BinanceClient: periodic server time resync attempt failed");
                }
            } catch (const std::exception& e) {
                LOG_WARN("BinanceClient: periodic server time resync threw: {}", e.what());
            }
        }
    });
}

BinanceClient::~BinanceClient() {
    // Stop background resync thread
    try {
        timeResyncRunning_.store(false);
        timeResyncCv_.notify_all();
        if (timeResyncThread_.joinable()) timeResyncThread_.join();
    } catch (...) {
        // destructor should not throw
    }
}
void BinanceClient::setApiKey(const std::string& key, const std::string& secret) {
    {
        std::lock_guard<std::mutex> lk(apiMutex_);
        m_api_key = key;
        m_api_secret = secret;
    }
    
    // Sync server time - do this OUTSIDE the lock to avoid deadlocks
    try {
        std::map<std::string, std::string> params;
        std::string resp = sendPublicRequest("/api/v3/time", params);
        auto root = nlohmann::json::parse(resp);
        if (root.contains("serverTime")) {
            long long serverTime = root["serverTime"].get<long long>();
            long long local = getCurrentTimestamp();
            // Use atomic operation for thread safety
            clockOffsetMs_.store(serverTime - local);
            LOG_INFO("BinanceClient: synchronized clock offset {} ms", clockOffsetMs_.load());
        }
    } catch (const std::exception& e) {
        LOG_WARN("BinanceClient: failed to sync server time: {}", e.what());
    }
}

// void BinanceClient::setApiKey(const std::string& key, const std::string& secret) {
//     std::lock_guard<std::mutex> lk(apiMutex_);
//     m_api_key = key;
//     m_api_secret = secret;
//     // Attempt to sync server time to reduce timestamp/recvWindow errors
//     try {
//         // Query Binance server time
//         std::map<std::string, std::string> params;
//         std::string resp = sendPublicRequest("/api/v3/time", params);
//         auto root = nlohmann::json::parse(resp);
//         if (root.contains("serverTime")) {
//             long long serverTime = root["serverTime"].get<long long>();
//             long long local = getCurrentTimestamp();
//             clockOffsetMs_ = serverTime - local;
//             LOG_INFO("BinanceClient: synchronized clock offset {} ms", clockOffsetMs_);
//         }
//     } catch (const std::exception& e) {
//         LOG_WARN("BinanceClient: failed to sync server time: {}", e.what());
//     }
// }

void BinanceClient::setBaseUrl(const std::string& url) {
    m_base_url = url;
}

std::map<std::string, double> BinanceClient::getBalances() {
    std::map<std::string, std::string> params;
    params["timestamp"] = std::to_string(getCurrentTimestamp());

    std::string response = sendSignedRequest("/api/v3/account", params);

    std::map<std::string, double> balances;

    try {
        auto root = nlohmann::json::parse(response);
        if (root.contains("balances") && root["balances"].is_array()) {
            for (const auto& balance : root["balances"]) {
                std::string asset = balance["asset"];
                double free = 0.0;
                // Binance sometimes returns free as string or number
                if (balance["free"].is_string()) {
                    free = std::stod(balance["free"].get<std::string>());
                } else {
                    free = balance["free"].get<double>();
                }
                if (free > 0.0) {
                    balances[asset] = free;
                }
            }
        } else {
            throw std::runtime_error("Invalid response format for account balances");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Error parsing balances response: " + std::string(e.what()));
    }

    return balances;
}

double BinanceClient::getBalance(const std::string& asset) {
    auto balances = getBalances();
    auto it = balances.find(asset);
    return (it != balances.end()) ? it->second : 0.0;
}

OrderBookUpdate BinanceClient::getOrderBook(const std::string& symbol, int limit) {
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    params["limit"] = std::to_string(limit);
    
    std::string response = sendPublicRequest("/api/v3/depth", params);
    
    OrderBookUpdate orderBook;
    orderBook.tradingPair = Symbol(symbol);
    
    try {
        auto root = nlohmann::json::parse(response);

        if (root.is_null() || !root.is_object()) {
            throw std::runtime_error("Failed to parse JSON response");
        }
        
        // Parse bids
        if (root.contains("bids") && root["bids"].is_array()) {
            for (const auto& bid : root["bids"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    PriceLevel level;
                    level.price = std::stod(bid[0].get<std::string>());
                    level.quantity = std::stod(bid[1].get<std::string>());
                    orderBook.bids.push_back(level);
                }
            }
        }
        
        // Parse asks
        if (root.contains("asks") && root["asks"].is_array()) {
            for (const auto& ask : root["asks"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    PriceLevel level;
                    level.price = std::stod(ask[0].get<std::string>());
                    level.quantity = std::stod(ask[1].get<std::string>());
                    orderBook.asks.push_back(level);
                }
            }
        }
        
        orderBook.receivedTimestamp = std::chrono::system_clock::now();
        orderBook.exchangeTimestamp = std::chrono::system_clock::now(); // Binance does not provide timestamp in this endpoint
        
    } catch (const std::exception& e) {
        throw std::runtime_error("Error parsing order book response: " + std::string(e.what()));
    }
    
    return orderBook;
}

double BinanceClient::getPrice(const std::string& symbol) {
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    
    std::string response = sendPublicRequest("/api/v3/ticker/price", params);
    
    try {
        // Json::Value root;
        // Json::Reader reader;
        auto root = nlohmann::json::parse(response);

        if (root.is_null() || !root.is_object()) {
            throw std::runtime_error("Failed to parse JSON response");
        }

        if (root.contains("price")) {
            // Binance sometimes returns price as string ("123.45") or number
            if (root["price"].is_string()) {
                return std::stod(root["price"].get<std::string>());
            } else if (root["price"].is_number()) {
                return root["price"].get<double>();
            } else {
                throw std::runtime_error("Unexpected type for price field");
            }
        } else {
            throw std::runtime_error("Price not found in response");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Error parsing price response: " + std::string(e.what()));
    }
}

std::string BinanceClient::placeOrder(const std::string& symbol, OrderSide side, OrderType type, 
                                     double quantity, double price) {
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    params["side"] = (side == OrderSide::BUY) ? "BUY" : "SELL";
    {
        std::ostringstream qoss;
        qoss << std::fixed << std::setprecision(8) << quantity;
        std::string qstr = qoss.str();
        // Trim trailing zeros and trailing dot
        qstr.erase(qstr.find_last_not_of('0') + 1, std::string::npos);
        if (!qstr.empty() && qstr.back() == '.') qstr.pop_back();
        params["quantity"] = qstr;
    }
    params["timestamp"] = std::to_string(getCurrentTimestamp());
    
    // Set order type
    switch (type) {
        case OrderType::MARKET:
            params["type"] = "MARKET";
            break;
        case OrderType::LIMIT:
            params["type"] = "LIMIT";
            params["timeInForce"] = "GTC";
            {
                std::ostringstream poss;
                poss << std::fixed << std::setprecision(8) << price;
                std::string pstr = poss.str();
                pstr.erase(pstr.find_last_not_of('0') + 1, std::string::npos);
                if (!pstr.empty() && pstr.back() == '.') pstr.pop_back();
                params["price"] = pstr;
            }
            break;
        case OrderType::TAKE_PROFIT:
            params["type"] = "TAKE_PROFIT";
            params["stopPrice"] = std::to_string(price);
            break;
        case OrderType::TAKE_PROFIT_LIMIT:
            params["type"] = "TAKE_PROFIT_LIMIT";
            params["timeInForce"] = "GTC";
            params["price"] = std::to_string(price);
            params["stopPrice"] = std::to_string(price);
            break;
        case OrderType::POST_ONLY:
            // Treat POST_ONLY as a LIMIT order with special flag
            params["type"] = "LIMIT";
            params["timeInForce"] = "GTC";
            params["price"] = std::to_string(price);
            params["postOnly"] = "true";
            break;
        default:
            // Optionally handle other types or throw
            break;
    }
    
    std::string response = sendSignedRequest("/api/v3/order", params);
    
    try {
        // Json::Value root; --- IGNORE ---
        // Json::Reader reader; --- IGNORE ---
        auto root = nlohmann::json::parse(response);
        if (root.is_null() || !root.is_object()) {
            throw std::runtime_error("Failed to parse JSON response");
        }

        if (root.contains("orderId")) {
            return root["orderId"].get<std::string>();
        } else if (root.contains("msg")) {
            throw std::runtime_error("Order failed: " + root["msg"].get<std::string>());
        } else {
            throw std::runtime_error("Invalid response format");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Error placing order: " + std::string(e.what()));
    }
}

bool BinanceClient::cancelOrder(const std::string& symbol, const std::string& orderId) {
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    params["orderId"] = orderId;
    params["timestamp"] = std::to_string(getCurrentTimestamp());
    
    try {
        std::string response = sendSignedRequest("/api/v3/order", params);
        
        auto root = nlohmann::json::parse(response);
        if (root.is_null() || !root.is_object()) {
            throw std::runtime_error("Failed to parse JSON response");
        }     
        
        return root.contains("orderId") && root["orderId"].get<std::string>() == orderId;
    } catch (const std::exception& e) {
        std::cerr << "Error canceling order: " << e.what() << std::endl;
        return false;
    }
}

OrderInfo BinanceClient::getOrderStatus(const std::string& symbol, const std::string& orderId) {
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    params["orderId"] = orderId;
    params["timestamp"] = std::to_string(getCurrentTimestamp());
    
    std::string response = sendSignedRequest("/api/v3/order", params);
    
    OrderInfo orderInfo;
    
    try {
        auto root = nlohmann::json::parse(response);
        if (root.is_null() || !root.is_object()) {
            throw std::runtime_error("Failed to parse JSON response");
        }

        orderInfo.orderId = root["orderId"].get<std::string>();
        orderInfo.symbol = root["symbol"].get<std::string>();
        // Robust parsing: Binance fields may come as strings or numbers
        if (root.contains("price")) {
            if (root["price"].is_string()) orderInfo.price = std::stod(root["price"].get<std::string>());
            else orderInfo.price = root["price"].get<double>();
        }
        if (root.contains("origQty")) {
            if (root["origQty"].is_string()) orderInfo.quantity = std::stod(root["origQty"].get<std::string>());
            else orderInfo.quantity = root["origQty"].get<double>();
        }
        if (root.contains("executedQty")) {
            if (root["executedQty"].is_string()) orderInfo.executedQty = std::stod(root["executedQty"].get<std::string>());
            else orderInfo.executedQty = root["executedQty"].get<double>();
        }
        if (root.contains("time")) {
            try {
                if (root["time"].is_string()) {
                    std::string ts = root["time"].get<std::string>();
                    // tolerant parse: handle empty, decimal, or numeric strings
                    if (!ts.empty()) {
                        if (ts.find('.') != std::string::npos) {
                            double d = std::stod(ts);
                            orderInfo.timestamp = static_cast<long long>(d);
                        } else {
                            orderInfo.timestamp = std::stoll(ts);
                        }
                    } else {
                        orderInfo.timestamp = 0;
                    }
                } else orderInfo.timestamp = root["time"].get<long long>();
            } catch (const std::exception &ex) {
                LOG_WARN("BinanceClient::getOrderStatus: failed to parse time '{}': {}", root["time"].dump(), ex.what());
                orderInfo.timestamp = 0;
            } catch (...) {
                LOG_WARN("BinanceClient::getOrderStatus: failed to parse time (unknown)");
                orderInfo.timestamp = 0;
            }
        }
        
        // Parse status
        std::string status = root["status"].get<std::string>();
        if (status == "NEW") orderInfo.status = OrderStatus::NEW;
        else if (status == "PARTIALLY_FILLED") orderInfo.status = OrderStatus::PARTIALLY_FILLED;
        else if (status == "FILLED") orderInfo.status = OrderStatus::FILLED;
        else if (status == "CANCELED") orderInfo.status = OrderStatus::CANCELED;
        else if (status == "REJECTED") orderInfo.status = OrderStatus::REJECTED;
        else if (status == "EXPIRED") orderInfo.status = OrderStatus::EXPIRED;
        
        // Parse side
        std::string side = root["side"].get<std::string>();
        orderInfo.side = (side == "BUY") ? OrderSide::BUY : OrderSide::SELL;
        
        // Parse type
        std::string type = root["type"].get<std::string>();
        if (type == "MARKET") orderInfo.type = OrderType::MARKET;
        else if (type == "LIMIT") orderInfo.type = OrderType::LIMIT;
        else if (type == "TAKE_PROFIT") orderInfo.type = OrderType::TAKE_PROFIT;
        else if (type == "TAKE_PROFIT_LIMIT") orderInfo.type = OrderType::TAKE_PROFIT_LIMIT;
        
    } catch (const std::exception& e) {
        throw std::runtime_error("Error parsing order status response: " + std::string(e.what()));
    }
    
    return orderInfo;
}

std::string BinanceClient::sendSignedRequest(const std::string& endpoint, 
                                           const std::map<std::string, std::string>& params) {
    // Get API credentials in a separate scope to minimize lock time
    std::string api_key, api_secret;
    {
        std::lock_guard<std::mutex> lk(apiMutex_);
        if (m_api_key.empty() || m_api_secret.empty()) {
            throw std::runtime_error("API key and secret must be set for signed requests");
        }
        api_key = m_api_key;
        api_secret = m_api_secret;
    }
    
    // Build parameters with thread-safe timestamp
    std::map<std::string, std::string> p = params;
    if (p.find("timestamp") == p.end()) {
        long long ts = getCurrentTimestamp() + clockOffsetMs_.load();
        p["timestamp"] = std::to_string(ts);
    }
    if (p.find("recvWindow") == p.end()) {
        p["recvWindow"] = std::to_string(defaultRecvWindowMs_.load());
    }

    std::string query_string = buildQueryString(p);
    std::string signature = generateSignature(query_string, api_secret);
    
    if (!query_string.empty()) {
        query_string += "&";
    }
    query_string += "signature=" + signature;
    
    std::string url = m_base_url + endpoint + "?" + query_string;
    
    std::map<std::string, std::string> headers;
    headers["X-MBX-APIKEY"] = api_key;
    
    try {
        return makeHttpRequest(url, "POST", "", headers);
    } catch (const std::exception& ex) {
        std::string err = ex.what();
        auto jsonPos = err.find('{');
        if (jsonPos != std::string::npos) {
            std::string body = err.substr(jsonPos);
            try {
                auto root = nlohmann::json::parse(body);
                if (root.contains("code") && root["code"].is_number_integer()) {
                    int code = root["code"].get<int>();
                    if (code == -1021) {
                        LOG_WARN("BinanceClient: received -1021 Timestamp error, performing server time sync and retry");
                        
                        // Resync server time WITHOUT holding any locks
                        if (resyncServerTime()) {
                            // Retry with corrected timestamp
                            std::map<std::string, std::string> p2 = p;
                            long long ts2 = getCurrentTimestamp() + clockOffsetMs_.load();
                            p2["timestamp"] = std::to_string(ts2);
                            
                            std::string query_string2 = buildQueryString(p2);
                            std::string signature2 = generateSignature(query_string2, api_secret);
                            if (!query_string2.empty()) query_string2 += "&";
                            query_string2 += "signature=" + signature2;
                            std::string url2 = m_base_url + endpoint + "?" + query_string2;

                            return makeHttpRequest(url2, "POST", "", headers);
                        }
                    }
                }
            } catch (const std::exception&) {
                // ignore parsing errors and rethrow original
            }
        }
        throw;
    }
}



// std::string BinanceClient::sendSignedRequest(const std::string& endpoint, 
//                                            const std::map<std::string, std::string>& params) {
//     {
//         std::lock_guard<std::mutex> lk(apiMutex_);
//         if (m_api_key.empty() || m_api_secret.empty()) {
//             throw std::runtime_error("API key and secret must be set for signed requests");
//         }
//     }
//     // Build a mutable copy so we can ensure timestamp and recvWindow are present
//     std::map<std::string, std::string> p = params;

//     // Ensure timestamp: use local time corrected by clockOffsetMs_
//     if (p.find("timestamp") == p.end()) {
//         long long offset;
//         {
//             std::lock_guard<std::mutex> lk(apiMutex_);
//             offset = clockOffsetMs_;
//         }
//         long long ts = getCurrentTimestamp() + offset;
//         p["timestamp"] = std::to_string(ts);
//     }

//     // Ensure recvWindow present
//     if (p.find("recvWindow") == p.end()) {
//         p["recvWindow"] = std::to_string(defaultRecvWindowMs_);
//     }

//     std::string query_string = buildQueryString(p);
//     std::string signature = generateSignature(query_string);
    
//     if (!query_string.empty()) {
//         query_string += "&";
//     }
//     query_string += "signature=" + signature;
    
//     std::string url = m_base_url + endpoint + "?" + query_string;
    
//     std::map<std::string, std::string> headers;
//     {
//         std::lock_guard<std::mutex> lk(apiMutex_);
//         headers["X-MBX-APIKEY"] = m_api_key;
//     }
    
//     // Attempt the request, but if Binance returns code -1021 (timestamp outside recvWindow)
//     // perform a server time sync and retry once with corrected timestamp.
//     try {
//         return makeHttpRequest(url, "POST", "", headers);
//     } catch (const std::exception& ex) {
//         std::string err = ex.what();
//         // Try to extract JSON body from the error message ("HTTP error <code>: <body>")
//         auto jsonPos = err.find('{');
//         if (jsonPos != std::string::npos) {
//             std::string body = err.substr(jsonPos);
//             try {
//                 auto root = nlohmann::json::parse(body);
//                 if (root.contains("code") && root["code"].is_number_integer()) {
//                     int code = root["code"].get<int>();
//                     if (code == -1021) {
//                         LOG_WARN("BinanceClient: received -1021 Timestamp error, performing server time sync and retry");
//                         // Resync server time
//                         try {
//                             std::map<std::string, std::string> empty;
//                             std::string resp = sendPublicRequest("/api/v3/time", empty);
//                             auto rt = nlohmann::json::parse(resp);
//                             if (rt.contains("serverTime")) {
//                                 long long serverTime = rt["serverTime"].get<long long>();
//                                 long long local = getCurrentTimestamp();
//                                 clockOffsetMs_ = serverTime - local;
//                                 LOG_INFO("BinanceClient: resynced clock offset {} ms", clockOffsetMs_);
//                             }
//                         } catch (const std::exception& e2) {
//                             LOG_WARN("BinanceClient: server time resync failed: {}", e2.what());
//                         }

//                         // Rebuild params with corrected timestamp and signature, then retry once
//                         std::map<std::string, std::string> p2 = p;
//                         long long ts2 = getCurrentTimestamp() + clockOffsetMs_;
//                         p2["timestamp"] = std::to_string(ts2);
//                         if (p2.find("recvWindow") == p2.end()) {
//                             p2["recvWindow"] = std::to_string(defaultRecvWindowMs_);
//                         }
//                         std::string query_string2 = buildQueryString(p2);
//                         std::string signature2 = generateSignature(query_string2);
//                         if (!query_string2.empty()) query_string2 += "&";
//                         query_string2 += "signature=" + signature2;
//                         std::string url2 = m_base_url + endpoint + "?" + query_string2;

//                         try {
//                             return makeHttpRequest(url2, "POST", "", headers);
//                         } catch (const std::exception& ex2) {
//                             // propagate the second error
//                             throw;
//                         }
//                     }
//                 }
//             } catch (const std::exception&) {
//                 // ignore parsing errors and rethrow original
//             }
//         }

//         // Not a recoverable -1021 or couldn't parse: rethrow original
//         throw;
//     }
// }

// Thread-safe signature generation with explicit secret parameter

std::string BinanceClient::generateSignature(const std::string& query_string, 
                                            const std::string& api_secret) const {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int hash_len = SHA256_DIGEST_LENGTH;
    
    HMAC(EVP_sha256(), 
         api_secret.c_str(), api_secret.length(),
         (unsigned char*)query_string.c_str(), query_string.length(),
         hash, &hash_len);
    
    std::ostringstream oss;
    for (int i = 0; i < hash_len; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    
    return oss.str();
}

bool BinanceClient::resyncServerTime() {
    try {
        std::map<std::string, std::string> empty;
        std::string resp = sendPublicRequest("/api/v3/time", empty);
        auto rt = nlohmann::json::parse(resp);
        if (rt.contains("serverTime")) {
            long long serverTime = rt["serverTime"].get<long long>();
            long long local = getCurrentTimestamp();
            clockOffsetMs_.store(serverTime - local);
            LOG_INFO("BinanceClient: resynced clock offset {} ms", clockOffsetMs_.load());
            return true;
        }
    } catch (const std::exception& e) {
        LOG_WARN("BinanceClient: server time resync failed: {}", e.what());
    }
    return false;
}

std::string BinanceClient::sendPublicRequest(const std::string& endpoint, 
                                           const std::map<std::string, std::string>& params) {
    std::string query_string = buildQueryString(params);
    std::string url = m_base_url + endpoint;
    
    if (!query_string.empty()) {
        url += "?" + query_string;
    }
    
    return makeHttpRequest(url);
}

std::string BinanceClient::buildQueryString(const std::map<std::string, std::string>& params) const {
    std::ostringstream oss;
    bool first = true;
    
    for (const auto& param : params) {
        if (!first) {
            oss << "&";
        }
        oss << urlEncode(param.first) << "=" << urlEncode(param.second);
        first = false;
    }
    
    return oss.str();
}

std::string BinanceClient::makeHttpRequest(const std::string& url, const std::string& method, 
                                         const std::string& body, 
                                         const std::map<std::string, std::string>& headers) const {
    CURL* curl;
    CURLcode res;
    std::string response_string;
    
    curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set method
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    // Set headers
    struct curl_slist* header_list = nullptr;
    for (const auto& header : headers) {
        std::string header_str = header.first + ": " + header.second;
        header_list = curl_slist_append(header_list, header_str.c_str());
    }
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    
    // Set response callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    
    // Set timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    // Enforce SSL verification and allow overriding CA bundle via env
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    const char* caBundle = getenv("BINANCE_CA_BUNDLE");
    if (!caBundle) caBundle = getenv("CURL_CA_BUNDLE");
    if (caBundle) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, caBundle);
    } else {
        // check common system locations
        struct stat st;
        if (stat("/etc/ssl/certs/ca-certificates.crt", &st) == 0) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
        } else if (stat("/etc/ssl/cert.pem", &st) == 0) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/cert.pem");
        }
    }
    
    // Perform request
    res = curl_easy_perform(curl);
    
    // Check response code
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    // Cleanup
    if (header_list) {
        curl_slist_free_all(header_list);
    }
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::string err = curl_easy_strerror(res);
        // Append hint about CA bundle to help debugging
        if (res == CURLE_SSL_CACERT || res == CURLE_SSL_CACERT_BADFILE) {
            err += std::string(" (check CA bundle path or set BINANCE_CA_BUNDLE/CURL_CA_BUNDLE)");
        }
        throw std::runtime_error("HTTP request failed: " + err);
    }
    
    if (response_code >= 400) {
        throw std::runtime_error("HTTP error " + std::to_string(response_code) + ": " + response_string);
    }
    
    return response_string;
}

long long BinanceClient::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return millis;
}

std::string BinanceClient::urlEncode(const std::string& value) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return value;
    }
    
    char* encoded = curl_easy_escape(curl, value.c_str(), value.length());
    std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    
    return result;
}

} // namespace data_ingestion
} // namespace trading