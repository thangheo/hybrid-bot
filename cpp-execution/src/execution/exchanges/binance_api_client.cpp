// exchanges/binance_api_client.cpp - OPTIMIZED VERSION
#include "execution/exchanges/binance_api_client.hpp"
#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <sstream>
#include <iomanip>
#include <openssl/hmac.h>
#include <chrono>
#include <future>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <sys/stat.h>
#include <common/logger.hpp>

namespace trading {
namespace execution {

// Private implementation class
struct BinanceApiClient::BinanceImpl {
    std::string apiKey;
    std::string apiSecret;
    std::string baseUrl = "https://api.binance.com";
    //for testnet
    // std::string baseUrl = "https://testnet.binance.vision";
    bool connected = false;
    
    // OPTIMIZATION 1: Cache symbol filters with better memory management
    struct SymbolFilters {
        double stepSize = 0.0; // lot size step for quantities
        double tickSize = 0.0; // price tick for prices
        double minQty = 0.0;   // minimum allowed quantity on exchange
        double minNotional = 0.0; // minimum notional value for the symbol
    };
    std::unordered_map<std::string, SymbolFilters> symbolFilters;
    
    // OPTIMIZATION 2: Reuse JSON components to avoid repeated allocations
    mutable Json::StreamWriterBuilder writerBuilder;
    mutable Json::CharReaderBuilder readerBuilder;
    mutable std::unique_ptr<Json::CharReader> jsonReader;
    
    // Constructor
    BinanceImpl(const ExchangeCredentials& credentials) 
            : apiKey(credentials.apiKey), 
                apiSecret(credentials.apiSecret) {
        // CURL global init is handled by the outer class once
        
        // OPTIMIZATION 3: Configure JSON reader/writer for minimal memory usage
        readerBuilder["collectComments"] = false;
        readerBuilder["allowComments"] = false;
        readerBuilder["strictRoot"] = true;
        readerBuilder["allowDroppedNullPlaceholders"] = false;
        readerBuilder["allowNumericKeys"] = false;
        readerBuilder["allowSingleQuotes"] = false;
        readerBuilder["stackLimit"] = 1000;  // Limit recursion depth
        readerBuilder["failIfExtra"] = false;
        readerBuilder["rejectDupKeys"] = false;
        readerBuilder["allowSpecialFloats"] = false;
        
        jsonReader = std::unique_ptr<Json::CharReader>(readerBuilder.newCharReader());
        
        writerBuilder["commentStyle"] = "None";
        writerBuilder["indentation"] = "";  // No pretty printing
        writerBuilder["enableYAMLCompatibility"] = false;
        writerBuilder["dropNullPlaceholders"] = true;
        writerBuilder["useSpecialFloats"] = false;
        writerBuilder["precision"] = 17;
    }
    
    // CURL callback for writing response
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
    
