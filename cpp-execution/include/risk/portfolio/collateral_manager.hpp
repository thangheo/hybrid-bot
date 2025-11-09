/**
 * Interface for ensuring sufficient funds for trading activities
 *
 * File: collateral_manager.hpp
 * Created: 2025-04-29
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "position_tracker.hpp"
#include "../interfaces/execution_interface.hpp"
#include "../interfaces/data_interface.hpp"
#include "core/market_types.hpp"
#include "risk/sync/position_synchronizer.hpp"
#include <map>
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>
#include <memory>
#include <functional>
#include <common/exchange_types.hpp>
namespace trading {
namespace risk {
// Use the same ExchangeId type as RiskConfig/CollateralConfig to avoid type mismatch
using ExchangeId = core::ExchangeId;
using namespace core;
/**
 * Structure to represent an exchange balance
 */
struct ExchangeBalance {
    ExchangeId exchangeId;
    std::string asset;
    double total;
    double free;
    double locked;
    Timestamp lastUpdated;
};

/**
 * Structure to represent collateral status
 */
struct CollateralStatus {
    double totalCollateral;
    double freeCollateral;
    double freeCollateralRatio;
    bool belowMinimumThreshold;
    bool emergencyFundingNeeded;
    std::vector<ExchangeBalance> balances;
    Timestamp timestamp;
};

/**
 * Structure to represent a funding operation
 */
struct FundingOperation {
    std::string source;
    ExchangeId targetExchangeId;
    std::string asset;
    double amount;
    std::string status;
    std::string transactionId;
    Timestamp timestamp;
};

/**
 * CollateralManager ensures sufficient funds for trading activities.
 * It monitors margin requirements and prevents over-allocation of capital.
 */
class CollateralManager {
public:
    /**
     * Constructor
     * @param config Risk configuration containing collateral settings
     * @param positionTracker Reference to position tracker
     * @param executionInterface Interface to execution system
     * @param dataInterface Interface to market data
     */
    CollateralManager(const RiskConfig& config, 
                    std::shared_ptr<PositionTracker> positionTracker,
                    std::shared_ptr<ExecutionInterface> executionInterface,
                    std::shared_ptr<DataInterface> dataInterface);
    
    /**
     * Initialize the collateral manager
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    bool shutdown();
    /**
     * Check collateral requirements for a proposed order
     * @param orderRequest The order request
     * @param violations Vector to fill with any violations
     * @return true if sufficient collateral, false otherwise
     */
    bool checkCollateralRequirements(const OrderRequest& orderRequest, std::vector<RiskViolation>& violations);
    
    /**
     * Update collateral status from exchange data
     * @return true if update was successful, false otherwise
     */
    bool updateCollateralStatus();
    
    /**
     * Get the current collateral status
     * @return The current collateral status
     */
    CollateralStatus getCollateralStatus() const;
    
    /**
     * Get exchange balances
     * @param exchangeId Optional exchange ID to filter balances
     * @return Vector of exchange balances
     */
    std::vector<ExchangeBalance> getExchangeBalances(const std::optional<ExchangeId>& exchangeId = std::nullopt) const;
    
    /**
    // Optional: inject PositionSynchronizer for fast cached position reads
    void setPositionSynchronizer(std::shared_ptr<trading::risk::sync::PositionSynchronizer> positionSync);
     * Initiate emergency funding when free collateral is below threshold
     * @return true if funding was initiated successfully, false otherwise
     */
    bool initiateEmergencyFunding();
    
    /**
     * Check if an order would exceed available collateral
     * @param exchangeId The exchange identifier
     * @param asset The asset to check
     * @param requiredAmount The required amount
     * @return true if sufficient collateral, false otherwise
     */
    bool hasSufficientCollateral(const ExchangeId& exchangeId, const std::string& asset, double requiredAmount) const;
    
    /**
     * Update collateral configuration
     * @param collateralConfig New collateral configuration
     * @return true if update was successful, false otherwise
     */
    bool updateConfiguration(const CollateralConfig& collateralConfig);
    
    /**
     * Get the current collateral configuration
     * @return The current configuration
     */
    CollateralConfig getConfiguration() const;
    
    /**
     * Register a callback for collateral events
     * @param callback The function to call when collateral events occur
     */
    void registerCollateralCallback(const std::function<void(const std::string&, const std::string&)>& callback);
    
    // Optional: inject PositionSynchronizer for fast cached position reads
    void setPositionSynchronizer(std::shared_ptr<trading::risk::sync::PositionSynchronizer> positionSync);
    /**
     * Get funding history
     * @return Vector of funding operations
     */
    std::vector<FundingOperation> getFundingHistory() const;
    
    /**
     * Calculate margin requirements for a position
     * @param exchangeId The exchange identifier
     * @param tradingPair The trading pair
     * @param size The position size
     * @return The required margin
     */
    double calculateMarginRequirement(const ExchangeId& exchangeId, const TradingPair& tradingPair, double size) const;

private:
    std::shared_ptr<PositionTracker> m_positionTracker;
    std::shared_ptr<ExecutionInterface> m_executionInterface;
    std::shared_ptr<execution::ExecutionService> m_executionService; // ✅ Direct access
    std::shared_ptr<DataInterface> m_dataInterface;
    RiskConfig m_config;
    CollateralConfig m_collateralConfig;
    
    // Collateral state
    mutable std::mutex m_collateralMutex;
    CollateralStatus m_collateralStatus;
    std::vector<ExchangeBalance> m_balances;
    
    // Funding history
    mutable std::mutex m_fundingMutex;
    std::vector<FundingOperation> m_fundingHistory;
    
    // Callbacks
    mutable std::mutex m_callbackMutex;
    std::vector<std::function<void(const std::string&, const std::string&)>> m_collateralCallbacks;
    
    // Helper methods
    bool fetchExchangeBalances();
    double calculateTotalCollateral() const;
    double calculateFreeCollateral() const;
    double calculateFreeCollateralRatio() const;
    bool isEmergencyFundingNeeded() const;
    void notifyCollateralEvent(const std::string& eventType, const std::string& description);
    bool executeFundingOperation(const std::string& source, const ExchangeId& targetExchangeId, 
                               const std::string& asset, double amount);
    ExchangeBalance getExchangeBalance(const ExchangeId& exchangeId, const std::string& asset) const;
    // Optional fast position cache
    std::shared_ptr<trading::risk::sync::PositionSynchronizer> m_positionSync;
};

} // namespace risk
} // namespace trading