// Minimal HTTP client to call the Python balance-service prototype
#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>

namespace trading {
namespace execution {

class HttpBalanceClient {
public:
    explicit HttpBalanceClient(std::string baseUrl);
    ~HttpBalanceClient();

    struct BalanceEntry {
        double free = 0.0;
        double locked = 0.0;
        std::string last_update;
    };

    struct ValidateResult {
        bool is_valid = false;
        double required = 0.0;
        double available = 0.0;
        double suggested_quantity = 0.0;
        std::string reason;
    };

    std::optional<BalanceEntry> getBalance(const std::string& exchange, const std::string& asset, std::string& out_error);
    std::optional<ValidateResult> validateOrder(const nlohmann::json& payload, std::string& out_error);
    std::optional<std::map<std::string, double>> getAllBalances(const std::string& exchange, std::string& out_error);

private:
    std::string baseUrl_;
    // init/cleanup helpers may be added if needed
};

} // namespace execution
} // namespace trading
