/**
 * Implementation of the collateral management component
 *
 * File: collateral_manager.cpp
 * Created: 2025-04-29
 */

#include "portfolio/collateral_manager.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <random>
#include <future>

namespace trading {
namespace risk {

CollateralManager::CollateralManager(const RiskConfig& config, 
                                   std::shared_ptr<PositionTracker> positionTracker,
                                   std::shared_ptr<ExecutionInterface> executionInterface,
                                   std::shared_ptr<DataInterface> dataInterface)
    : m_positionTracker(positionTracker)
    , m_executionInterface(executionInterface)
    , m_dataInterface(dataInterface)
    , m_config(config)
    , m_collateralConfig(config.getCollateralConfig()) {
    
    std::cerr << "[CollateralManager] Constructor - managedExchanges count: " << m_collateralConfig.managedExchanges.size() << std::endl;
    
    // CRASH PROTECTION: Validate critical pointers
    if (!m_executionInterface) {
        std::cerr << "[CollateralManager] WARNING: ExecutionInterface is null!" << std::endl;
    }
    if (!m_positionTracker) {
        std::cerr << "[CollateralManager] WARNING: PositionTracker is null!" << std::endl;
    }
    if (!m_dataInterface) {
        std::cerr << "[CollateralManager] WARNING: DataInterface is null!" << std::endl;
    }
    
    // Initialize default collateral status
    m_collateralStatus.totalCollateral = 0.0;
    m_collateralStatus.freeCollateral = 0.0;
    m_collateralStatus.freeCollateralRatio = 1.0;
    m_collateralStatus.belowMinimumThreshold = false;
    m_collateralStatus.emergencyFundingNeeded = false;
    m_collateralStatus.timestamp = std::chrono::system_clock::now();
}

bool CollateralManager::initialize() {
    try {
        // Fetch initial balances
        if (!fetchExchangeBalances()) {
            // std::cerr << "Warning: Initial balance fetch failed" << std::endl;
            // Continue anyway as this might recover later
        }
        
        // Register position update callback to monitor collateral levels
        if (m_positionTracker) {
            m_positionTracker->registerPositionUpdateCallback(
                [this](const Position& position, const std::string& updateType) {
                    try {
                        // Trigger a collateral update when positions change significantly
                        if (updateType == "new" || updateType == "modified" || updateType == "reconciliation") {
                            // Check if object is still valid before calling methods
                            if (m_executionInterface && m_dataInterface) {
                                updateCollateralStatus();
                            }
                        }
                    } catch (...) {
                        // Silently ignore all errors in callbacks to prevent crashes
                    }
                }
            );
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error initializing CollateralManager: " << e.what() << std::endl;
        return false;
    }
}
bool CollateralManager::shutdown() {
    try {
        // Perform any necessary cleanup
        // Currently, there are no persistent connections or threads to clean up
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during CollateralManager shutdown: " << e.what() << std::endl;
        return false;
    }
}

bool CollateralManager::checkCollateralRequirements(const OrderRequest& orderRequest, 
                                                  std::vector<RiskViolation>& violations) {
    try {
        // Extract order details
        ExchangeId exchangeId{orderRequest.exchangeId};
        std::string tradingPairStr = orderRequest.tradingPair.toString();
        OrderSide side = orderRequest.side;
        double quantity = orderRequest.quantity;
        double price = orderRequest.price;
        
        // Parse trading pair
        size_t delimiterPos = tradingPairStr.find('/');
        if (delimiterPos == std::string::npos) {
            std::cerr << "Invalid trading pair format: " << tradingPairStr << std::endl;
            violations.push_back({
                ViolationType::OTHER,
                "Trading pair format is invalid",
                0.0,
                0.0
            });
            return false;
        }
        
        std::string baseAsset = tradingPairStr.substr(0, delimiterPos);
        std::string quoteAsset = tradingPairStr.substr(delimiterPos + 1);
        
        // Create TradingPair struct
        TradingPair tradingPair;
        tradingPair.fromString(tradingPairStr);
        
        // For buy orders, we need quote asset as collateral
        // For sell orders, we need base asset as collateral
        std::string collateralAsset;
        double requiredAmount;
        
        if (side == OrderSide::BUY) {
            collateralAsset = quoteAsset;
            requiredAmount = quantity * price;
            
            // Check if we have enough collateral
            if (!hasSufficientCollateral(exchangeId, collateralAsset, requiredAmount)) {
                // Get actual balance
                double availableBalance = 0.0;
                auto balance = getExchangeBalance(exchangeId, collateralAsset);
                if (balance.exchangeId.toString() == exchangeId.toString()) {
                    availableBalance = balance.free;
                }
                
                violations.push_back({
                    ViolationType::COLLATERAL_REQUIREMENT,
                    "Insufficient " + collateralAsset + " for buy order",
                    requiredAmount,
                    availableBalance
                });
                return false;
            }
            
            // Check margin requirements for leveraged trading
            double marginRequired = calculateMarginRequirement(exchangeId, tradingPair, quantity);
            if (marginRequired > 0) {
                if (!hasSufficientCollateral(exchangeId, collateralAsset, marginRequired)) {
                    double availableBalance = 0.0;
                    auto balance = getExchangeBalance(exchangeId, collateralAsset);
                    if (balance.exchangeId.toString() == exchangeId.toString()) {
                        availableBalance = balance.free;
                    }
                    
                    violations.push_back({
                        ViolationType::COLLATERAL_REQUIREMENT,
                        "Insufficient margin for leveraged position",
                        marginRequired,
                        availableBalance
                    });
                    return false;
                }
            }
        } else if (side == OrderSide::SELL) {
            collateralAsset = baseAsset;
            requiredAmount = quantity;
            
            // Check if we have enough of the asset to sell
            if (!hasSufficientCollateral(exchangeId, collateralAsset, requiredAmount)) {
                double availableBalance = 0.0;
                auto balance = getExchangeBalance(exchangeId, collateralAsset);
                if (balance.exchangeId.toString() == exchangeId.toString()) {
                    availableBalance = balance.free;
                }
                
                violations.push_back({
                    ViolationType::COLLATERAL_REQUIREMENT,
                    "Insufficient " + collateralAsset + " for sell order",
                    requiredAmount,
                    availableBalance
                });
                return false;
            }
        } else {
            violations.push_back({
                ViolationType::OTHER,
                "Invalid order side",
                0.0,
                0.0
            });
            return false;
        }
        
        // Check if free collateral ratio would go below minimum
        double currentFreeCollateralRatio = calculateFreeCollateralRatio();
        double estimatedImpact = 0.0;
        
        // Simple estimation of impact on free collateral ratio
        if (side == OrderSide::BUY) {
            double orderValue = quantity * price;
            double totalCollateral = calculateTotalCollateral();
            if (totalCollateral > 0) {
                estimatedImpact = orderValue / totalCollateral;
            }
        }
        
        double projectedRatio = currentFreeCollateralRatio - estimatedImpact;
        if (projectedRatio < m_collateralConfig.minimumFreeCollateralRatio) {
            violations.push_back({
                ViolationType::COLLATERAL_REQUIREMENT,
                "Order would reduce free collateral ratio below minimum",
                m_collateralConfig.minimumFreeCollateralRatio,
                projectedRatio
            });
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error checking collateral requirements: " << e.what() << std::endl;
        violations.push_back({
            ViolationType::OTHER,
            std::string("Internal error: ") + e.what(),
            0.0,
            0.0
        });
        return false;
    }
}

bool CollateralManager::updateCollateralStatus() {
    try {
        // Fetch latest balances
        bool balancesUpdated = fetchExchangeBalances();
        
        // Calculate collateral metrics
        double totalCollateral = calculateTotalCollateral();
        double freeCollateral = calculateFreeCollateral();
        double freeCollateralRatio = calculateFreeCollateralRatio();
        bool belowMinimum = freeCollateralRatio < m_collateralConfig.minimumFreeCollateralRatio;
        bool emergencyNeeded = isEmergencyFundingNeeded();
        
        // Update status
        {
            std::lock_guard<std::mutex> lock(m_collateralMutex);
            m_collateralStatus.totalCollateral = totalCollateral;
            m_collateralStatus.freeCollateral = freeCollateral;
            m_collateralStatus.freeCollateralRatio = freeCollateralRatio;
            m_collateralStatus.belowMinimumThreshold = belowMinimum;
            m_collateralStatus.emergencyFundingNeeded = emergencyNeeded;
            m_collateralStatus.balances = m_balances;
            m_collateralStatus.timestamp = std::chrono::system_clock::now();
        }
        
        // Notify about critical collateral events
        if (belowMinimum) {
            notifyCollateralEvent("below_minimum", 
                                "Free collateral ratio " + std::to_string(freeCollateralRatio) + 
                                " below minimum " + std::to_string(m_collateralConfig.minimumFreeCollateralRatio));
        }
        
        if (emergencyNeeded) {
            notifyCollateralEvent("emergency_funding", "Emergency funding needed");
            
            // Auto-initiate emergency funding if configured
            if (m_collateralConfig.autoInitiateEmergencyFunding) {
                initiateEmergencyFunding();
            }
        }
        
        return balancesUpdated;
    } catch (const std::exception& e) {
        std::cerr << "Error updating collateral status: " << e.what() << std::endl;
        return false;
    }
}

CollateralStatus CollateralManager::getCollateralStatus() const {
    std::lock_guard<std::mutex> lock(m_collateralMutex);
    return m_collateralStatus;
}

std::vector<ExchangeBalance> CollateralManager::getExchangeBalances(
    const std::optional<ExchangeId>& exchangeId) const {
    
    std::lock_guard<std::mutex> lock(m_collateralMutex);
    
    if (!exchangeId) {
        return m_balances;
    }
    
    std::vector<ExchangeBalance> filteredBalances;
    for (const auto& balance : m_balances) {
        if (balance.exchangeId == *exchangeId) {
            filteredBalances.push_back(balance);
        }
    }
    
    return filteredBalances;
}

bool CollateralManager::initiateEmergencyFunding() {
    try {
        // Check if emergency funding is needed
        if (!isEmergencyFundingNeeded()) {
            notifyCollateralEvent("emergency_funding_not_needed", "Emergency funding not required at this time");
            return false;
        }
        
        // Find exchanges that need funding
        struct ExchangeFunding {
            ExchangeId exchangeId;
            std::string asset;
            double requiredAmount;
        };
        
        std::vector<ExchangeFunding> fundingNeeded;
        
        // Calculate required funding for each exchange
        for (const auto& exchangeId : m_collateralConfig.managedExchanges) {
            std::vector<std::string> assetsWithLowCollateral;
            
            for (const auto& balance : m_balances) {
                if (balance.exchangeId == exchangeId) {
                    // Check if this asset is below minimum requirements
                    auto it = m_collateralConfig.minimumBalances.find(balance.asset);
                    if (it != m_collateralConfig.minimumBalances.end()) {
                        double minimumRequired = it->second;
                        
                        if (balance.free < minimumRequired) {
                            double fundingAmount = minimumRequired - balance.free;
                            
                            // Add a buffer amount
                            fundingAmount *= (1.0 + m_collateralConfig.emergencyFundingBuffer);
                            
                            fundingNeeded.push_back({
                                exchangeId,
                                balance.asset,
                                fundingAmount
                            });
                            
                            assetsWithLowCollateral.push_back(balance.asset);
                        }
                    }
                }
            }
            
            if (!assetsWithLowCollateral.empty()) {
                std::string assetsStr;
                for (size_t i = 0; i < assetsWithLowCollateral.size(); ++i) {
                    if (i > 0) assetsStr += ", ";
                    assetsStr += assetsWithLowCollateral[i];
                }
                
                notifyCollateralEvent("low_collateral", 
                                    "Exchange " + exchangeId.toString() + " has low collateral for: " + assetsStr);
            }
        }
        
        if (fundingNeeded.empty()) {
            notifyCollateralEvent("funding_calculation", "No specific funding needs identified");
            return false;
        }
        
        // Execute funding operations
        bool overallSuccess = true;
        int successCount = 0;
        
        for (const auto& funding : fundingNeeded) {
            // Default funding source is configured emergency source
            std::string source = m_collateralConfig.emergencyFundingSource;
            
            // Execute the funding
            bool success = executeFundingOperation(source, funding.exchangeId, 
                                                funding.asset, funding.requiredAmount);
            
            if (success) {
                successCount++;
            } else {
                overallSuccess = false;
            }
        }
        
        // Update collateral status after funding operations
        updateCollateralStatus();
        
        notifyCollateralEvent("emergency_funding_result", 
                            "Completed " + std::to_string(successCount) + "/" + 
                            std::to_string(fundingNeeded.size()) + " funding operations");
        
        return overallSuccess;
    } catch (const std::exception& e) {
        std::cerr << "Error initiating emergency funding: " << e.what() << std::endl;
        notifyCollateralEvent("emergency_funding_error", std::string("Exception: ") + e.what());
        return false;
    }
}

bool CollateralManager::hasSufficientCollateral(const ExchangeId& exchangeId, 
                                              const std::string& asset, 
                                              double requiredAmount) const {
    // Find balance for this exchange and asset
    auto balance = getExchangeBalance(exchangeId, asset);
    
    // Check if balance record was found
    if (balance.exchangeId.toString() != exchangeId.toString()) {
        // Balance not found
        return false;
    }
    
    // Check free balance against required amount
    return balance.free >= requiredAmount;
}

bool CollateralManager::updateConfiguration(const CollateralConfig& collateralConfig) {
    try {
        m_collateralConfig = collateralConfig;
        
        // Re-evaluate collateral status with new configuration
        updateCollateralStatus();
        
        notifyCollateralEvent("config_updated", "Collateral configuration updated");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error updating collateral configuration: " << e.what() << std::endl;
        return false;
    }
}

CollateralConfig CollateralManager::getConfiguration() const {
    return m_collateralConfig;
}

void CollateralManager::registerCollateralCallback(
    const std::function<void(const std::string&, const std::string&)>& callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_collateralCallbacks.push_back(callback);
}

std::vector<FundingOperation> CollateralManager::getFundingHistory() const {
    std::lock_guard<std::mutex> lock(m_fundingMutex);
    return m_fundingHistory;
}
double CollateralManager::calculateMarginRequirement(const ExchangeId& exchangeId,
                                                   const TradingPair& tradingPair,
                                                   double size) const {
    try {
        // Get current positions to calculate total exposure
        auto currentPosition = m_positionTracker->getPosition(exchangeId, tradingPair);
        
        // Get market data for price
        double currentPrice = 0.0;
        if (m_dataInterface) {
            auto marketData = m_dataInterface->getMarketData(tradingPair.toString());
            // Check if the market data is valid by checking the price
            if (marketData && marketData->price > 0) {
                currentPrice = marketData->price;
            }
        }
        
        if (currentPrice <= 0) {
            // Use fallback price from position if available
            if (currentPosition) {
                currentPrice = currentPosition->getCurrentPrice();
            }
            if (currentPrice <= 0) {
                std::cerr << "Unable to determine price for " << tradingPair.toString() << std::endl;
                return 0.0;
            }
        }
        
        // Calculate notional value of the position
        double notionalValue = size * currentPrice;
        
        // Get exchange-specific margin requirements
        double marginRate = 1.0; // Default is 100% (no leverage)
        auto it = m_collateralConfig.exchangeMarginRates.find(exchangeId);
        if (it != m_collateralConfig.exchangeMarginRates.end()) {
            auto pairIt = it->second.find(tradingPair.toString());
            if (pairIt != it->second.end()) {
                marginRate = pairIt->second;
            } else {
                // Try to find a default rate for this exchange
                auto defaultIt = it->second.find("default");
                if (defaultIt != it->second.end()) {
                    marginRate = defaultIt->second;
                }
            }
        }
        
        // Calculate margin requirement
        return notionalValue * marginRate;
        
    } catch (const std::exception& e) {
        std::cerr << "Error calculating margin requirement: " << e.what() << std::endl;
        return 0.0;
    }
}
bool CollateralManager::fetchExchangeBalances() {
    try {
        // CRASH PROTECTION: Check for null pointer
        if (!m_executionInterface) {
            std::cerr << "[CollateralManager] ERROR: m_executionInterface is null!" << std::endl;
            return false;
        }
        
        // Clear current balances
        std::vector<ExchangeBalance> newBalances;
        
        // Request balances from each configured exchange
        std::cerr << "[CollateralManager] Total managedExchanges count: " << m_collateralConfig.managedExchanges.size() << std::endl;
        
        // Validate vector integrity before iterating
        if (m_collateralConfig.managedExchanges.size() > 100) {
            std::cerr << "[CollateralManager] ERROR: Suspiciously large exchange count - possible memory corruption!" << std::endl;
            return false;
        }
        
        for (size_t i = 0; i < m_collateralConfig.managedExchanges.size(); ++i) {
            std::cerr << "[CollateralManager] Processing exchange index " << i << std::endl;
            
            // Add bounds checking
            if (i >= m_collateralConfig.managedExchanges.size()) {
                std::cerr << "[CollateralManager] ERROR: Index out of bounds!" << std::endl;
                break;
            }
            
            ExchangeId exchangeId; // Declare here so it's in scope for the rest of the loop
            
            try {
                exchangeId = m_collateralConfig.managedExchanges[i];
                std::cerr << "[CollateralManager] Exchange object address: " << &exchangeId << std::endl;
                
                // Test toString() first (safer than isValid())
                std::string exchangeName;
                try {
                    exchangeName = exchangeId.toString();
                    std::cerr << "[CollateralManager] Exchange toString(): " << exchangeName << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[CollateralManager] Exception in toString(): " << e.what() << std::endl;
                    continue;
                } catch (...) {
                    std::cerr << "[CollateralManager] Unknown error in toString() - skipping" << std::endl;
                    continue;
                }
                
                // Only call isValid() if toString() worked
                try {
                    bool isValid = exchangeId.isValid();
                    std::cerr << "[CollateralManager] Exchange isValid(): " << (isValid ? "true" : "false") << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[CollateralManager] Exception in isValid(): " << e.what() << std::endl;
                    continue;
                } catch (...) {
                    std::cerr << "[CollateralManager] Unknown error in isValid() - skipping" << std::endl;
                    continue;
                }
            } catch (const std::exception& e) {
                std::cerr << "[CollateralManager] Exception accessing exchange " << i << ": " << e.what() << std::endl;
                continue;
            } catch (...) {
                std::cerr << "[CollateralManager] Unknown error accessing exchange " << i << " - skipping" << std::endl;
                continue;
            }
            
            try {
                // First try to use cached balances from the data interface (cheaper, centralized cache)
                std::map<std::string, double> exchangeBalances;
                bool gotBalances = false;

                if (m_dataInterface) {
                    try {
                        auto mdBalances = m_dataInterface->getExchangeBalances(exchangeId);
                        if (!mdBalances.empty()) {
                            exchangeBalances = mdBalances;
                            gotBalances = true;
                            std::cerr << "[CollateralManager] Using cached balances from DataInterface for " << exchangeId.toString() << std::endl;
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[CollateralManager] DataInterface getExchangeBalances threw: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "[CollateralManager] DataInterface getExchangeBalances unknown error" << std::endl;
                    }
                }

                // Fallback to calling ExecutionInterface (may be expensive / blocking) if no cached balances
                if (!gotBalances) {
                    if (!m_executionInterface) {
                        std::cerr << "[CollateralManager] ERROR: m_executionInterface is null when fetching balances" << std::endl;
                        continue;
                    }

                    // Add timeout protection - use async with timeout
                    auto future = std::async(std::launch::async, [this, exchangeId]() {
                        return m_executionInterface->getAccountBalances(exchangeId);
                    });

                    // Wait max 10 seconds for balance request
                    auto status = future.wait_for(std::chrono::seconds(10));
                    if (status == std::future_status::timeout) {
                        std::cerr << "[CollateralManager] Timeout fetching balances - skipping this exchange" << std::endl;
                        continue;
                    }

                    exchangeBalances = future.get();
                }

                if (!exchangeBalances.empty()) {
                    // Convert to our balance format
                    for (const auto& [asset, amount] : exchangeBalances) {
                        ExchangeBalance balance;
                        balance.exchangeId = exchangeId;
                        balance.asset = asset;
                        balance.total = amount;
                        balance.free = amount;  // Assuming all is free for now
                        balance.locked = 0.0;   // No locked amount info from simple API
                        balance.lastUpdated = std::chrono::system_clock::now();

                        newBalances.push_back(balance);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[CollateralManager] Error fetching balances for " << exchangeId.toString()
                          << ": " << e.what() << std::endl;
                // Continue with next exchange
            }
        }
        
        // Update the balances
        {
            std::lock_guard<std::mutex> lock(m_collateralMutex);
            m_balances = newBalances;
        }
        
        return !newBalances.empty();
        
    } catch (const std::exception& e) {
        std::cerr << "Error fetching exchange balances: " << e.what() << std::endl;
        return false;
    }
}

double CollateralManager::calculateTotalCollateral() const {
    std::lock_guard<std::mutex> lock(m_collateralMutex);
    
    double totalCollateral = 0.0;
    
    // Sum up total balance for each asset in base currency
    for (const auto& balance : m_balances) {
        double assetValue = balance.total;
        
        // Convert to base currency if needed
        std::string assetToBaseConversionPair = balance.asset + "/" + m_collateralConfig.baseCurrency;
        std::string baseToAssetConversionPair = m_collateralConfig.baseCurrency + "/" + balance.asset;
        
        // Try to get the conversion rate
        double conversionRate = 1.0;
        
        if (balance.asset != m_collateralConfig.baseCurrency) {
            // Try to get price from data interface
            if (m_dataInterface) {
                // Try direct conversion first
                auto directConversion = m_dataInterface->getMarketData(assetToBaseConversionPair);
                if (directConversion) {
                    conversionRate = directConversion->price;
                } else {
                    // Try inverse conversion
                    auto inverseConversion = m_dataInterface->getMarketData(baseToAssetConversionPair);
                    if (inverseConversion && inverseConversion->price > 0) {
                        conversionRate = 1.0 / inverseConversion->price;
                    } else {
                        // If no direct conversion is available, check configured conversion rates
                        auto it = m_collateralConfig.assetConversionRates.find(balance.asset);
                        if (it != m_collateralConfig.assetConversionRates.end()) {
                            conversionRate = it->second;
                        } else {
                            // Skip this asset if no conversion is available
                            continue;
                        }
                    }
                }
            } else {
                // Check configured conversion rates
                auto it = m_collateralConfig.assetConversionRates.find(balance.asset);
                if (it != m_collateralConfig.assetConversionRates.end()) {
                    conversionRate = it->second;
                } else {
                    // Skip this asset if no conversion is available
                    continue;
                }
            }
        }
        
        // Add to total in base currency
        totalCollateral += assetValue * conversionRate;
    }
    
    return totalCollateral;
}

double CollateralManager::calculateFreeCollateral() const {
    std::lock_guard<std::mutex> lock(m_collateralMutex);
    
    double freeCollateral = 0.0;
    
    // Sum up free balance for each asset in base currency
    for (const auto& balance : m_balances) {
        double assetValue = balance.free;
        
        // Convert to base currency if needed
        std::string assetToBaseConversionPair = balance.asset + "/" + m_collateralConfig.baseCurrency;
        std::string baseToAssetConversionPair = m_collateralConfig.baseCurrency + "/" + balance.asset;
        
        // Try to get the conversion rate
        double conversionRate = 1.0;
        
        if (balance.asset != m_collateralConfig.baseCurrency) {
            // Try to get price from data interface
            if (m_dataInterface) {
                // Try direct conversion first
                auto directConversion = m_dataInterface->getMarketData(assetToBaseConversionPair);
                if (directConversion) {
                    conversionRate = directConversion->price;
                } else {
                    // Try inverse conversion
                    auto inverseConversion = m_dataInterface->getMarketData(baseToAssetConversionPair);
                    if (inverseConversion && inverseConversion->price > 0) {
                        conversionRate = 1.0 / inverseConversion->price;
                    } else {
                        // If no direct conversion is available, check configured conversion rates
                        auto it = m_collateralConfig.assetConversionRates.find(balance.asset);
                        if (it != m_collateralConfig.assetConversionRates.end()) {
                            conversionRate = it->second;
                        } else {
                            // Skip this asset if no conversion is available
                            continue;
                        }
                    }
                }
            } else {
                // Check configured conversion rates
                auto it = m_collateralConfig.assetConversionRates.find(balance.asset);
                if (it != m_collateralConfig.assetConversionRates.end()) {
                    conversionRate = it->second;
                } else {
                    // Skip this asset if no conversion is available
                    continue;
                }
            }
        }
        
        // Add to free collateral in base currency
        freeCollateral += assetValue * conversionRate;
    }
    
    return freeCollateral;
}

double CollateralManager::calculateFreeCollateralRatio() const {
    double totalCollateral = calculateTotalCollateral();
    if (totalCollateral <= 0) {
        return 1.0; // Default to 100% if no collateral
    }
    
    double freeCollateral = calculateFreeCollateral();
    return freeCollateral / totalCollateral;
}

bool CollateralManager::isEmergencyFundingNeeded() const {
    // Check if free collateral ratio is below emergency threshold
    double freeCollateralRatio = calculateFreeCollateralRatio();
    if (freeCollateralRatio < m_collateralConfig.emergencyFundingThreshold) {
        return true;
    }
    
    // Check if any critical asset is below minimum
    for (const auto& [asset, minimumBalance] : m_collateralConfig.criticalAssetMinimums) {
        // Check all exchanges
        bool assetBelowMinimum = true;
        
        for (const auto& exchangeId : m_collateralConfig.managedExchanges) {
            auto balance = getExchangeBalance(exchangeId, asset);
            if (balance.exchangeId.toString() == exchangeId.toString() && balance.free >= minimumBalance) {
                // At least one exchange has sufficient balance
                assetBelowMinimum = false;
                break;
            }
        }
        
        if (assetBelowMinimum) {
            return true;
        }
    }
    
    return false;
}

void CollateralManager::notifyCollateralEvent(const std::string& eventType, const std::string& description) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    for (const auto& callback : m_collateralCallbacks) {
        callback(eventType, description);
    }
}

bool CollateralManager::executeFundingOperation(const std::string& source, const ExchangeId& targetExchangeId, 
                                              const std::string& asset, double amount) {
    try {
        // Create a funding operation record
        FundingOperation operation;
        operation.source = source;
        operation.targetExchangeId = targetExchangeId;
        operation.asset = asset;
        operation.amount = amount;
        operation.status = "pending";
        operation.timestamp = std::chrono::system_clock::now();
        
        // Log the operation
        {
            std::lock_guard<std::mutex> lock(m_fundingMutex);
            m_fundingHistory.push_back(operation);
        }
        
        notifyCollateralEvent("funding_initiated", 
                            "Funding operation initiated: " + std::to_string(amount) + " " + 
                            asset + " from " + source + " to " + targetExchangeId.toString());
        
        // Execute the funding operation
        // In a real system, this would integrate with the funding source APIs
        // For this implementation, we'll simulate the operation
        
        // Simulate success/failure based on some conditions
        bool success = true;
        
        // Generate random transaction ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999);
        std::string transactionId = "tx-" + std::to_string(dis(gen));
        std::string status = "completed";
        
        // Update the operation record
        {
            std::lock_guard<std::mutex> lock(m_fundingMutex);
            for (auto& op : m_fundingHistory) {
                // Find the matching operation by timestamp
                if (op.timestamp == operation.timestamp) {
                    op.status = status;
                    op.transactionId = transactionId;
                    break;
                }
            }
        }
        
        if (success) {
            notifyCollateralEvent("funding_completed", 
                                "Funding operation completed: " + std::to_string(amount) + 
                                " " + asset + " to " + targetExchangeId.toString() + 
                                " (Transaction ID: " + transactionId + ")");
            
            // Update balances
            fetchExchangeBalances();
        } else {
            notifyCollateralEvent("funding_failed", 
                                "Funding operation failed for " + std::to_string(amount) + 
                                " " + asset + " to " + targetExchangeId.toString());
        }
        
        return success;
    } catch (const std::exception& e) {
        std::cerr << "Error executing funding operation: " << e.what() << std::endl;
        
        // Update operation record as failed
        {
            std::lock_guard<std::mutex> lock(m_fundingMutex);
            for (auto& op : m_fundingHistory) {
                // Find the operation by matching key fields
                if (op.source == source && op.targetExchangeId == targetExchangeId && 
                    op.asset == asset && std::abs(op.amount - amount) < 0.000001) {
                    op.status = "failed";
                    break;
                }
            }
        }
        
        notifyCollateralEvent("funding_error", 
                            "Error during funding operation: " + std::string(e.what()));
        
        return false;
    }
}

ExchangeBalance CollateralManager::getExchangeBalance(const ExchangeId& exchangeId, 
                                                     const std::string& asset) const {
    std::lock_guard<std::mutex> lock(m_collateralMutex);
    
    for (const auto& balance : m_balances) {
        if (balance.exchangeId == exchangeId && balance.asset == asset) {
            return balance;
        }
    }
    
    // Return empty balance if not found
    ExchangeBalance emptyBalance;
    emptyBalance.exchangeId = ExchangeId::unknown();
    emptyBalance.asset = asset;
    emptyBalance.total = 0.0;
    emptyBalance.free = 0.0;
    emptyBalance.locked = 0.0;
    emptyBalance.lastUpdated = std::chrono::system_clock::now();
    
    return emptyBalance;
}

} // namespace risk
} // namespace trading