    // OPTIMIZATION 4: Helper method for efficient JSON parsing
    bool parseJsonResponse(const std::string& response, Json::Value& root, std::string& errors) const {
        const char* begin = response.c_str();
        const char* end = begin + response.length();
        return jsonReader->parse(begin, end, &root, &errors);
    }
};

BinanceApiClient::BinanceApiClient(const ExchangeCredentials& credentials, 
                                 std::shared_ptr<RateLimitController> rateLimitController)
    : pImpl_(std::make_unique<BinanceImpl>(credentials)),
      rateLimitController_(std::move(rateLimitController)) {
    // Initialize curl globally if not already done
    static std::once_flag flag;
    std::call_once(flag, []() { curl_global_init(CURL_GLOBAL_ALL); });
}

BinanceApiClient::~BinanceApiClient() = default;

void BinanceApiClient::connect() {
    if (pImpl_->connected) return;
    
    try {
        // Lightweight connectivity check using ping to avoid large exchangeInfo allocations
        std::map<std::string, std::string> params;
        // /api/v3/ping returns an empty JSON body but is much smaller than exchangeInfo
        std::string response = makeApiRequest("/api/v3/ping", "GET", params, false);
        // No need to parse large JSON here; success of the request indicates connectivity
        (void)response;
        pImpl_->connected = true;

        // OPTIMIZATION 5: Fetch and cache exchangeInfo more efficiently
        try {
            std::string info = makeApiRequest("/api/v3/exchangeInfo", "GET", {}, false);
            Json::Value root;
            std::string errors;
            
            if (pImpl_->parseJsonResponse(info, root, errors)) {
                // OPTIMIZATION 6: Reserve space for symbols to avoid reallocations
                const Json::Value& symbolsJson = root["symbols"];
                if (symbolsJson.isArray()) {
                    pImpl_->symbolFilters.reserve(symbolsJson.size());
                    
                    for (const auto& sym : symbolsJson) {
                        if (!sym.isMember("symbol")) continue;
                        std::string symbol = sym["symbol"].asString();
                        BinanceApiClient::BinanceImpl::SymbolFilters filters;
                        
                        const Json::Value& filtersJson = sym["filters"];
                        if (filtersJson.isArray()) {
                            for (const auto& f : filtersJson) {
                                const std::string& ftype = f["filterType"].asString();
                                if (ftype == "LOT_SIZE") {
                                    filters.stepSize = std::stod(f["stepSize"].asString());
                                    if (f.isMember("minQty")) {
                                        filters.minQty = std::stod(f["minQty"].asString());
                                    }
                                } else if (ftype == "PRICE_FILTER") {
                                    filters.tickSize = std::stod(f["tickSize"].asString());
                                } else if (ftype == "MIN_NOTIONAL") {
                                    if (f.isMember("minNotional")) {
                                        filters.minNotional = std::stod(f["minNotional"].asString());
                                    }
                                }
                            }
                        }
                        
                        // OPTIMIZATION 7: Use emplace for better performance
                        pImpl_->symbolFilters.emplace(std::move(symbol), filters);
                    }
                }
            } else {
                std::cerr << "Warning: JSON parsing errors in exchangeInfo: " << errors << std::endl;
            }
        } catch (const std::exception& ex) {
            // Non-fatal: we can continue without filters, but log for debugging
            std::cerr << "Warning: failed to fetch Binance exchangeInfo: " << ex.what() << std::endl;
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to connect to Binance: ") + e.what());
    }
}

bool BinanceApiClient::checkConnection() const {
    if (!pImpl_->connected) {
        return false;
    }
    
    // Actually test the connection by making a lightweight API call
    try {
        std::map<std::string, std::string> params;
        std::string response = makeApiRequest("/api/v3/ping", "GET", params, false);
        // If the call succeeded, the connection is still valid
        return true;
    } catch (const std::exception& e) {
        // Log the error but don't throw it
        std::cerr << "Connection check failed: " << e.what() << std::endl;
        
        // Connection test failed, so we're not connected
        pImpl_->connected = false;
        return false;
    }
}

void BinanceApiClient::disconnect() {
    pImpl_->connected = false;
    // OPTIMIZATION 8: Clear cached data to free memory
    pImpl_->symbolFilters.clear();
}

bool BinanceApiClient::isConnected() const {
    return pImpl_->connected;
}

std::string BinanceApiClient::getExchangeName() const {
    return "BINANCE";
}

std::vector<std::string> BinanceApiClient::getSupportedSymbols() const {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to Binance");
    }
    
    // OPTIMIZATION 9: Use cached symbol filters if available
    if (!pImpl_->symbolFilters.empty()) {
        std::vector<std::string> symbols;
        symbols.reserve(pImpl_->symbolFilters.size());
        for (const auto& [symbol, filters] : pImpl_->symbolFilters) {
            symbols.push_back(symbol);
        }
        return symbols;
    }
    
    // Fallback to API call if cache is empty
    std::map<std::string, std::string> params;
    std::string response = makeApiRequest("/api/v3/exchangeInfo", "GET", params, false);
    
    Json::Value root;
    std::string errors;
    if (!pImpl_->parseJsonResponse(response, root, errors)) {
        throw std::runtime_error("Failed to parse Binance exchange info: " + errors);
    }
    
    std::vector<std::string> symbols;
    const Json::Value& symbolsJson = root["symbols"];
    if (symbolsJson.isArray()) {
        symbols.reserve(symbolsJson.size());
        for (const auto& symbolJson : symbolsJson) {
            if (symbolJson["status"].asString() == "TRADING") {
                symbols.emplace_back(symbolJson["symbol"].asString());
            }
        }
    }
    
    return symbols;
}

//placeOrder
std::future<models::ExecutionReport> BinanceApiClient::placeOrder(const models::Order& order) {
    // Extract all the necessary data from the order before the async call
    core::Symbol tradingPair = order.getTradingPair().toString();
    auto side = order.getSide();
    auto type = order.getType();
    double quantity = order.getQuantity();
    double price = order.getPrice();
    auto timeInForce = order.getTimeInForce();
    std::string clientOrderId = order.getClientOrderId();
    
    // Copy the extra parameters map
    std::unordered_map<std::string, std::string> extraParams = order.getExtraParams();
    
    return std::async(std::launch::async, [this, 
                                          tradingPair, 
                                          side, 
                                          type, 
                                          quantity, 
                                          price, 
                                          timeInForce, 
                                          clientOrderId, 
                                          extraParams]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::PlaceOrder);
        
        // Prepare parameters
        std::map<std::string, std::string> params;
        params["symbol"] = trading::execution::util::normalizeSymbolForExchange(tradingPair.toString());
        params["side"] = translateOrderSide(side);
        params["type"] = translateOrderType(type);
        
        // Format quantity and price according to symbol filters (if available)
        std::string sym = trading::execution::util::normalizeSymbolForExchange(tradingPair.toString());
        double step = 0.0;
        double tick = 0.0;
        auto it = pImpl_->symbolFilters.find(sym);
        if (it != pImpl_->symbolFilters.end()) {
            step = it->second.stepSize;
            tick = it->second.tickSize;
        }

        if (step > 0.0) {
            double alignedQty = roundDownToStep(quantity, step);
            // Validate against exchange minQty if available
            auto itf = pImpl_->symbolFilters.find(sym);
            double minQty = 0.0;
            double minNotional = 0.0;
            if (itf != pImpl_->symbolFilters.end()) {
                minQty = itf->second.minQty;
                minNotional = itf->second.minNotional;
            }
            // If aligned to zero or too small, try to round up to meet exchange minimums
            const double DEFAULT_MIN_NOTIONAL = 10.0; // $10 default if exchange doesn't provide minNotional
            double effectiveMinNotional = (minNotional > 0.0 ? minNotional : DEFAULT_MIN_NOTIONAL);

            if (alignedQty <= 0.0) {
                // Compute required qty to meet minNotional (if price available)
                double requiredQty = step; // at least one step
                if (price > 0.0 && effectiveMinNotional > 0.0) {
                    double raw = effectiveMinNotional / price;
                    requiredQty = roundUpToStep(raw, step);
                } else if (minQty > 0.0) {
                    requiredQty = roundUpToStep(minQty, step);
                }

                // Ensure we at least place one step
                if (requiredQty <= 0.0) requiredQty = step;

                // Safety: do not increase order size by more than a configured multiplier . STFU
                const double MAX_UPLIFT = 50; // allow at most 2x increase
                double uplift = (quantity > 0.0) ? (requiredQty / quantity) : MAX_UPLIFT;
                if (uplift > MAX_UPLIFT) {
                    // Gracefully reject the order instead of throwing an exception
                    models::ExecutionReport rep;
                    rep.setStatus(models::OrderState::REJECTED);
                    rep.setExchangeId(core::ExchangeId::binance());
                    rep.setSymbol(sym);
                    std::ostringstream oss;
                    oss << "Adjusted quantity for symbol " << sym << " would uplift too much: requested=" << quantity
                        << ", required=" << requiredQty << ", uplift=" << uplift << " > " << MAX_UPLIFT;
                    rep.setRejectReason(oss.str());
                    return rep;
                }

                alignedQty = requiredQty;
            }

            // If MIN_NOTIONAL is set, also validate notional and increase if necessary
            if (effectiveMinNotional > 0.0 && price > 0.0) {
                double notional = alignedQty * price;
                if (notional + 1e-12 < effectiveMinNotional) {
                    double raw = effectiveMinNotional / price;
                    double requiredQty = roundUpToStep(raw, step);
                    if (requiredQty <= 0.0) requiredQty = step;

                    const double MAX_UPLIFT = 50;
                    double uplift = (quantity > 0.0) ? (requiredQty / quantity) : MAX_UPLIFT;
                    if (uplift > MAX_UPLIFT) {
                        models::ExecutionReport rep;
                        rep.setStatus(models::OrderState::REJECTED);
                        rep.setExchangeId(core::ExchangeId::binance());
                        rep.setSymbol(sym);
                        std::ostringstream oss;
                        oss << "Notional adjustment for symbol " << sym << " would uplift too much: requested=" << quantity
                            << ", required=" << requiredQty << ", uplift=" << uplift << " > " << MAX_UPLIFT;
                        rep.setRejectReason(oss.str());
                        return rep;
                    }

                    alignedQty = requiredQty;
                }
            }

            // Ensure alignedQty respects minQty if provided
            if (minQty > 0.0 && alignedQty + 1e-12 < minQty) {
                double requiredQty = roundUpToStep(minQty, step);
                const double MAX_UPLIFT = 50;
                double uplift = (quantity > 0.0) ? (requiredQty / quantity) : MAX_UPLIFT;
                if (uplift > MAX_UPLIFT) {
                    models::ExecutionReport rep;
                    rep.setStatus(models::OrderState::REJECTED);
                    rep.setExchangeId(core::ExchangeId::binance());
                    rep.setSymbol(sym);
                    std::ostringstream oss;
                    oss << "minQty adjustment for symbol " << sym << " would uplift too much: requested=" << quantity
                        << ", required=" << requiredQty << ", uplift=" << uplift << " > " << MAX_UPLIFT;
                    rep.setRejectReason(oss.str());
                    return rep;
                }
                alignedQty = requiredQty;
            }

            params["quantity"] = formatDoubleWithStep(alignedQty, step);
        } else {
            // Use safe precision formatting instead of std::to_string to avoid long decimals
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(8) << quantity;
            std::string qtyStr = oss.str();
            // Remove trailing zeros
            qtyStr.erase(qtyStr.find_last_not_of('0') + 1, std::string::npos);
            if (qtyStr.back() == '.') qtyStr.pop_back();
            params["quantity"] = qtyStr;
        }

        if (type != models::OrderType::MARKET) {
            if (tick > 0.0) {
                params["price"] = formatDoubleWithStep(roundDownToStep(price, tick), tick);
            } else {
                params["price"] = std::to_string(price);
            }
            params["timeInForce"] = translateTimeInForce(timeInForce);
        }
        
        if (!clientOrderId.empty()) {
            params["newClientOrderId"] = clientOrderId;
        }
        
        // Add extra parameters if present
        for (const auto& [key, value] : extraParams) {
            params[key] = value;
        }
        
        // Add timestamp
        params["timestamp"] = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        // Send request
        std::string response = makeApiRequest("/api/v3/order", "POST", params, true);
        
        // Parse initial response
        models::ExecutionReport report = parseOrderResponse(response);

        // If caller provided a cancel-after-ms parameter, start a detached watcher
        // extraParams is a copy captured above. Look for "cancel_after_ms" key.
        // auto it_cancel = extraParams.find("cancel_after_ms");
        //fuck stupid dog
        double cancel_fuckingyou= 0;
        // if (it_cancel != extraParams.end()) {
            try {
                // long cancelMs = std::stol(it_cancel->second);
                long cancelMs = 1000;
                // Only start watcher if we have an order id and the order is still open
                std::string orderId = report.exchangeOrderId();
                core::Symbol symObj = report.symbol();
                if (!orderId.empty() && (report.status() == models::OrderState::OPEN || report.status() == models::OrderState::PARTIALLY_FILLED)) {
                    // Launch detached thread to cancel after timeout if still open
                    std::thread([this, orderId, symObj, cancelMs]() {
                        try {
                            std::this_thread::sleep_for(std::chrono::milliseconds(cancelMs));
                            // Query status
                            auto futStatus = this->getOrderStatus(orderId, symObj);
                            models::ExecutionReport statusReport = futStatus.get();
                            if (statusReport.status() == models::OrderState::OPEN || statusReport.status() == models::OrderState::PARTIALLY_FILLED) {
                                // Cancel the order
                                (void)this->cancelOrder(orderId, symObj).get();
                            }
                        } catch (const std::exception &ex) {
                            // Swallow exceptions from watcher thread; log for debugging
                            std::cerr << "cancel_after_ms watcher error: " << ex.what() << std::endl;
                        } catch (...) {
                            std::cerr << "cancel_after_ms watcher unknown error" << std::endl;
                        }
                    }).detach();
                }
            } catch (const std::exception &ex) {
                // std::cerr << "Invalid cancel_after_ms value: " << it_cancel->second << " (" << ex.what() << ")" << std::endl;
            }
        // }

        return report;
    });
}

