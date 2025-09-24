#pragma once

#include <string>
#include "execution/preflight/preflight.hpp"

namespace trading {
namespace risk {

class RiskStub {
public:
    RiskStub(double minNotional = 10.0) : minNotional_(minNotional) {}

    bool allow(const trading::execution::OrderRequest& req) const {
        double notional = req.price * req.qty;
        return notional >= minNotional_;
    }

private:
    double minNotional_ = 10.0;
};

} // namespace risk
} // namespace trading
