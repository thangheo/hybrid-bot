#include <gtest/gtest.h>
#include "execution/preflight/preflight.hpp"
#include "risk/preflight/risk_preflight.hpp"

using namespace trading::execution;
using namespace trading::risk;

TEST(PreflightTest, BuyInsufficientQuote) {
    ExecutionPreflight preflight(10.0);
    OrderRequest req{"ETHUSDT", OrderRequest::Side::BUY, 2000.0, 0.01}; // notional = 20
    AccountBalances bal; bal.quoteAvailable = 5.0; bal.baseAvailable = 0.0;

    auto res = preflight.check(req, bal);
    EXPECT_FALSE(res.allowed);
    EXPECT_EQ(res.reason, "insufficient_quote_balance");
}

TEST(PreflightTest, BuySufficientQuote) {
    ExecutionPreflight preflight(10.0);
    OrderRequest req{"ETHUSDT", OrderRequest::Side::BUY, 2000.0, 0.01}; // notional = 20
    AccountBalances bal; bal.quoteAvailable = 100.0; bal.baseAvailable = 0.0;

    auto res = preflight.check(req, bal);
    EXPECT_TRUE(res.allowed);
    EXPECT_EQ(res.reason, "approved");
}

TEST(PreflightTest, SellInsufficientBase) {
    ExecutionPreflight preflight(10.0);
    OrderRequest req{"ETHUSDT", OrderRequest::Side::SELL, 2000.0, 0.01};
    AccountBalances bal; bal.quoteAvailable = 0.0; bal.baseAvailable = 0.005;

    auto res = preflight.check(req, bal);
    EXPECT_FALSE(res.allowed);
    EXPECT_EQ(res.reason, "insufficient_asset_balance");
}

TEST(RiskStubTest, RejectsBelowMinNotional) {
    RiskStub risk(50.0);
    OrderRequest req{"ETHUSDT", OrderRequest::Side::BUY, 2000.0, 0.01}; // notional = 20
    EXPECT_FALSE(risk.allow(req));
}