std::future<models::ExecutionReport> BinanceApiClient::cancelOrder(const std::string& orderId, const core::Symbol& symbol) {
    // Capture the parameters by value since they're already simple types
    return std::async(std::launch::async, [this, orderId, symbol]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::CancelOrder);
        
        // Prepare parameters
        std::map<std::string, std::string> params;
        params["symbol"] = trading::execution::util::normalizeSymbolForExchange(symbol.toString());
        
        // Check if orderId is a client order ID or exchange order ID
        if (orderId.find("CLIENT:") == 0) {
            // It's a client order ID
            params["origClientOrderId"] = orderId.substr(7);
        } else {
            // It's an exchange order ID
            params["orderId"] = orderId;
        }
        
        // Add timestamp
        params["timestamp"] = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        // Send request
        std::string response = makeApiRequest("/api/v3/order", "DELETE", params, true);
        
        // Parse response
        return parseOrderResponse(response);
    });
}

std::future<models::ExecutionReport> BinanceApiClient::getOrderStatus(const std::string& orderId, const core::Symbol& symbol) {
    // Capture the parameters by value since they're already simple types
    return std::async(std::launch::async, [this, orderId, symbol]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::GetOrderStatus);
        
        // Prepare parameters
        std::map<std::string, std::string> params;
        params["symbol"] = trading::execution::util::normalizeSymbolForExchange(symbol.toString());
        
        // Check if orderId is a client order ID or exchange order ID
        if (orderId.find("CLIENT:") == 0) {
            // It's a client order ID
            params["origClientOrderId"] = orderId.substr(7);
        } else {
            // It's an exchange order ID
            params["orderId"] = orderId;
        }
        
        // Add timestamp
        params["timestamp"] = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        // Send request
        std::string response = makeApiRequest("/api/v3/order", "GET", params, true);
        
        // Parse response
        return parseOrderResponse(response);
    });
}

