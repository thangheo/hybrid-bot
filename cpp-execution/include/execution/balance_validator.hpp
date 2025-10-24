#pragma once

#include <string>
#include <map>
#include <memory>
#include <future>
#include <chrono>
#include <mutex>
#include "execution/models/order.hpp"
#include "execution/exchange_api_client.hpp"
#include "execution/exchange_gateway.hpp"
#include "core/market_types.hpp"

namespace trading {
namespace execution {

/**
 * @brief Configuration for balance validation
 */
struct BalanceValidatorConfig {
    double defaultTradingFee = 0.001; // 0.1% default
    std::map<std::string, double> exchangeTradingFees; // exchange-specific fees
    double balanceBufferPercent = 0.01; // 1% safety buffer
    std::chrono::milliseconds balanceCacheExpiry{30000}; // 30 seconds
    bool enableRealTimeValidation = true;
    double minNotionalValue = 10.0;
};

/**
 * @brief Result of balance validation
 */
struct BalanceValidationResult {
    bool isValid = false;
    std::string reason;
    double requiredBalance = 0.0;
    double availableBalance = 0.0;
    double estimatedFees = 0.0;
    std::string asset;
    
    // Suggested adjustments if order can be modified
    bool canAdjust = false;
    double suggestedQuantity = 0.0;
};

/**
 * @brief Cached balance information with timestamp
 */
struct CachedBalance {
    std::map<std::string, double> balances;
    std::chrono::steady_clock::time_point timestamp;
    bool isValid() const;
};

/**
 * @brief Professional balance validator for trading orders
 * 
 * This class provides comprehensive balance validation before order placement,
 * including real-time balance fetching, fee calculations, and intelligent
 * order adjustments to prevent insufficient balance errors.
 */
class BalanceValidator {
public:
    explicit BalanceValidator(const BalanceValidatorConfig& config = {});
    
    /**
     * @brief Validate order against current account balances
     * @param order The order to validate
     * @param exchangeId The exchange identifier
     * @param forceRefresh Force refresh of cached balances
     * @return Validation result with detailed information
     */
    std::future<BalanceValidationResult> validateOrderAsync(
        const models::Order& order, 
    const trading::core::ExchangeId& exchangeId,
        bool forceRefresh = false
    );
    
    /**
     * @brief Synchronous version of balance validation
     */
    BalanceValidationResult validateOrder(
        const models::Order& order,
    const trading::core::ExchangeId& exchangeId,
        bool forceRefresh = false
    );
    
    /**
     * @brief Set exchange gateway for balance fetching
     */
    void setExchangeGateway(std::shared_ptr<trading::execution::ExchangeGateway> gateway);
    
    /**
     * @brief Set API client for legacy support
     */
    void setApiClient(std::shared_ptr<ExchangeApiClient> client);
    
    /**
     * @brief Update configuration
     */
    void updateConfig(const BalanceValidatorConfig& config);
    
    /**
     * @brief Get current cached balances for exchange
     */
    std::map<std::string, double> getCachedBalances(const trading::core::ExchangeId& exchangeId) const;
    
    /**
     * @brief Force refresh of all cached balances
     */
    void refreshAllBalances();
    
    /**
     * @brief Clear balance cache for specific exchange
     */
    void clearBalanceCache(const trading::core::ExchangeId& exchangeId);

private:
    BalanceValidatorConfig config_;
    std::shared_ptr<trading::execution::ExchangeGateway> exchangeGateway_;
    std::shared_ptr<ExchangeApiClient> apiClient_;
    
    // Balance caching
    mutable std::mutex balanceCacheMutex_;
    std::map<trading::core::ExchangeId, CachedBalance> balanceCache_;
    
    /**
     * @brief Fetch fresh balances from exchange
     */
    std::future<std::map<std::string, double>> fetchFreshBalances(
        const trading::core::ExchangeId& exchangeId
    );
    
    /**
     * @brief Get balances (cached or fresh)
     */
    std::future<std::map<std::string, double>> getBalances(
        const trading::core::ExchangeId& exchangeId, 
        bool forceRefresh
    );
    
    /**
     * @brief Calculate required balance for order
     */
    double calculateRequiredBalance(
        const models::Order& order,
        const std::string& exchangeName
    ) const;
    
    /**
     * @brief Get trading fee for exchange
     */
    double getTradingFee(const std::string& exchangeName) const;
    
    /**
     * @brief Parse symbol to extract base and quote assets
     */
    std::pair<std::string, std::string> parseSymbol(const std::string& symbol) const;
    
    /**
     * @brief Find balance for asset (case-insensitive)
     */
    double findAssetBalance(
        const std::map<std::string, double>& balances,
        const std::string& asset
    ) const;
    
    /**
     * @brief Calculate suggested quantity adjustment
     */
    double calculateSuggestedQuantity(
        const models::Order& order,
        double availableBalance,
        const std::string& exchangeName
    ) const;
    
    /**
     * @brief Validate minimum notional value
     */
    bool validateMinNotional(const models::Order& order) const;
};

} // namespace execution
} // namespace trading