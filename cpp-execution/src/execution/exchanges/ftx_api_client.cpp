// exchanges/ftx_api_client.cpp
#include "execution/exchanges/ftx_api_client.hpp"
#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <sstream>
#include <iomanip>
#include <openssl/hmac.h>
#include <chrono>
#include <thread>
#include <future>
#include <stdexcept>
#include <iostream>
#include <map>
namespace trading {
namespace execution {


// Private implementation class
struct FtxApiClient::FtxImpl {
    std::string apiKey;
    std::string apiSecret;
    std::string subaccount;  // FTX supports subaccounts
    std::string baseUrl = "https://ftx.com";
    bool connected = false;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl;
    
    // Constructor
    FtxImpl(const ExchangeCredentials& credentials) 
        : apiKey(credentials.apiKey), 
          apiSecret(credentials.apiSecret),
          curl(curl_easy_init(), curl_easy_cleanup) {
        
        // If credentials.passphrase contains subaccount info, use it
        if (!credentials.passphrase.empty()) {
            subaccount = credentials.passphrase;
        }
        
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
    }
    
    // CURL callback for writing response
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
};


FtxApiClient::FtxApiClient(const ExchangeCredentials& credentials, 
                         std::shared_ptr<RateLimitController> rateLimitController)
    : pImpl_(std::make_unique<FtxImpl>(credentials)),
      rateLimitController_(std::move(rateLimitController)) {
    // Initialize curl globally if not already done
    static std::once_flag flag;
    std::call_once(flag, []() { curl_global_init(CURL_GLOBAL_ALL); });
}

FtxApiClient::~FtxApiClient() = default;

void FtxApiClient::connect() {
    if (pImpl_->connected) return;
    
    try {
        // Test connection by getting markets info
        std::map<std::string, std::string> params;
        std::string response = makeApiRequest("/api/markets", "GET", params, false);
        
        // Parse response to verify it's valid
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            throw std::runtime_error("Failed to parse FTX markets info");
        }
        
        if (!root["success"].asBool()) {
            throw std::runtime_error("FTX API error: " + root["error"].asString());
        }
        
        pImpl_->connected = true;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to connect to FTX: ") + e.what());
    }
}

void FtxApiClient::disconnect() {
    pImpl_->connected = false;
}

bool FtxApiClient::checkConnection() const {
    if (!pImpl_->connected) {
        return false;
    }
    
    // Actually test the connection by making a lightweight API call
    try {
        std::map<std::string, std::string> params;
        std::string response = makeApiRequest("/api/markets", "GET", params, false);
        
        // Parse response to verify it's valid
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            throw std::runtime_error("Failed to parse FTX markets info");
        }
        
        return root["success"].asBool();
    } catch (const std::exception& e) {
        // Log the error but don't throw it
        std::cerr << "Connection check failed: " << e.what() << std::endl;
        
        // Connection test failed, so we're not connected
        pImpl_->connected = false;
        return false;
    }
}

bool FtxApiClient::isConnected() const {
    return pImpl_->connected;
}

std::string FtxApiClient::getExchangeName() const {
    return "FTX";
}

std::vector<std::string> FtxApiClient::getSupportedSymbols() const {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to FTX");
    }
    
    // Get markets info to retrieve supported symbols
    std::map<std::string, std::string> params;
    std::string response = makeApiRequest("/api/markets", "GET", params, false);
    
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(response, root)) {
        throw std::runtime_error("Failed to parse FTX markets info");
    }
    
    if (!root["success"].asBool()) {
        throw std::runtime_error("FTX API error: " + root["error"].asString());
    }
    
    std::vector<std::string> symbols;
    const Json::Value& marketsJson = root["result"];
    for (const auto& marketJson : marketsJson) {
        if (marketJson["enabled"].asBool()) {
            symbols.push_back(marketJson["name"].asString());
        }
    }
    
    return symbols;
}
// In ftx_api_client.cpp