std::future<std::map<std::string, double>> BinanceApiClient::getAccountBalances() {
    return std::async(std::launch::async, [this]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::GetAccountBalance);
        
        // Prepare parameters
        std::map<std::string, std::string> params;
        params["timestamp"] = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        // Send request
        std::string response = makeApiRequest("/api/v3/account", "GET", params, true);
        
        // OPTIMIZATION 10: More efficient JSON parsing for balances
        Json::Value root;
        std::string errors;
        if (!pImpl_->parseJsonResponse(response, root, errors)) {
            throw std::runtime_error("Failed to parse Binance account information: " + errors);
        }
        
        std::map<std::string, double> balances;
        const Json::Value& balancesJson = root["balances"];
        if (balancesJson.isArray()) {
            for (const auto& balanceJson : balancesJson) {
                std::string asset = balanceJson["asset"].asString();
                double free = std::stod(balanceJson["free"].asString());
                double locked = std::stod(balanceJson["locked"].asString());
                
                // Only store non-zero balances to save memory
                double total = free + locked;
                if (total > 0.0) {
                    balances.emplace(std::move(asset), total);
                }
            }
        }
        
        return balances;
    });
}

std::future<double> BinanceApiClient::getCurrentPrice(const core::Symbol& symbol) {
    return std::async(std::launch::async, [this, symbol]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::GetMarketData);
        
        // Prepare parameters
        std::map<std::string, std::string> params;
        params["symbol"] = trading::execution::util::normalizeSymbolForExchange(symbol.toString());
        
        // Send request
        std::string response = makeApiRequest("/api/v3/ticker/price", "GET", params, false);
        
        // Parse response
        Json::Value root;
        std::string errors;
        if (!pImpl_->parseJsonResponse(response, root, errors)) {
            throw std::runtime_error("Failed to parse Binance price information: " + errors);
        }
        
        return std::stod(root["price"].asString());
    });
}

