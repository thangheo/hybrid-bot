#include "execution/preflight/preflight.hpp"

namespace trading {
namespace execution {

PreflightResult ExecutionPreflight::check(const OrderRequest& req, const AccountBalances& bal) const {
    PreflightResult res;
    double notional = req.price * req.qty;

    // Strategy-level minimum
    if (notional < minNotional_) {
        res.allowed = false;
        res.reason = "below_strategy_min_notional";
        return res;
    }

    // BUY: check quote balance
    if (req.side == OrderRequest::Side::BUY) {
        double estimatedNeeded = notional; // fees ignored in this simple stub
        if (bal.quoteAvailable + 1e-9 < estimatedNeeded) {
            res.allowed = false;
            res.reason = "insufficient_quote_balance";
            return res;
        }
    } else { // SELL: check base balance
        if (bal.baseAvailable + 1e-9 < req.qty) {
            res.allowed = false;
            res.reason = "insufficient_asset_balance";
            return res;
        }
    }

    // If all checks pass, approve
    res.allowed = true;
    res.reason = "approved";
    return res;
}

} // namespace execution
} // namespace trading
