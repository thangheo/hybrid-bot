#pragma once

#include <string>
#include <optional>

namespace trading {
namespace execution {

struct AccountBalances {
    double quoteAvailable = 0.0; // e.g., USDT
    double baseAvailable = 0.0;  // e.g., BTC, ETH
};

struct OrderRequest {
    std::string symbol;
    enum class Side { BUY, SELL } side;
    double price = 0.0;
    double qty = 0.0;
};

struct PreflightResult {
    bool allowed = false;
    std::string reason;
    std::optional<OrderRequest> adjustedOrder;
};

// Very small preflight checker used for tests/demo.
class ExecutionPreflight {
public:
    ExecutionPreflight(double minNotional = 10.0)
        : minNotional_(minNotional) {}

    PreflightResult check(const OrderRequest& req, const AccountBalances& bal) const;

private:
    double minNotional_ = 10.0; // minimum trade value in quote currency
};

} // namespace execution
} // namespace trading