int BinanceApiClient::getRemainingRequests(ExchangeRequestType requestType) const {
    return rateLimitController_->getRemainingRequests(requestType);
}

std::chrono::milliseconds BinanceApiClient::getResetTimeForRequestType(ExchangeRequestType requestType) const {
    return rateLimitController_->getResetTime(requestType);
}

std::string BinanceApiClient::getHealthStatus() {
    if (!isConnected()) {
        connect();
    }
    
    // Wait for rate limit
    rateLimitController_->waitForPermission(ExchangeRequestType::GetHealthStatus);
    
    // Prepare parameters
    std::map<std::string, std::string> params;
    
    // Send request
    std::string response = makeApiRequest("/api/v3/ping", "GET", params, false);
    
    return response;
}

// getExchangeStatuses
std::unordered_map<std::string, std::string> BinanceApiClient::getExchangeStatuses() const {
    // Check if connected    
    if (!isConnected()) {
        throw std::runtime_error("Not connected to Binance");        
    }
    
    // Wait for rate limit
    rateLimitController_->waitForPermission(ExchangeRequestType::GetExchangeStatuses);
    // Prepare parameters
    std::map<std::string, std::string> params;
    
    // Send request
    std::string response = makeApiRequest("/sapi/v1/exchangeStatus", "GET", params, false);
    
    Json::Value root;
    std::string errors;
    if (!pImpl_->parseJsonResponse(response, root, errors)) {
        throw std::runtime_error("Failed to parse Binance exchange status information: " + errors);
    }
    
    std::unordered_map<std::string, std::string> statuses;
    const Json::Value& symbolsJson = root["symbols"];
    if (symbolsJson.isArray()) {
        statuses.reserve(symbolsJson.size());
        for (const auto& symbolJson : symbolsJson) {
            std::string symbol = symbolJson["symbol"].asString();
            std::string status = symbolJson["status"].asString();
            statuses.emplace(std::move(symbol), std::move(status));
        }
    }
    
    return statuses;
}

