#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "execution/balance_validator.hpp"
#include "execution/exchange_gateway.hpp"
#include "execution/models/order.hpp"

using namespace trading::execution;
using namespace trading::execution::models;
using namespace trading::core;
using ::testing::Return;
using ::testing::ByMove;
using ::testing::_;

// Mock ExchangeGateway
class MockExchangeGateway : public ExchangeGateway {
public:
    MOCK_METHOD((std::future<std::unordered_map<std::string, double>>), 
                getAccountBalancesAsync, (const ExchangeId&), (override));
    MOCK_METHOD(std::future<double>, 
                getCurrentPrice, (const ExchangeId&, const TradingPair&, bool), (override));
};

class BalanceValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup default config with SAFE values
        config_.defaultTradingFee = 0.001;  // 0.1%
        config_.balanceBufferPercent = 0.005;  // 0.5%
        config_.safetyMarginPercent = 0.0;  // NO extra margin
        config_.minNotionalValue = 10.0;
        config_.balanceCacheExpiry = std::chrono::milliseconds(100);
        config_.enableRealTimeValidation = true;
        
        validator_ = std::make_unique<BalanceValidator>(config_);
        mockGateway_ = std::make_shared<MockExchangeGateway>();
        validator_->setExchangeGateway(mockGateway_);
        
        exchangeId_ = ExchangeId::fromString("binance");
    }
    
    // Helper to create order
    execution::models::Order createOrder(OrderSide side, double price, double quantity, const std::string& pair = "BTCUSDT") {
        execution::models::Order order;
        order.setSide(side);
        order.setPrice(price);
        order.setQuantity(quantity);
        order.setTradingPair(TradingPair::fromString(pair));
        return order;
    }
    
    // Helper to setup mock balances
    void setupMockBalances(const std::map<std::string, double>& balances) {
        std::unordered_map<std::string, double> unordered(balances.begin(), balances.end());
        
        EXPECT_CALL(*mockGateway_, getAccountBalancesAsync(exchangeId_))
            .WillRepeatedly(Return(ByMove(std::async(std::launch::deferred, [unordered]() {
                return unordered;
            }))));
    }
    
    // Helper to setup mock price
    void setupMockPrice(double price) {
        EXPECT_CALL(*mockGateway_, getCurrentPrice(_, _, _))
            .WillRepeatedly(Return(ByMove(std::async(std::launch::deferred, [price]() {
                return price;
            }))));
    }

    BalanceValidatorConfig config_;
    std::unique_ptr<BalanceValidator> validator_;
    std::shared_ptr<MockExchangeGateway> mockGateway_;
    ExchangeId exchangeId_;
};

// ============================================================================
// TEST 1: Market Order Without Price - MUST REJECT
// ============================================================================
TEST_F(BalanceValidatorTest, MarketOrderWithoutPrice_ShouldReject) {
    // Setup: Balance có đủ nhưng không fetch được price
    setupMockBalances({{"USDT", 10000.0}});
    
    // Mock getCurrentPrice throws exception
    EXPECT_CALL(*mockGateway_, getCurrentPrice(_, _, _))
        .WillOnce(testing::Invoke([](const auto&, const auto&, bool) {
            return std::async(std::launch::deferred, []() -> double {
                throw std::runtime_error("Price feed unavailable");
            });
        }));
    
    // Create market order (price = 0)
    execution::models::Order order = createOrder(OrderSide::BUY, 0.0, 0.1, "BTCUSDT");
    
    // Validate
    auto result = validator_->validateOrder(order, exchangeId_, true);
    
    // MUST be rejected
    EXPECT_FALSE(result.isValid);
    EXPECT_THAT(result.reason, testing::HasSubstr("Cannot fetch price"));
    std::cout << "✓ Test passed: Market order rejected when price unavailable" << std::endl;
}

