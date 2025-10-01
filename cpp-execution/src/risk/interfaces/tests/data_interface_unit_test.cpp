// Unit tests for MarketDataInterface (focused, isolated)
#include "risk/interfaces/market_data_interface.hpp"
#include <gtest/gtest.h>

using namespace trading;
using namespace trading::risk;

TEST(MarketDataInterfaceTest, BalanceUpdateAndQuery) {
	MarketDataInterface mdi; // default constructed, no initialize()

	// Update a balance and verify retrieval
	mdi.updateBalance(core::ExchangeId::binance(), "BTC", 1.5);
	double b = mdi.getBalance(core::ExchangeId::binance(), "BTC");
	EXPECT_DOUBLE_EQ(b, 1.5);

	auto balances = mdi.getExchangeBalances(core::ExchangeId::binance());
	ASSERT_FALSE(balances.empty());
	EXPECT_DOUBLE_EQ(balances["BTC"], 1.5);
}

TEST(MarketDataInterfaceTest, TradingFees) {
	MarketDataInterface mdi;

	// Binance should return maker/taker 0.001
	auto feesBin = mdi.getTradingFees(core::ExchangeId::binance(), std::nullopt);
	EXPECT_EQ(feesBin.size(), 2);
	EXPECT_DOUBLE_EQ(feesBin.at("maker"), 0.001);
	EXPECT_DOUBLE_EQ(feesBin.at("taker"), 0.001);

	// Unknown exchange should return default 0.002
	auto feesOther = mdi.getTradingFees(core::ExchangeId::unknown(), std::nullopt);
	EXPECT_DOUBLE_EQ(feesOther.at("maker"), 0.002);
	EXPECT_DOUBLE_EQ(feesOther.at("taker"), 0.002);
}

TEST(MarketDataInterfaceTest, DefaultHealthStatus) {
	MarketDataInterface mdi;
	auto health = mdi.getHealthStatus();

	// By default, without initialize(), cache/subscriber are not present
	EXPECT_TRUE(health.restClientHealthy);
	EXPECT_FALSE(health.cacheHealthy);
	EXPECT_FALSE(health.subscriberHealthy);
}