std::string BinanceApiClient::makeApiRequest(std::string endpoint, std::string method,
        const std::map<std::string, std::string>& params, bool isPrivate) const {
    std::string url = pImpl_->baseUrl + endpoint;
    std::string queryString;
    
    // Build query string
    for (const auto& [key, value] : params) {
        if (!queryString.empty()) {
            queryString += "&";
        }
        queryString += key + "=" + value;
    }
    
    // For GET requests, append query string to URL
    if (method == "GET" && !queryString.empty()) {
        url += "?" + queryString;
    }
    
    // Create a new CURL handle for this request to be thread-safe
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL handle");
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set request method
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!queryString.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, queryString.c_str());
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        if (!queryString.empty() && url.find('?') == std::string::npos) {
            url += "?" + queryString;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        }
    }
    
    // OPTIMIZATION 11: Reserve response buffer capacity to avoid reallocations
    std::string responseBuffer;
    responseBuffer.reserve(8192);  // Reserve 8KB initially
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, BinanceImpl::WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
    
    // Set headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    
    if (isPrivate) {
        // Add API key header
        std::string apiKeyHeader = "X-MBX-APIKEY: " + pImpl_->apiKey;
        headers = curl_slist_append(headers, apiKeyHeader.c_str());
        
        // Add signature
        std::string signature = generateSignature(queryString);
        if (method == "GET" || method == "DELETE") {
            if (url.find('?') != std::string::npos) {
                url += "&signature=" + signature;
            } else {
                url += "?signature=" + signature;
            }
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        } else {
            // For POST, add to query string
            if (!queryString.empty()) {
                queryString += "&signature=" + signature;
            } else {
                queryString = "signature=" + signature;
            }
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, queryString.c_str());
        }
    }
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // Enforce SSL verification and allow overriding CA bundle via env
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    const char* caBundle = getenv("BINANCE_CA_BUNDLE");
    if (!caBundle) caBundle = getenv("CURL_CA_BUNDLE");
    if (caBundle) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, caBundle);
    } else {
        struct stat st;
        if (stat("/etc/ssl/certs/ca-certificates.crt", &st) == 0) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
        } else if (stat("/etc/ssl/cert.pem", &st) == 0) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/cert.pem");
        }
    }
    
    // Execute request
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    // Check HTTP response code
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    // Clean up the per-request CURL handle
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::string err = curl_easy_strerror(res);
        if (res == CURLE_SSL_CACERT || res == CURLE_SSL_CACERT_BADFILE) {
            err += std::string(" (SSL CA cert problem - check BINANCE_CA_BUNDLE/CURL_CA_BUNDLE or system CA path)");
        }
        throw std::runtime_error(std::string("CURL request failed: ") + err);
    }
    if (httpCode >= 400) {
        // Build a short context string for debugging (do not include secrets)
        std::ostringstream ctx;
        ctx << "HTTP " << httpCode << " on " << endpoint << " method=" << method;
        if (!params.empty()) {
            ctx << " params=";
            bool first = true;
            for (const auto& kv : params) {
                if (!first) ctx << ",";
                // hide values for keys that could be sensitive
                if (kv.first == "signature") {
                    ctx << kv.first << "=<sig>";
                } else {
                    ctx << kv.first << "=" << kv.second;
                }
                first = false;
            }
        }
        std::string errMsg = std::string("HTTP error ") + std::to_string(httpCode) + ": " + responseBuffer + " (" + ctx.str() + ")";
        throw std::runtime_error(errMsg);
    }
    
    return responseBuffer;
}

