#include "execution/http_balance_client.hpp"
#include <curl/curl.h>
#include <iostream>
#include <mutex>

using json = nlohmann::json;

namespace trading {
namespace execution {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = static_cast<std::string*>(userp);
    mem->append(static_cast<char*>(contents), realsize);
    return realsize;
}

HttpBalanceClient::HttpBalanceClient(std::string baseUrl)
    : baseUrl_(std::move(baseUrl)) {
    static std::once_flag init_flag;
    std::call_once(init_flag, [](){ curl_global_init(CURL_GLOBAL_ALL); });
}

HttpBalanceClient::~HttpBalanceClient() {
    // no-op for now
}

std::optional<HttpBalanceClient::BalanceEntry> HttpBalanceClient::getBalance(const std::string& exchange, const std::string& asset, std::string& out_error) {
    std::string url = baseUrl_ + "/v1/balance/get";
    CURL* curl = curl_easy_init();
    if (!curl) {
        out_error = "curl init failed";
        return std::nullopt;
    }

    json req = {
        {"exchange", exchange},
        {"asset", asset}
    };
    std::string reqStr = req.dump();
    std::string respStr;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respStr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        out_error = std::string("curl perform failed: ") + curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        out_error = std::string("http error code: ") + std::to_string(http_code) + "; body=" + respStr;
        return std::nullopt;
    }

    try {
        auto j = json::parse(respStr);
        BalanceEntry e;
        e.free = std::stod(j.at("free").get<std::string>());
        e.locked = std::stod(j.at("locked").get<std::string>());
        e.last_update = j.at("last_update").get<std::string>();
        return e;
    } catch (const std::exception& ex) {
        out_error = std::string("json parse error: ") + ex.what() + "; body=" + respStr;
        return std::nullopt;
    }
}

std::optional<HttpBalanceClient::ValidateResult> HttpBalanceClient::validateOrder(const json& payload, std::string& out_error) {
    std::string url = baseUrl_ + "/v1/validate_order";
    CURL* curl = curl_easy_init();
    if (!curl) {
        out_error = "curl init failed";
        return std::nullopt;
    }

    std::string reqStr = payload.dump();
    std::string respStr;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respStr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        out_error = std::string("curl perform failed: ") + curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        out_error = std::string("http error code: ") + std::to_string(http_code) + "; body=" + respStr;
        return std::nullopt;
    }

    try {
        auto j = json::parse(respStr);
        ValidateResult r;
        r.is_valid = j.at("is_valid").get<bool>();
        r.required = std::stod(j.at("required").get<std::string>());
        r.available = std::stod(j.at("available").get<std::string>());
        if (j.contains("suggested_quantity") && !j.at("suggested_quantity").is_null()) {
            r.suggested_quantity = std::stod(j.at("suggested_quantity").get<std::string>());
        }
        if (j.contains("reason") && !j.at("reason").is_null()) {
            r.reason = j.at("reason").get<std::string>();
        }
        return r;
    } catch (const std::exception& ex) {
        out_error = std::string("json parse error: ") + ex.what() + "; body=" + respStr;
        return std::nullopt;
    }
}

std::optional<std::map<std::string, double>> HttpBalanceClient::getAllBalances(const std::string& exchange, std::string& out_error) {
    std::string url = baseUrl_ + "/v1/balance/all";
    CURL* curl = curl_easy_init();
    if (!curl) {
        out_error = "curl init failed";
        return std::nullopt;
    }

    json req = { {"exchange", exchange} };
    std::string reqStr = req.dump();
    std::string respStr;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respStr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        out_error = std::string("curl perform failed: ") + curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        out_error = std::string("http error code: ") + std::to_string(http_code) + "; body=" + respStr;
        return std::nullopt;
    }

    try {
        auto j = json::parse(respStr);
        std::map<std::string, double> out;
        if (j.contains("balances")) {
            auto bal = j.at("balances");
            for (auto it = bal.begin(); it != bal.end(); ++it) {
                std::string asset = it.key();
                const auto& entry = it.value();
                // entry.free is serialized as string
                double free_val = std::stod(entry.at("free").get<std::string>());
                out[asset] = free_val;
            }
        }
        return out;
    } catch (const std::exception& ex) {
        out_error = std::string("json parse error: ") + ex.what() + "; body=" + respStr;
        return std::nullopt;
    }
}

} // namespace execution
} // namespace trading