std::string FtxApiClient::getHealthStatus() {
    if (!isConnected()) {
        connect();
    }
    
    // Wait for rate limit
    rateLimitController_->waitForPermission(ExchangeRequestType::GetHealthStatus);
    
    // Prepare parameters
    std::map<std::string, std::string> params;
    
    // Send request - adjust endpoint as needed for FTX
    std::string response = makeApiRequest("/api/health", "GET", params, false);
    
    return response;
}

std::unordered_map<std::string, std::string> FtxApiClient::getExchangeStatuses() const {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to FTX");
    }
    
    // Implement logic similar to BinanceApiClient::getExchangeStatuses but for FTX
    // Return a map of symbol -> status
    std::unordered_map<std::string, std::string> statuses;
    
    // Example implementation:
    try {
        // Use const-safe methods only since this is a const method
        std::map<std::string, std::string> params;
        std::string response = makeApiRequest("/api/markets", "GET", params, false);
        
        // Parse response JSON and populate the statuses map
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(response, root)) {
            const Json::Value& markets = root["result"];
            for (const auto& market : markets) {
                std::string symbol = market["name"].asString();
                bool enabled = market["enabled"].asBool();
                statuses[symbol] = enabled ? "TRADING" : "BREAK";
            }
        }
    } catch (const std::exception&) {
        // Handle exception (possibly by returning an empty map or throwing)
        throw std::runtime_error("Failed to get exchange statuses from FTX");
    }
    
    return statuses;
}
std::future<models::ExecutionReport> FtxApiClient::placeOrder(const models::Order& order) {
    // Extract necessary data from the order to avoid capturing the entire object
    core::Symbol tradingPair = order.getTradingPair();
    models::OrderSide side = order.getSide();
    models::OrderType type = order.getType();
    double quantity = order.getQuantity();
    double price = order.getPrice();
    models::TimeInForce timeInForce = order.getTimeInForce();
    std::string clientOrderId = order.getClientOrderId();
    
    // Create a copy of the extra parameters
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
        params["market"] = tradingPair.toString();
        params["side"] = translateOrderSide(side);
        params["type"] = translateOrderType(type);
        params["size"] = std::to_string(quantity);
        
        if (type != models::OrderType::MARKET) {
            params["price"] = std::to_string(price);
        }
        
        // FTX supports client order IDs via the clientId field
        if (!clientOrderId.empty()) {
            params["clientId"] = clientOrderId;
        }
        
        // Check for IOC/FOK
        if (timeInForce == models::TimeInForce::IOC) {
            params["ioc"] = "true";
        } else if (timeInForce == models::TimeInForce::FOK) {
            // FTX doesn't support FOK directly, we use IOC as closest option
            params["ioc"] = "true";
        }
        
        // Check for post-only
        if (type == models::OrderType::POST_ONLY) {
            params["postOnly"] = "true";
        }
        
        // Add extra parameters if present
        for (const auto& [key, value] : extraParams) {
            params[key] = value;
        }
        
        // Send request
        std::string response = makeApiRequest("/api/orders", "POST", params, true);
        
        // Parse response
        return parseOrderResponse(response);
    });
}

std::future<models::ExecutionReport> FtxApiClient::cancelOrder(const std::string& orderId, const core::Symbol& symbol) {
    // Already using value types, so no need to extract anything
    return std::async(std::launch::async, [this, orderId, symbol]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::CancelOrder);
        
        // Check if orderId is a client order ID or exchange order ID
        std::string endpoint;
        if (orderId.find("CLIENT:") == 0) {
            // It's a client order ID
            endpoint = "/api/orders/by_client_id/" + orderId.substr(7);
        } else {
            // It's an exchange order ID
            endpoint = "/api/orders/" + orderId;
        }
        
        // Send request
        std::map<std::string, std::string> params;
        std::string response = makeApiRequest(endpoint, "DELETE", params, true);
        
        // Parse response
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            throw std::runtime_error("Failed to parse FTX cancel order response");
        }
        
        if (!root["success"].asBool()) {
            throw std::runtime_error("FTX API error: " + root["error"].asString());
        }
        
        // Get order status after cancellation
        return getOrderStatus(orderId, symbol).get();
    });
}