std::string BinanceApiClient::generateSignature(const std::string& queryString) const {
    // Create HMAC-SHA256 signature
    unsigned char* digest = HMAC(EVP_sha256(), 
                              pImpl_->apiSecret.c_str(), 
                              pImpl_->apiSecret.size(),
                              reinterpret_cast<const unsigned char*>(queryString.c_str()),
                              queryString.size(),
                              nullptr, nullptr);
    
    // Convert to hex string
    std::stringstream ss;
    for (int i = 0; i < 32; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    
    return ss.str();
}

std::string BinanceApiClient::translateOrderType(models::OrderType type) const {
    switch (type) {
        case models::OrderType::MARKET: return "MARKET";
        case models::OrderType::LIMIT: return "LIMIT";
        case models::OrderType::STOP: return "STOP_LOSS";
        case models::OrderType::STOP_LIMIT: return "STOP_LOSS_LIMIT";
        case models::OrderType::TRAILING_STOP: return "TRAILING_STOP_MARKET";
        case models::OrderType::FILL_OR_KILL: return "LIMIT";  // With timeInForce = FOK
        case models::OrderType::IMMEDIATE_OR_CANCEL: return "LIMIT";  // With timeInForce = IOC
        case models::OrderType::POST_ONLY: return "LIMIT";  // Special flag
        case models::OrderType::ICEBERG: return "LIMIT";  // With icebergQty
        default: throw std::invalid_argument("Unsupported order type for Binance");
    }
}

std::string BinanceApiClient::translateTimeInForce(models::TimeInForce tif) const {
    switch (tif) {
        case models::TimeInForce::GTC: return "GTC";  // Good Till Canceled
        case models::TimeInForce::IOC: return "IOC";  // Immediate Or Cancel
        case models::TimeInForce::FOK: return "FOK";  // models::Fill Or Kill
        case models::TimeInForce::GTD: return "GTC";  // Binance doesn't directly support GTD
        default: return "GTC";  // Default to GTC
    }
}

std::string BinanceApiClient::translateOrderSide(models::OrderSide side) const {
    switch (side) {
        case models::OrderSide::BUY: return "BUY";
        case models::OrderSide::SELL: return "SELL";
        default: throw std::invalid_argument("Invalid order side");
    }
}

// Helper: round down value to nearest multiple of step (avoid exceeding allowed precision)
double BinanceApiClient::roundDownToStep(double value, double step) const {
    if (step <= 0.0) return value;
    // Determine decimal places from step by converting to string
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss << std::setprecision(18) << step;
    std::string s = ss.str();
    // Trim trailing zeros
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    size_t pos = s.find('.');
    int decimals = 0;
    if (pos != std::string::npos) {
        decimals = (int)(s.size() - pos - 1);
    }
    double factor = std::pow(10.0, decimals);
    double stepped = std::floor(value * factor + 1e-12) / factor;
    // Ensure stepped is a multiple of step (avoid precision residue)
    double multiples = std::floor((stepped / step) + 1e-12);
    double result = multiples * step;
    // Round result to decimals
    double rounded = std::floor(result * factor + 0.5) / factor;
    return rounded;
}

// Helper: round up value to nearest multiple of step
double BinanceApiClient::roundUpToStep(double value, double step) const {
    if (step <= 0.0) return value;
    // Determine decimal places from step by converting to string
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss << std::setprecision(18) << step;
    std::string s = ss.str();
    // Trim trailing zeros
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    size_t pos = s.find('.');
    int decimals = 0;
    if (pos != std::string::npos) {
        decimals = (int)(s.size() - pos - 1);
    }
    double factor = std::pow(10.0, decimals);
    // Compute ceil to decimals then ceil to step multiples
    double stepped = std::ceil(value * factor - 1e-12) / factor;
    double multiples = std::ceil((stepped / step) - 1e-12);
    double result = multiples * step;
    // Round result to decimals
    double rounded = std::floor(result * factor + 0.5) / factor;
    return rounded;
}

// Helper: format double to string with decimals matching the step
std::string BinanceApiClient::formatDoubleWithStep(double value, double step) const {
    if (step <= 0.0) {
        return std::to_string(value);
    }
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    // Determine decimals similarly to roundDownToStep
    ss << std::setprecision(18) << step;
    std::string s = ss.str();
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    size_t pos = s.find('.');
    int decimals = 0;
    if (pos != std::string::npos) decimals = (int)(s.size() - pos - 1);
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out << std::setprecision(decimals) << value;
    return out.str();
}

models::ExecutionReport BinanceApiClient::parseOrderResponse(const std::string& response) const {
    Json::Value root;
    std::string errors;
    if (!pImpl_->parseJsonResponse(response, root, errors)) {
        throw std::runtime_error("Failed to parse Binance order response: " + errors);
    }
    
    models::ExecutionReport report;
    
    // Check for error response
    if (root.isMember("code") && root["code"].asInt() != 0) {
        report.setStatus(models::OrderState::REJECTED);
        report.setRejectReason(root["msg"].asString());
        try { std::cerr << "[BinanceApiClient] parseOrderResponse REJECTED: " << report.toString() << std::endl; } catch(...) {}
        return report;
    }
    
    // Parse successful response
    report.setExchangeId(core::ExchangeId::binance());
    report.setOrderId(root["clientOrderId"].asString());
    report.setExchangeOrderId(root["orderId"].asString());
    report.setSymbol(root["symbol"].asString());
    
    // Parse order side
    std::string side = root["side"].asString();
    report.setSide(side == "BUY" ? models::OrderSide::BUY : models::OrderSide::SELL);
    
    // Parse order type
    std::string type = root["type"].asString();
    if (type == "MARKET") {
        report.setType(models::OrderType::MARKET);
    } else if (type == "LIMIT") {
        report.setType(models::OrderType::LIMIT);
    } else if (type == "STOP_LOSS") {
        report.setType(models::OrderType::STOP);
    } else if (type == "STOP_LOSS_LIMIT") {
        report.setType(models::OrderType::STOP_LIMIT);
    } else {
        // Handle other types as needed
        report.setType(models::OrderType::LIMIT);
    }
    
    // Parse order status
    std::string status = root["status"].asString();
    if (status == "NEW" || status == "PARTIALLY_FILLED") {
        report.setStatus(status == "NEW" ? models::OrderState::OPEN : models::OrderState::PARTIALLY_FILLED);
    } else if (status == "FILLED") {
        report.setStatus(models::OrderState::FILLED);
    } else if (status == "CANCELED" || status == "PENDING_CANCEL" || status == "EXPIRED" || status == "REJECTED") {
        report.setStatus(
            status == "CANCELED" ? models::OrderState::CANCELED :
            status == "PENDING_CANCEL" ? models::OrderState::PENDING_CANCEL :
            status == "EXPIRED" ? models::OrderState::EXPIRED : models::OrderState::REJECTED
        );
    }
    
    // Parse quantities
    report.setQuantity(std::stod(root["origQty"].asString()));
    report.setFilledQuantity(std::stod(root["executedQty"].asString()));
    
    // Parse price if present
    if (root.isMember("price") && !root["price"].asString().empty()) {
        report.setPrice(std::stod(root["price"].asString()));
    }
    
    // OPTIMIZATION 12: More efficient fills parsing
    if (root.isMember("fills") && root["fills"].isArray()) {
        const Json::Value& fills = root["fills"];
        for (const auto& fill : fills) {
            models::Fill f;
            f.fill_id = fill["tradeId"].asString();
            f.orderId = root["orderId"].asString();
            f.quantity = std::stod(fill["qty"].asString());
            f.price = std::stod(fill["price"].asString());
            f.fee = std::stod(fill["commission"].asString());
            f.fee_currency = fill["commissionAsset"].asString();
            
            // Parse timestamp (tolerant)
            try {
                std::string ts = fill["time"].asString();
                if (!ts.empty()) {
                    if (ts.find('.') != std::string::npos) {
                        double d = std::stod(ts);
                        auto ms = std::chrono::milliseconds(static_cast<long long>(d));
                        f.timestamp = std::chrono::system_clock::time_point(ms);
                    } else {
                        auto ms = std::chrono::milliseconds(std::stoll(ts));
                        f.timestamp = std::chrono::system_clock::time_point(ms);
                    }
                }
            } catch (const std::exception &ex) {
                LOG_WARN("binance_api_client: failed to parse fill time '{}' : {}", fill["time"].asString(), ex.what());
            } catch (...) {
                LOG_WARN("binance_api_client: failed to parse fill time (unknown)");
            }
            
            report.addFill(std::move(f));  // Use move semantics
        }
    }
    
    // Set timestamps
    if (root.isMember("time")) {
        try {
            std::string ts = root["time"].asString();
            if (!ts.empty()) {
                if (ts.find('.') != std::string::npos) {
                    double d = std::stod(ts);
                    auto ms = std::chrono::milliseconds(static_cast<long long>(d));
                    report.setCreatedAt(std::chrono::system_clock::time_point(ms));
                } else {
                    auto ms = std::chrono::milliseconds(std::stoll(ts));
                    report.setCreatedAt(std::chrono::system_clock::time_point(ms));
                }
            }
        } catch (const std::exception &ex) {
            LOG_WARN("binance_api_client: failed to parse root time '{}' : {}", root["time"].asString(), ex.what());
        } catch (...) {
            LOG_WARN("binance_api_client: failed to parse root time (unknown)");
        }
    }
    
    // Add any exchange-specific data
    if (root.isMember("icebergQty")) {
        report.setExtraData("icebergQty", root["icebergQty"].asString());
    }
    
    if (root.isMember("stopPrice")) {
        report.setExtraData("stopPrice", root["stopPrice"].asString());
    }
    
    try { std::cerr << "[BinanceApiClient] parseOrderResponse -> " << report.toString() << std::endl; } catch(...) {}
    return report;
}

} // namespace execution
} // namespace trading