// ============================================================================
// TEST 2: BUY execution::models::Order With Tight Balance - OLD vs NEW
// ============================================================================
TEST_F(BalanceValidatorTest, BuyOrderWithTightBalance_NewConfigShouldPass) {
    // Scenario:
    // execution::models::Order: BUY 0.02 BTC @ 50000 USDT = 1000 USDT
    // Fee (0.1%): 1 USDT
    // Buffer (0.5%): 5 USDT
    // Total required: 1006 USDT
    // Available: 1007 USDT
    
    setupMockBalances({{"USDT", 1007.0}});
    
    execution::models::Order order = createOrder(OrderSide::BUY, 50000.0, 0.02, "BTCUSDT");
    auto result = validator_->validateOrder(order, exchangeId_, true);
    
    // With NEW config (buffer 0.5%, no safety margin):
    // Required = 1000 + 1 + 5 = 1006 USDT
    // Available = 1007 USDT
    // → SHOULD PASS
    EXPECT_TRUE(result.isValid) << "Reason: " << result.reason;
    EXPECT_EQ(result.asset, "USDT");
    EXPECT_NEAR(result.requiredBalance, 1006.0, 1.0);
    
    std::cout << "✓ Test passed: Tight balance now passes with new config" << std::endl;
    std::cout << "  Required: " << result.requiredBalance << " USDT" << std::endl;
    std::cout << "  Available: " << result.availableBalance << " USDT" << std::endl;
}

// ============================================================================
// TEST 3: Zero Balance - Should Reject Early
// ============================================================================
TEST_F(BalanceValidatorTest, ZeroBalance_ShouldRejectEarly) {
    setupMockBalances({
        {"BTC", 1.0},
        {"USDT", 0.0}  // Zero balance
    });
    
    execution::models::Order order = createOrder(OrderSide::BUY, 50000.0, 0.1, "BTCUSDT");
    auto result = validator_->validateOrder(order, exchangeId_, true);
    
    EXPECT_FALSE(result.isValid);
    EXPECT_THAT(result.reason, testing::HasSubstr("Zero USDT balance"));
    EXPECT_EQ(result.availableBalance, 0.0);
    
    std::cout << "✓ Test passed: Zero balance rejected early" << std::endl;
}

// ============================================================================
// TEST 4: Minimum Notional Value Check
// ============================================================================
TEST_F(BalanceValidatorTest, BelowMinNotional_ShouldReject) {
    setupMockBalances({{"USDT", 10000.0}});
    
    // execution::models::Order value = 50000 * 0.0001 = 5 USDT < 10 USDT min notional
    execution::models::Order order = createOrder(OrderSide::BUY, 50000.0, 0.0001, "BTCUSDT");
    auto result = validator_->validateOrder(order, exchangeId_, true);
    
    EXPECT_FALSE(result.isValid);
    EXPECT_THAT(result.reason, testing::HasSubstr("below minimum notional"));
    
    std::cout << "✓ Test passed: Below min notional rejected" << std::endl;
}

// ============================================================================
// TEST 5: SELL execution::models::Order Validation
// ============================================================================
TEST_F(BalanceValidatorTest, SellOrder_SufficientBalance) {
    setupMockBalances({{"BTC", 1.0}});
    
    // SELL 0.5 BTC
    // Required = 0.5 * 1.005 = 0.5025 BTC (with 0.5% buffer)
    execution::models::Order order = createOrder(OrderSide::SELL, 50000.0, 0.5, "BTCUSDT");
    auto result = validator_->validateOrder(order, exchangeId_, true);
    
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.asset, "BTC");
    EXPECT_NEAR(result.requiredBalance, 0.5, 0.01);
    EXPECT_NEAR(result.availableBalance, 1.0, 0.01);
    
    std::cout << "✓ Test passed: SELL order with sufficient balance" << std::endl;
}

// ============================================================================
// TEST 6: Suggested Quantity Calculation
// ============================================================================
TEST_F(BalanceValidatorTest, SuggestedQuantity_BuyOrder) {
    // Balance: 1000 USDT
    // execution::models::Order: BUY 0.03 BTC @ 50000 = 1500 USDT (insufficient)
    setupMockBalances({{"USDT", 1000.0}});
    
    execution::models::Order order = createOrder(OrderSide::BUY, 50000.0, 0.03, "BTCUSDT");
    auto result = validator_->validateOrder(order, exchangeId_, true);
    
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(result.canAdjust);
    EXPECT_GT(result.suggestedQuantity, 0.0);
    
    // Suggested quantity should be:
    // usable = 1000 * (1 - 0.005) = 995 USDT
    // maxOrderValue = 995 / 1.001 = 994.01 USDT
    // qty = 994.01 / 50000 = 0.01988 BTC
    EXPECT_NEAR(result.suggestedQuantity, 0.01988, 0.0001);
    
    std::cout << "✓ Test passed: Suggested quantity calculated correctly" << std::endl;
    std::cout << "  Original: " << order.getQuantity() << " BTC" << std::endl;
    std::cout << "  Suggested: " << result.suggestedQuantity << " BTC" << std::endl;
}