std::future<models::ExecutionReport> FtxApiClient::getOrderStatus(const std::string& orderId, const core::Symbol& symbol) {
    // Already using value types, so no need to extract anything
    return std::async(std::launch::async, [this, orderId]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::GetOrderStatus);
        
        // Check if orderId is a client order ID or exchange order ID
        std::string endpoint;
        if (orderId.find("CLIENT:") == 0) {
            // It's a client order ID
            endpoint = "/api/orders/by_client_id/" + orderId.substr(7);
        } else {
            // It's an exchange order ID
            endpoint = "/api/orders/" + orderId;
        }
        
        // Send request
        std::map<std::string, std::string> params;
        std::string response = makeApiRequest(endpoint, "GET", params, true);
        
        // Parse response
        return parseOrderResponse(response);
    });
}

std::future<std::map<std::string, double>> FtxApiClient::getAccountBalances() {
    return std::async(std::launch::async, [this]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::GetAccountBalance);
        
        // Send request
        std::map<std::string, std::string> params;
        std::string response = makeApiRequest("/api/wallet/balances", "GET", params, true);
        
        // Parse response
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            throw std::runtime_error("Failed to parse FTX account balances");
        }
        
        if (!root["success"].asBool()) {
            throw std::runtime_error("FTX API error: " + root["error"].asString());
        }
        
        std::map<std::string, double> balances;
        const Json::Value& balancesJson = root["result"];
        for (const auto& balanceJson : balancesJson) {
            std::string coin = balanceJson["coin"].asString();
            double total = balanceJson["total"].asDouble();
            
            balances[coin] = total;
        }
        
        return balances;
    });
}

std::future<double> FtxApiClient::getCurrentPrice(const core::Symbol& symbol) {
    return std::async(std::launch::async, [this, symbol]() {
        if (!isConnected()) {
            connect();
        }
        
        // Wait for rate limit
        rateLimitController_->waitForPermission(ExchangeRequestType::GetMarketData);
        
        // Send request
        std::map<std::string, std::string> params;
        std::string response = makeApiRequest("/api/markets/" + symbol.toString(), "GET", params, false);

        // Parse response
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            throw std::runtime_error("Failed to parse FTX market data");
        }
        
        if (!root["success"].asBool()) {
            throw std::runtime_error("FTX API error: " + root["error"].asString());
        }
        
        return root["result"]["last"].asDouble();
    });
}

int FtxApiClient::getRemainingRequests(ExchangeRequestType requestType) const {
    return rateLimitController_->getRemainingRequests(requestType);
}

std::chrono::milliseconds FtxApiClient::getResetTimeForRequestType(ExchangeRequestType requestType) const {
    return rateLimitController_->getResetTime(requestType);
}

