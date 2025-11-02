#include "execution/balance_validator.hpp"
#include "execution/http_balance_client.hpp"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <cctype>

namespace trading {
namespace execution {

bool CachedBalance::isValid() const {
    // Consider cache valid for configured expiry time
    return !balances.empty();
}

BalanceValidator::BalanceValidator(const BalanceValidatorConfig& config)
    : config_(config) {
    
    // Set default exchange fees if not provided
    if (config_.exchangeTradingFees.empty()) {
        config_.exchangeTradingFees["binance"] = 0.001;  // 0.1%
        config_.exchangeTradingFees["ftx"] = 0.0007;     // 0.07%
        config_.exchangeTradingFees["coinbase"] = 0.005; // 0.5%
    }
}

void BalanceValidator::setExchangeGateway(std::shared_ptr<trading::execution::ExchangeGateway> gateway) {
    exchangeGateway_ = gateway;
}

void BalanceValidator::setApiClient(std::shared_ptr<ExchangeApiClient> client) {
    apiClient_ = client;
}

void BalanceValidator::updateConfig(const BalanceValidatorConfig& config) {
    std::lock_guard<std::mutex> lock(balanceCacheMutex_);
    config_ = config;
}

std::future<BalanceValidationResult> BalanceValidator::validateOrderAsync(
    const models::Order& order,
    const trading::core::ExchangeId& exchangeId,
    bool forceRefresh
) {
    return std::async(std::launch::async, [this, order, exchangeId, forceRefresh]() {
        return validateOrder(order, exchangeId, forceRefresh);
    });
}

BalanceValidationResult BalanceValidator::validateOrder(
    const models::Order& order,
    const trading::core::ExchangeId& exchangeId,
    bool forceRefresh
) {
    BalanceValidationResult result;
    
    try {
        // Validate minimum notional first (quick check)
        if (!validateMinNotional(order)) {
            result.isValid = false;
            result.reason = "Order value below minimum notional requirement";
            return result;
        }
        
        // Get balances
        auto balancesFuture = getBalances(exchangeId, forceRefresh);
        auto balances = balancesFuture.get();
        
        if (balances.empty()) {
            result.isValid = false;
            result.reason = "Unable to fetch account balances";
            return result;
        }
        
        // Parse symbol to get base/quote assets
        auto [baseAsset, quoteAsset] = parseSymbol(order.getTradingPair().toString());
        
        if (baseAsset.empty() || quoteAsset.empty()) {
            result.isValid = false;
            result.reason = "Unable to parse trading pair: " + order.getTradingPair().toString();
            return result;
        }
        
        // Calculate required balance based on order side
        std::string exchangeName = exchangeId.toString();
        std::transform(exchangeName.begin(), exchangeName.end(), exchangeName.begin(), ::tolower);
        
        if (order.getSide() == models::OrderSide::BUY) {
            // For buy orders, check quote asset balance
            result.asset = quoteAsset;
            result.availableBalance = findAssetBalance(balances, quoteAsset);
            result.requiredBalance = calculateRequiredBalance(order, exchangeName);
            result.estimatedFees = result.requiredBalance - (order.getPrice() * order.getQuantity());
            
            // Add safety buffer + safety margin
            double bufferAmount = result.requiredBalance * config_.balanceBufferPercent;
            double safetyAmount = result.requiredBalance * config_.safetyMarginPercent;
            double totalRequired = result.requiredBalance + bufferAmount + safetyAmount;
            
            if (result.availableBalance < totalRequired) {
                result.isValid = false;
                result.reason = "Insufficient " + quoteAsset + " balance for BUY order";
                
                // Calculate suggested quantity
                if (result.availableBalance > 0) {
                    result.canAdjust = true;
                    result.suggestedQuantity = calculateSuggestedQuantity(order, result.availableBalance, exchangeName);
                }
            } else {
                result.isValid = true;
                result.reason = "Sufficient balance available";
            }
            
        } else { // SELL order
            // For sell orders, check base asset balance
            result.asset = baseAsset;
            result.availableBalance = findAssetBalance(balances, baseAsset);
            result.requiredBalance = order.getQuantity();
            
            // For sell orders, fees are typically deducted from the quote asset received,
            // but we need to ensure we have enough base asset to sell
            // Add safety buffer + safety margin for sell checks
            double bufferAmount = result.requiredBalance * config_.balanceBufferPercent;
            double safetyAmount = result.requiredBalance * config_.safetyMarginPercent;
            double totalRequired = result.requiredBalance + bufferAmount + safetyAmount;
            
            if (result.availableBalance < totalRequired) {
                result.isValid = false;
                result.reason = "Insufficient " + baseAsset + " balance for SELL order";
                
                // Calculate suggested quantity
                if (result.availableBalance > 0) {
                    result.canAdjust = true;
                    // suggested quantity should also account for safety margin
                    result.suggestedQuantity = result.availableBalance * (1.0 - config_.balanceBufferPercent - config_.safetyMarginPercent);
                }
            } else {
                result.isValid = true;
                result.reason = "Sufficient balance available";
            }
        }
        
    } catch (const std::exception& e) {
        result.isValid = false;
        result.reason = "Balance validation failed: " + std::string(e.what());
        std::cerr << "BalanceValidator error: " << e.what() << std::endl;
    }
    
    return result;
}

std::future<std::map<std::string, double>> BalanceValidator::fetchFreshBalances(
    const trading::core::ExchangeId& exchangeId
) {
    // If an HTTP authoritative balance service is configured and available,
    // prefer it for forced fresh balance reads.
    if (httpBalanceClient_) {
        return std::async(std::launch::async, [this, exchangeId]() {
            std::map<std::string, double> result;
            std::string err;
            auto opt = httpBalanceClient_->getAllBalances(exchangeId.toString(), err);
            if (opt) {
                return *opt;
            }
            // fall through to gateway/client if http call failed
            if (exchangeGateway_) {
                auto unorderedBalances = exchangeGateway_->getAccountBalancesAsync(exchangeId).get();
                std::map<std::string, double> r(unorderedBalances.begin(), unorderedBalances.end());
                return r;
            } else if (apiClient_) {
                auto balances = apiClient_->getAccountBalances().get();
                return std::map<std::string, double>(balances.begin(), balances.end());
            }
            return result;
        });
    }

    if (exchangeGateway_) {
        // Use exchange gateway with force refresh and convert unordered_map -> map
        return std::async(std::launch::async, [this, exchangeId]() {
            auto unorderedBalances = exchangeGateway_->getAccountBalancesAsync(exchangeId).get();
            std::map<std::string, double> result(unorderedBalances.begin(), unorderedBalances.end());
            return result;
        });
    } else if (apiClient_) {
        // Use legacy API client and normalize the returned container to std::map
        return std::async(std::launch::async, [this]() {
            auto balances = apiClient_->getAccountBalances().get();
            return std::map<std::string, double>(balances.begin(), balances.end());
        });
    } else {
        // Return empty future
        return std::async(std::launch::deferred, []() {
            return std::map<std::string, double>{};
        });
    }
}

std::future<std::map<std::string, double>> BalanceValidator::getBalances(
    const trading::core::ExchangeId& exchangeId,
    bool forceRefresh
) {
    std::lock_guard<std::mutex> lock(balanceCacheMutex_);
    
    // Check if we should use cached balances
    if (!forceRefresh && config_.enableRealTimeValidation) {
        auto it = balanceCache_.find(exchangeId);
        if (it != balanceCache_.end()) {
            auto age = std::chrono::steady_clock::now() - it->second.timestamp;
            if (age < config_.balanceCacheExpiry && it->second.isValid()) {
                // Return cached balances
                return std::async(std::launch::deferred, [balances = it->second.balances]() {
                    return balances;
                });
            }
        }
    }
    
    // Fetch fresh balances and update cache
    return std::async(std::launch::async, [this, exchangeId]() {
        auto freshBalances = fetchFreshBalances(exchangeId).get();
        
        // Update cache
        {
            std::lock_guard<std::mutex> lock(balanceCacheMutex_);
            CachedBalance cached;
            cached.balances = freshBalances;
            cached.timestamp = std::chrono::steady_clock::now();
            balanceCache_[exchangeId] = cached;
        }
        
        return freshBalances;
    });
}

double BalanceValidator::calculateRequiredBalance(
    const models::Order& order,
    const std::string& exchangeName
) const {
    if (order.getSide() == models::OrderSide::BUY) {
        double orderValue = order.getPrice() * order.getQuantity();
        double tradingFee = getTradingFee(exchangeName);
        return orderValue * (1.0 + tradingFee);
    } else {
        // For sell orders, the required balance is just the quantity
        return order.getQuantity();
    }
}

double BalanceValidator::getTradingFee(const std::string& exchangeName) const {
    auto it = config_.exchangeTradingFees.find(exchangeName);
    if (it != config_.exchangeTradingFees.end()) {
        return it->second;
    }
    return config_.defaultTradingFee;
}

std::pair<std::string, std::string> BalanceValidator::parseSymbol(const std::string& symbol) const {
    // Handle common formats: BTCUSDT, BTC/USDT, BTC-USDT
    std::string normalizedSymbol = symbol;
    
    // Remove separators
    normalizedSymbol.erase(std::remove(normalizedSymbol.begin(), normalizedSymbol.end(), '/'), normalizedSymbol.end());
    normalizedSymbol.erase(std::remove(normalizedSymbol.begin(), normalizedSymbol.end(), '-'), normalizedSymbol.end());
    
    // Common quote currencies (ordered by priority)
    std::vector<std::string> quoteCurrencies = {"USDT", "USDC", "USD", "BTC", "ETH", "BNB"};
    
    for (const auto& quote : quoteCurrencies) {
        if (normalizedSymbol.length() > quote.length() && 
            normalizedSymbol.substr(normalizedSymbol.length() - quote.length()) == quote) {
            std::string base = normalizedSymbol.substr(0, normalizedSymbol.length() - quote.length());
            return {base, quote};
        }
    }
    
    // Fallback: assume last 4 characters are quote currency
    if (normalizedSymbol.length() > 4) {
        return {
            normalizedSymbol.substr(0, normalizedSymbol.length() - 4),
            normalizedSymbol.substr(normalizedSymbol.length() - 4)
        };
    }
    
    return {"", ""};
}

double BalanceValidator::findAssetBalance(
    const std::map<std::string, double>& balances,
    const std::string& asset
) const {
    // Try exact match first
    auto it = balances.find(asset);
    if (it != balances.end()) {
        return it->second;
    }
    
    // Try case-insensitive search
    for (const auto& [key, value] : balances) {
        std::string upperKey = key;
        std::string upperAsset = asset;
        std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(), ::toupper);
        std::transform(upperAsset.begin(), upperAsset.end(), upperAsset.begin(), ::toupper);
        
        if (upperKey == upperAsset) {
            return value;
        }
    }
    
