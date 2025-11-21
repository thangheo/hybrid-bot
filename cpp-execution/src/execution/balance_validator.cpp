#include "execution/balance_validator.hpp"
#include "execution/http_balance_client.hpp"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <cctype>
#include <iomanip>

namespace trading {
namespace execution {

bool CachedBalance::isValid() const {
    return !balances.empty();
}

BalanceValidator::BalanceValidator(const BalanceValidatorConfig& config)
    : config_(config) {
    
    if (config_.exchangeTradingFees.empty()) {
        config_.exchangeTradingFees["binance"] = 0.001;
        config_.exchangeTradingFees["ftx"] = 0.0007;
        config_.exchangeTradingFees["coinbase"] = 0.005;
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
        // === FIX 1: Cải thiện xử lý giá cho market orders ===
        double priceForValidation = order.getPrice();
        
        // Nếu là market order (price = 0), BẮT BUỘC phải lấy giá hiện tại
        if (priceForValidation <= 0.0) {
            std::cerr << "[BalanceValidator] Market order detected, fetching current price for " 
                      << order.getTradingPair().toString() << std::endl;
            
            try {
                if (exchangeGateway_) {
                    priceForValidation = exchangeGateway_->getCurrentPrice(
                        exchangeId, order.getTradingPair(), false).get();
                } else if (apiClient_) {
                    priceForValidation = apiClient_->getCurrentPrice(order.getTradingPair()).get();
                }
                
                if (priceForValidation <= 0.0) {
                    result.isValid = false;
                    result.reason = "Cannot validate market order: price unavailable";
                    std::cerr << "[BalanceValidator] ERROR: Failed to fetch price for market order" << std::endl;
                    return result;
                }
                
                std::cerr << "[BalanceValidator] Fetched market price: " << std::fixed 
                          << std::setprecision(8) << priceForValidation << std::endl;
                
            } catch (const std::exception& e) {
                result.isValid = false;
                result.reason = std::string("Cannot fetch price for market order: ") + e.what();
                std::cerr << "[BalanceValidator] ERROR: Exception fetching price: " << e.what() << std::endl;
                return result;
            }
        }

        // Validate minimum notional value
        double notionalValue = priceForValidation * order.getQuantity();
        if (notionalValue < config_.minNotionalValue) {
            result.isValid = false;
            result.reason = "Order value below minimum notional requirement";
            result.requiredBalance = config_.minNotionalValue;
            std::cerr << "[BalanceValidator] Order rejected: notional=" << notionalValue 
                      << " < min=" << config_.minNotionalValue << std::endl;
            return result;
        }

        // Fetch account balances
        auto balancesFuture = getBalances(exchangeId, forceRefresh);
        auto balances = balancesFuture.get();
        
        if (balances.empty()) {
            result.isValid = false;
            result.reason = "Unable to fetch account balances";
            std::cerr << "[BalanceValidator] ERROR: Empty balances returned" << std::endl;
            return result;
        }

        // Parse trading pair
        auto [baseAsset, quoteAsset] = parseSymbol(order.getTradingPair().toString());
        if (baseAsset.empty() || quoteAsset.empty()) {
            result.isValid = false;
            result.reason = "Unable to parse trading pair: " + order.getTradingPair().toString();
            return result;
        }

        std::string exchangeName = exchangeId.toString();
        std::transform(exchangeName.begin(), exchangeName.end(), exchangeName.begin(), ::tolower);
        double tradingFee = getTradingFee(exchangeName);

        // === FIX 2: Giảm safety margin và đơn giản hóa logic ===
        if (order.getSide() == models::OrderSide::BUY) {
            // BUY order validation
            result.asset = quoteAsset;
            result.availableBalance = findAssetBalance(balances, quoteAsset);

            // === FIX 3: Check balance = 0 sớm ===
            if (result.availableBalance <= 0.0) {
                result.isValid = false;
                result.reason = "Zero " + quoteAsset + " balance available";
                std::cerr << "[BalanceValidator] Insufficient balance: " << quoteAsset << " = 0" << std::endl;
                return result;
            }

            // Calculate required balance: cost + fees
            double orderCost = priceForValidation * order.getQuantity();
            double feeAmount = orderCost * tradingFee;
            result.estimatedFees = feeAmount;
            
            // === FIX 4: Chỉ dùng 1 safety buffer thay vì 2 ===
            // Dùng balanceBufferPercent (1%) làm buffer duy nhất
            double bufferAmount = orderCost * config_.balanceBufferPercent;
            result.requiredBalance = orderCost + feeAmount + bufferAmount;

            std::cerr << "[BalanceValidator] BUY validation:" << std::endl;
            std::cerr << "  Order cost: " << orderCost << " " << quoteAsset << std::endl;
            std::cerr << "  Fee (" << (tradingFee*100) << "%): " << feeAmount << " " << quoteAsset << std::endl;
            std::cerr << "  Buffer (" << (config_.balanceBufferPercent*100) << "%): " 
                      << bufferAmount << " " << quoteAsset << std::endl;
            std::cerr << "  Total required: " << result.requiredBalance << " " << quoteAsset << std::endl;
            std::cerr << "  Available: " << result.availableBalance << " " << quoteAsset << std::endl;

            if (result.availableBalance < result.requiredBalance) {
                result.isValid = false;
                result.reason = "Insufficient " + quoteAsset + " balance for BUY order";
                
                // === FIX 5: Suggested quantity với safety margin ===
                if (result.availableBalance > (feeAmount + bufferAmount)) {
                    result.canAdjust = true;
                    // Usable balance sau khi trừ buffer
                    double usableBalance = result.availableBalance * (1.0 - config_.balanceBufferPercent);
                    // Max order value sau khi trừ fees
                    double maxOrderValue = usableBalance / (1.0 + tradingFee);
                    result.suggestedQuantity = maxOrderValue / priceForValidation;
                    
                    std::cerr << "  Suggested quantity: " << result.suggestedQuantity << " " << baseAsset << std::endl;
                } else {
                    std::cerr << "  Balance too low for any adjustment" << std::endl;
                }
            } else {
                result.isValid = true;
                result.reason = "Sufficient balance available";
                std::cerr << "  ✓ Validation PASSED" << std::endl;
            }

        } else {
            // SELL order validation
            result.asset = baseAsset;
            result.availableBalance = findAssetBalance(balances, baseAsset);
            
            // === FIX 3: Check balance = 0 sớm ===
            if (result.availableBalance <= 0.0) {
                result.isValid = false;
                result.reason = "Zero " + baseAsset + " balance available";
                std::cerr << "[BalanceValidator] Insufficient balance: " << baseAsset << " = 0" << std::endl;
                return result;
            }

            // For SELL, we just need the quantity (fees deducted from proceeds)
            result.requiredBalance = order.getQuantity();
            
            // === FIX 4: Chỉ dùng buffer nhỏ cho SELL ===
            double bufferAmount = result.requiredBalance * config_.balanceBufferPercent;
            double totalRequired = result.requiredBalance + bufferAmount;

            std::cerr << "[BalanceValidator] SELL validation:" << std::endl;
            std::cerr << "  Order quantity: " << result.requiredBalance << " " << baseAsset << std::endl;
            std::cerr << "  Buffer (" << (config_.balanceBufferPercent*100) << "%): " 
                      << bufferAmount << " " << baseAsset << std::endl;
            std::cerr << "  Total required: " << totalRequired << " " << baseAsset << std::endl;
            std::cerr << "  Available: " << result.availableBalance << " " << baseAsset << std::endl;

            if (result.availableBalance < totalRequired) {
                result.isValid = false;
                result.reason = "Insufficient " + baseAsset + " balance for SELL order";
                
                if (result.availableBalance > bufferAmount) {
                    result.canAdjust = true;
                    // === FIX 5: Safe suggested quantity ===
                    result.suggestedQuantity = result.availableBalance * (1.0 - config_.balanceBufferPercent);
                    std::cerr << "  Suggested quantity: " << result.suggestedQuantity << " " << baseAsset << std::endl;
                }
            } else {
                result.isValid = true;
                result.reason = "Sufficient balance available";
                std::cerr << "  ✓ Validation PASSED" << std::endl;
            }
        }

    } catch (const std::exception& e) {
        result.isValid = false;
        result.reason = "Balance validation failed: " + std::string(e.what());
        std::cerr << "[BalanceValidator] EXCEPTION: " << e.what() << std::endl;
    }

    return result;
}

std::future<std::map<std::string, double>> BalanceValidator::fetchFreshBalances(
    const trading::core::ExchangeId& exchangeId
) {
    // Prefer HTTP balance client if available
    if (httpBalanceClient_) {
        return std::async(std::launch::async, [this, exchangeId]() {
            std::map<std::string, double> result;
            std::string err;
            auto opt = httpBalanceClient_->getAllBalances(exchangeId.toString(), err);
            if (opt) {
                return *opt;
            }
            // Fallback to gateway/client
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

    // Use ExchangeGateway or ApiClient
    return std::async(std::launch::async, [this, exchangeId]() {
        std::map<std::string, double> result;
        
        if (exchangeGateway_) {
            auto unorderedBalances = exchangeGateway_->getAccountBalancesAsync(exchangeId).get();
            result = std::map<std::string, double>(unorderedBalances.begin(), unorderedBalances.end());
        } else if (apiClient_) {
            auto balances = apiClient_->getAccountBalances().get();
            result = std::map<std::string, double>(balances.begin(), balances.end());
        }
        
        return result;
    });
}

// === FIX 6: Cải thiện getBalances để tránh race condition ===
std::future<std::map<std::string, double>> BalanceValidator::getBalances(
    const trading::core::ExchangeId& exchangeId,
    bool forceRefresh
) {
    // Try DataInterface first (non-blocking centralized cache)
    if (!forceRefresh && dataInterface_) {
        try {
            auto mdBalances = dataInterface_->getExchangeBalances(exchangeId);
            if (!mdBalances.empty()) {
                std::cerr << "[BalanceValidator] Using DataInterface balances" << std::endl;
                return std::async(std::launch::deferred, [mdBalances]() {
                    return mdBalances;
                });
            }
        } catch (const std::exception& e) {
            std::cerr << "[BalanceValidator] DataInterface error: " << e.what() << std::endl;
        }
    }

    // Check local cache
    {
        std::lock_guard<std::mutex> lock(balanceCacheMutex_);
        
        if (!forceRefresh && config_.enableRealTimeValidation) {
            auto it = balanceCache_.find(exchangeId);
            if (it != balanceCache_.end()) {
                auto age = std::chrono::steady_clock::now() - it->second.timestamp;
                if (age < config_.balanceCacheExpiry && it->second.isValid()) {
                    std::cerr << "[BalanceValidator] Using cached balances (age: " 
                              << std::chrono::duration_cast<std::chrono::milliseconds>(age).count() 
                              << "ms)" << std::endl;
                    
                    auto cachedBalances = it->second.balances;
                    return std::async(std::launch::deferred, [cachedBalances]() {
                        return cachedBalances;
                    });
                }
            }
        }
    }

    // Need to fetch fresh balances
    std::cerr << "[BalanceValidator] Fetching fresh balances..." << std::endl;
    
    return std::async(std::launch::async, [this, exchangeId]() {
        auto freshBalances = fetchFreshBalances(exchangeId).get();

        // Update cache
        {
            std::lock_guard<std::mutex> lock(balanceCacheMutex_);
            CachedBalance cached;
            cached.balances = freshBalances;
            cached.timestamp = std::chrono::steady_clock::now();
            balanceCache_[exchangeId] = cached;
            
            std::cerr << "[BalanceValidator] Cache updated with " << freshBalances.size() 
                      << " assets" << std::endl;
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
    std::string normalizedSymbol = symbol;
    
    // Remove separators
    normalizedSymbol.erase(std::remove(normalizedSymbol.begin(), normalizedSymbol.end(), '/'), normalizedSymbol.end());
    normalizedSymbol.erase(std::remove(normalizedSymbol.begin(), normalizedSymbol.end(), '-'), normalizedSymbol.end());
    
    // Common quote currencies (ordered by priority)
    std::vector<std::string> quoteCurrencies = {"USDT", "USDC", "BUSD", "USD", "BTC", "ETH", "BNB"};
    
    for (const auto& quote : quoteCurrencies) {
        if (normalizedSymbol.length() > quote.length() && 
            normalizedSymbol.substr(normalizedSymbol.length() - quote.length()) == quote) {
            std::string base = normalizedSymbol.substr(0, normalizedSymbol.length() - quote.length());
            return {base, quote};
        }
    }
    
    // Fallback
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
    // Exact match
    auto it = balances.find(asset);
    if (it != balances.end()) {
        return it->second;
    }
    
    // Case-insensitive search
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
        double tradingFee = getTradingFee(exchangeName);
        double usableBalance = availableBalance * (1.0 - config_.balanceBufferPercent);
        double maxOrderValue = usableBalance / (1.0 + tradingFee);
        
        double priceForCalculation = order.getPrice();
        if (priceForCalculation <= 0.0) {
            try {
                if (exchangeGateway_) {
                    priceForCalculation = exchangeGateway_->getCurrentPrice(
                        core::ExchangeId::fromString(exchangeName), 
                        order.getTradingPair(), false).get();
                } else if (apiClient_) {
                    priceForCalculation = apiClient_->getCurrentPrice(order.getTradingPair()).get();
                }
            } catch (...) { 
                return 0.0; 
            }
        }
        
        if (priceForCalculation <= 0.0) return 0.0;
        return maxOrderValue / priceForCalculation;
        
    } else {
        // SELL: max quantity with buffer
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