std::string FtxApiClient::makeApiRequest(const std::string& endpoint, const std::string& method, 
                                       const std::map<std::string, std::string>& params, bool isPrivate) const {
    std::string url = pImpl_->baseUrl + endpoint;
    std::string queryString;
    std::string bodyString;
    
    // For GET requests with parameters, build query string
    if (method == "GET" && !params.empty()) {
        std::stringstream ss;
        for (const auto& [key, value] : params) {
            if (!ss.str().empty()) {
                ss << "&";
            }
            ss << key << "=" << value;
        }
        queryString = ss.str();
        url += "?" + queryString;
    } else if (method == "POST" || method == "PUT") {
        // For POST/PUT, create JSON body
        Json::Value body;
        for (const auto& [key, value] : params) {
            // Need to handle numeric values specially
            if (key == "size" || key == "price") {
                body[key] = std::stod(value);
            } else if (value == "true") {
                body[key] = true;
            } else if (value == "false") {
                body[key] = false;
            } else {
                body[key] = value;
            }
        }
        
        Json::FastWriter writer;
        bodyString = writer.write(body);
        // Remove trailing newline
        if (!bodyString.empty() && bodyString[bodyString.size() - 1] == '\n') {
            bodyString.erase(bodyString.size() - 1);
        }
    }
    
    // Set up CURL
    curl_easy_reset(pImpl_->curl.get());
    curl_easy_setopt(pImpl_->curl.get(), CURLOPT_URL, url.c_str());
    
    // Set request method
    if (method == "POST") {
        curl_easy_setopt(pImpl_->curl.get(), CURLOPT_POST, 1L);
        if (!bodyString.empty()) {
            curl_easy_setopt(pImpl_->curl.get(), CURLOPT_POSTFIELDS, bodyString.c_str());
        }
    } else if (method == "PUT") {
        curl_easy_setopt(pImpl_->curl.get(), CURLOPT_CUSTOMREQUEST, "PUT");
        if (!bodyString.empty()) {
            curl_easy_setopt(pImpl_->curl.get(), CURLOPT_POSTFIELDS, bodyString.c_str());
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(pImpl_->curl.get(), CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    // Set response callback
    std::string responseBuffer;
    curl_easy_setopt(pImpl_->curl.get(), CURLOPT_WRITEFUNCTION, FtxImpl::WriteCallback);
    curl_easy_setopt(pImpl_->curl.get(), CURLOPT_WRITEDATA, &responseBuffer);
    
    // Set headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    if (isPrivate) {
        // Get timestamp
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::string timestamp = std::to_string(ms);
        
        // Generate signature
        std::string signaturePayload = timestamp + method + endpoint;
        if (method == "GET" && !queryString.empty()) {
            signaturePayload += "?" + queryString;
        } else if ((method == "POST" || method == "PUT") && !bodyString.empty()) {
            signaturePayload += bodyString;
        }
        
        std::string signature = generateSignature(endpoint, method, timestamp, bodyString);
        
        // Add authentication headers
        headers = curl_slist_append(headers, ("FTX-KEY: " + pImpl_->apiKey).c_str());
        headers = curl_slist_append(headers, ("FTX-TS: " + timestamp).c_str());
        headers = curl_slist_append(headers, ("FTX-SIGN: " + signature).c_str());
        
        // Add subaccount header if set
        if (!pImpl_->subaccount.empty()) {
            headers = curl_slist_append(headers, ("FTX-SUBACCOUNT: " + pImpl_->subaccount).c_str());
        }
    }
    
    curl_easy_setopt(pImpl_->curl.get(), CURLOPT_HTTPHEADER, headers);
    
    // Execute request
    CURLcode res = curl_easy_perform(pImpl_->curl.get());
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
    }
    
    // Check HTTP response code
    long httpCode = 0;
    curl_easy_getinfo(pImpl_->curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);
    
    if (httpCode >= 400) {
        throw std::runtime_error("HTTP error " + std::to_string(httpCode) + ": " + responseBuffer);
    }
    
    return responseBuffer;
}

std::string FtxApiClient::generateSignature(const std::string& endpoint, const std::string& method, 
                                          const std::string& timestamp, const std::string& body) const {
    // Construct payload: timestamp + method + path + body
    std::string payload = timestamp + method + endpoint;
    if (!body.empty()) {
        payload += body;
    }
    
    // Create HMAC-SHA256 signature
    unsigned char* digest = HMAC(EVP_sha256(), 
                              pImpl_->apiSecret.c_str(), 
                              pImpl_->apiSecret.size(),
                              reinterpret_cast<const unsigned char*>(payload.c_str()),
                              payload.size(),
                              nullptr, nullptr);
    
    // Convert to hex string
    std::stringstream ss;
    for (int i = 0; i < 32; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    
    return ss.str();
}

std::string FtxApiClient::translateOrderType(models::OrderType type) const {
    switch (type) {
        case models::OrderType::MARKET: return "market";
        case models::OrderType::LIMIT: return "limit";
        // FTX doesn't directly support these order types, map to closest equivalent
        case models::OrderType::STOP: 
        case models::OrderType::STOP_LIMIT:
        case models::OrderType::TRAILING_STOP:
        case models::OrderType::FILL_OR_KILL:
        case models::OrderType::IMMEDIATE_OR_CANCEL:
        case models::OrderType::POST_ONLY:
        case models::OrderType::ICEBERG:
            return "limit";  // Additional parameters set separately
        default: throw std::invalid_argument("Unsupported order type for FTX");
    }
}

std::string FtxApiClient::translateOrderSide(models::OrderSide side) const {
    switch (side) {
        case models::OrderSide::BUY: return "buy";
        case models::OrderSide::SELL: return "sell";
        default: throw std::invalid_argument("Invalid order side");
    }
}

models::ExecutionReport FtxApiClient::parseOrderResponse(const std::string& response) const {
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(response, root)) {
        throw std::runtime_error("Failed to parse FTX order response");
    }
    
    if (!root["success"].asBool()) {
        models::ExecutionReport report;
        report.setStatus(models::OrderState::REJECTED);
        report.setRejectReason(root["error"].asString());
        return report;
    }
    
    const Json::Value& result = root["result"];
    models::ExecutionReport report;
    
    // Set exchange info
    report.setExchangeId(core::ExchangeId::ftx());
    report.setExchangeOrderId(result["id"].asString());
    
    // Set client order ID if present
    if (result.isMember("clientId") && !result["clientId"].isNull()) {
        report.setClientOrderId(result["clientId"].asString());
    }
    
    // Set market symbol
    report.setSymbol(result["market"].asString());
    
    // Set side
    std::string side = result["side"].asString();
    report.setSide(side == "buy" ? models::OrderSide::BUY : models::OrderSide::SELL);
    
    // Set type
    std::string type = result["type"].asString();
    report.setType(type == "market" ? models::OrderType::MARKET : models::OrderType::LIMIT);
    
    // Set price if present
    if (result.isMember("price") && !result["price"].isNull()) {
        report.setPrice(result["price"].asDouble());
    }
    
    // Set quantities
    report.setQuantity(result["size"].asDouble());
    report.setFilledQuantity(result["filledSize"].asDouble());
    report.setAverageFillPrice(result["avgFillPrice"].isNull() ? 0.0 : result["avgFillPrice"].asDouble());
    
    // Set status
    std::string status = result["status"].asString();
    if (status == "new") {
        report.setStatus(models::OrderState::OPEN);
    } else if (status == "open") {
        double remainingSize = result["remainingSize"].asDouble();
        report.setStatus(remainingSize < result["size"].asDouble() ? models::OrderState::PARTIALLY_FILLED : models::OrderState::OPEN);
    } else if (status == "closed") {
        // Check if it was filled or canceled
        if (report.filledQuantity() >= report.quantity() * 0.99) {  // Allow for tiny rounding differences
            report.setStatus(models::OrderState::FILLED);
        } else {
            report.setStatus(models::OrderState::CANCELED);
        }
    }
    
    // Set timestamps
    auto createTimeMs = std::chrono::milliseconds(static_cast<long long>(result["createdAt"].asDouble() * 1000));
    report.setCreatedAt(std::chrono::system_clock::time_point(createTimeMs));
    
    // Set extra data
    if (result.isMember("reduceOnly")) {
        report.setExtraData("reduceOnly", result["reduceOnly"].asBool() ? "true" : "false");
    }
    
    if (result.isMember("ioc")) {
        report.setExtraData("ioc", result["ioc"].asBool() ? "true" : "false");
    }
    
    if (result.isMember("postOnly")) {
        report.setExtraData("postOnly", result["postOnly"].asBool() ? "true" : "false");
    }
    
    return report;
}

} // namespace execution
} // namespace trading