    return 0.0;
}

double BalanceValidator::calculateSuggestedQuantity(
    const models::Order& order,
    double availableBalance,
    const std::string& exchangeName
) const {
    if (order.getSide() == models::OrderSide::BUY) {
        // For buy orders, calculate max quantity based on available quote balance
        double tradingFee = getTradingFee(exchangeName);
        double usableBalance = availableBalance * (1.0 - config_.balanceBufferPercent);
        double maxOrderValue = usableBalance / (1.0 + tradingFee);
        return maxOrderValue / order.getPrice();
    } else {
        // For sell orders, max quantity is the available base balance minus buffer
        return availableBalance * (1.0 - config_.balanceBufferPercent);
    }
}

bool BalanceValidator::validateMinNotional(const models::Order& order) const {
    double notionalValue = order.getPrice() * order.getQuantity();
    return notionalValue >= config_.minNotionalValue;
}

std::map<std::string, double> BalanceValidator::getCachedBalances(const core::ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(balanceCacheMutex_);
    auto it = balanceCache_.find(exchangeId);
    if (it != balanceCache_.end() && it->second.isValid()) {
        return it->second.balances;
    }
    return {};
}

void BalanceValidator::refreshAllBalances() {
    std::lock_guard<std::mutex> lock(balanceCacheMutex_);
    balanceCache_.clear();
}

void BalanceValidator::clearBalanceCache(const core::ExchangeId& exchangeId) {
    std::lock_guard<std::mutex> lock(balanceCacheMutex_);
    balanceCache_.erase(exchangeId);
}

} // namespace execution
} // namespace trading