// ============================================================================
// TEST 7: Concurrent Validation - No Race Condition
// ============================================================================
TEST_F(BalanceValidatorTest, ConcurrentValidation_NoRaceCondition) {
    setupMockBalances({{"USDT", 10000.0}});
    
    // Launch 10 concurrent validations
    std::vector<std::future<BalanceValidationResult>> futures;
    for (int i = 0; i < 10; i++) {
        execution::models::Order order = createOrder(OrderSide::BUY, 50000.0, 0.01, "BTCUSDT");
        futures.push_back(validator_->validateOrderAsync(order, exchangeId_, false));
    }
    
    // All should succeed without race condition
    int successCount = 0;
    for (auto& fut : futures) {
        auto result = fut.get();
        if (result.isValid) successCount++;
    }
    
    EXPECT_EQ(successCount, 10);
    std::cout << "✓ Test passed: No race condition in concurrent validation" << std::endl;
}

// ============================================================================
// TEST 8: Cache Expiry Behavior
// ============================================================================
TEST_F(BalanceValidatorTest, CacheExpiry_RefetchesAfterTimeout) {
    int fetchCount = 0;
    
    EXPECT_CALL(*mockGateway_, getAccountBalancesAsync(exchangeId_))
        .WillRepeatedly(testing::Invoke([&fetchCount](const auto&) {
            fetchCount++;
            std::unordered_map<std::string, double> balances{{"USDT", 10000.0}};
            return std::async(std::launch::deferred, [balances]() {
                return balances;
            });
        }));
    
    execution::models::Order order = createOrder(OrderSide::BUY, 50000.0, 0.01, "BTCUSDT");
    
    // First validation - should fetch
    validator_->validateOrder(order, exchangeId_, false);
    EXPECT_EQ(fetchCount, 1);
    
    // Second validation immediately - should use cache
    validator_->validateOrder(order, exchangeId_, false);
    EXPECT_EQ(fetchCount, 1);  // No new fetch
    
    // Wait for cache to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Third validation - should fetch again
    validator_->validateOrder(order, exchangeId_, false);
    EXPECT_EQ(fetchCount, 2);  // New fetch
    
    std::cout << "✓ Test passed: Cache expiry works correctly" << std::endl;
}

// ============================================================================
// TEST 9: Symbol Parsing - Various Formats
// ============================================================================
TEST_F(BalanceValidatorTest, SymbolParsing_VariousFormats) {
    setupMockBalances({{"USDT", 10000.0}});
    
    std::vector<std::string> symbols = {
        "BTCUSDT",
        "BTC/USDT",
        "BTC-USDT",
        "ETHUSDC",
        "ETH/USDC"
    };
    
    for (const auto& symbol : symbols) {
        execution::models::Order order = createOrder(OrderSide::BUY, 50000.0, 0.01, symbol);
        auto result = validator_->validateOrder(order, exchangeId_, true);
        
        // Should successfully parse and validate
        EXPECT_NE(result.asset, "") << "Failed to parse: " << symbol;
        std::cout << "  Parsed " << symbol << " → asset: " << result.asset << std::endl;
    }
    
    std::cout << "✓ Test passed: Symbol parsing handles various formats" << std::endl;
}

// ============================================================================
// TEST 10: Case Insensitive Balance Lookup
// ============================================================================
TEST_F(BalanceValidatorTest, CaseInsensitiveBalanceLookup) {
    // Exchange might return lowercase
    setupMockBalances({{"usdt", 10000.0}});
    
    // But order uses uppercase
    execution::models::Order order = createOrder(OrderSide::BUY, 50000.0, 0.01, "BTCUSDT");
    auto result = validator_->validateOrder(order, exchangeId_, true);
    
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.asset, "USDT");
    EXPECT_NEAR(result.availableBalance, 10000.0, 0.01);
    
    std::cout << "✓ Test passed: Case-insensitive balance lookup works" << std::endl;
}

// ============================================================================
// MAIN
// ============================================================================
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    std::cout << "\n========================================" << std::endl;
    std::cout << "  BALANCE VALIDATOR UNIT TESTS" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    int result = RUN_ALL_TESTS();
    
    std::cout << "\n========================================" << std::endl;
    if (result == 0) {
        std::cout << "  ✓ ALL TESTS PASSED!" << std::endl;
    } else {
        std::cout << "  ✗ SOME TESTS FAILED" << std::endl;
    }
    std::cout << "========================================\n" << std::endl;
    
    return result;
}