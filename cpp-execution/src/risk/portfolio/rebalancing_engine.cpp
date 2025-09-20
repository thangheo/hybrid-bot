/**
 * Implementation of the inventory management across venues
 *
 * File: rebalancing_engine.cpp
 * Created: 2025-04-29
 */

 #include "portfolio/rebalancing_engine.hpp"
 #include <iostream>
 #include <algorithm>
 #include <numeric>
 #include <thread>
 #include <chrono>
 #include <future>
 
 namespace trading {
 namespace risk {

 RebalancingEngine::RebalancingEngine(const RiskConfig& config, 
                                   std::shared_ptr<PositionTracker> positionTracker,
                                   std::shared_ptr<ExecutionInterface> executionInterface,
                                   std::shared_ptr<DataInterface> dataInterface)
    : m_positionTracker(positionTracker)
    , m_executionInterface(executionInterface)
    , m_dataInterface(dataInterface)
    , m_config(config)
    , m_rebalancingConfig(config.getRebalancingConfig())
    , m_rebalancingScheduled(false)
    , m_autoRebalancingEnabled(config.getRebalancingConfig().autoRebalancingEnabled)
{
    // Initialize last rebalancing result
    m_lastRebalancing.success = false;
    m_lastRebalancing.timestamp = std::chrono::system_clock::now();
    
    // Initialize next scheduled rebalancing
    m_nextScheduledRebalancing = std::chrono::system_clock::now() + 
                                std::chrono::minutes(m_rebalancingConfig.rebalancingIntervalMinutes);
}

 bool RebalancingEngine::initialize() {
     try {
         // Load target allocations from config
         {
             std::lock_guard<std::mutex> lock(m_allocationMutex);
             m_targetAllocations = m_rebalancingConfig.targetAllocations;
         }
         
         // Register for position updates to monitor allocation drift
         m_positionTracker->registerPositionUpdateCallback(
             [this](const Position& position, const std::string& updateType) {
                 // Check if rebalancing is needed when positions change significantly
                 if (updateType == "new" || updateType == "modified" || updateType == "reconciliation") {
                     if (isRebalancingNeeded()) {
                         notifyRebalancingEvent({
                             true,
                             {},
                             {"Rebalancing needed due to position change"},
                             std::chrono::system_clock::now() 
                         });
                     }
                 }
             }
         );
         
         // Schedule rebalancing if auto-rebalancing is enabled
         if (m_autoRebalancingEnabled) {
             scheduleRebalancingChecks();
         }
         
         return true;
     } catch (const std::exception& e) {
         std::cerr << "Error initializing RebalancingEngine: " << e.what() << std::endl;
         return false;
     }
 }
 
 bool RebalancingEngine::isRebalancingNeeded() const {
     try {
         std::lock_guard<std::mutex> lock(m_allocationMutex);
         
         // Get current positions
         auto positions = m_positionTracker->getAllPositions();
         
         // Calculate current allocations by exchange and trading pair
         std::unordered_map<std::string, std::unordered_map<std::string, double>> currentAllocations;
         
         // Calculate total portfolio value first
         double totalPortfolioValue = 0.0;
         for (const auto& position : positions) {
             totalPortfolioValue += position.getNotionalValue();
         }
         
         if (totalPortfolioValue <= 0) {
             return false; // No positions to rebalance
         }
         
         // Calculate allocations
         for (const auto& position : positions) {
             const auto& exchangeId = position.getExchangeId();
             const auto& tradingPair = position.getTradingPair();
             
             // Calculate allocation percentage
             double allocationPct = position.getNotionalValue() / totalPortfolioValue;
             
             // Store allocation
             currentAllocations[exchangeId.toString()][tradingPair.toString()] = allocationPct;
             
             // Check allocation against target
             std::string allocationKey = exchangeId.toString() + ":" + tradingPair.toString();
             auto targetIt = m_targetAllocations.find(allocationKey);
             
             if (targetIt != m_targetAllocations.end()) {
                 double targetAllocation = targetIt->second;
                 
                 // Check if allocation deviation exceeds threshold
                 if (shouldRebalance(allocationPct, targetAllocation)) {
                     return true;
                 }
             }
         }
         
         // Check if any target allocations are missing completely
         for (const auto& [key, targetAlloc] : m_targetAllocations) {
             // Parse the key to get exchange and trading pair
             size_t colonPos = key.find(':');
             if (colonPos == std::string::npos) continue;
             
            // std::string exchangeIdStr = key.substr(0, colonPos);
            std::string exchangeIdStr = key.substr(0, colonPos);
            core::ExchangeId exchangeId = core::ExchangeId::fromString(exchangeIdStr);
             core::Symbol tradingPair = key.substr(colonPos + 1);
             
             // Check if this allocation exists in current allocations
            auto exchangeIt = currentAllocations.find(exchangeIdStr);
            if (exchangeIt == currentAllocations.end() || 
                exchangeIt->second.find(tradingPair.toString()) == exchangeIt->second.end()) {
                
                // Target allocation is missing completely
                if (targetAlloc > m_rebalancingConfig.allocationDeviationThreshold) {
                    return true;
                }
            }
        }
        
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error checking if rebalancing is needed: " << e.what() << std::endl;
        return false;
    }
}

std::vector<RebalancingOrder> RebalancingEngine::calculateRebalancingNeeds() const {
    std::vector<RebalancingOrder> rebalancingOrders;
    
    try {
        std::lock_guard<std::mutex> lock(m_allocationMutex);
        
        // Get current positions
        auto positions = m_positionTracker->getAllPositions();
        
        // Calculate total portfolio value
        double totalPortfolioValue = 0.0;
        for (const auto& position : positions) {
            totalPortfolioValue += position.getNotionalValue();
        }
        
        if (totalPortfolioValue <= 0) {
            return rebalancingOrders; // No positions to rebalance
        }
        
        // Get current allocations by exchange and trading pair
        std::unordered_map<std::string, std::unordered_map<std::string, double>> notionalValues;
        std::unordered_map<std::string, std::unordered_map<std::string, double>> currentAllocations;
        
        // Track asset positions across exchanges
        std::unordered_map<std::string, std::unordered_map<std::string, double>> exchangeAssetPositions;
        
        for (const auto& position : positions) {
            const auto& exchangeId = position.getExchangeId();
            const auto& tradingPair = position.getTradingPair();
            
            // Extract base asset from trading pair (e.g., "BTC" from "BTC/USD")
            std::string baseAsset = tradingPair.base;
            
            // Calculate notional value
            double notionalValue = position.getNotionalValue();
            
            // Store notional value and allocation
            notionalValues[exchangeId.toString()][tradingPair.toString()] = notionalValue;
            currentAllocations[exchangeId.toString()][tradingPair.toString()] = notionalValue / totalPortfolioValue;
            
            // Track asset positions by exchange
            exchangeAssetPositions[exchangeId.toString()][baseAsset] = position.getQuantity();
        }
        
        // Calculate target notional values based on target allocations
        std::unordered_map<std::string, std::unordered_map<std::string, double>> targetNotional;
        std::unordered_map<std::string, std::unordered_map<std::string, double>> allocationDelta;
        
        for (const auto& [key, targetAlloc] : m_targetAllocations) {
            // Parse the key to get exchange and trading pair
            size_t colonPos = key.find(':');
            if (colonPos == std::string::npos) continue;
            
            std::string exchangeIdStr = key.substr(0, colonPos);
            core::ExchangeId exchangeId = core::ExchangeId::fromString(exchangeIdStr);
            core::Symbol tradingPair = key.substr(colonPos + 1);
            
            // Calculate target notional value
            double targetNotionalValue = totalPortfolioValue * targetAlloc;
            targetNotional[exchangeId.toString()][tradingPair.toString()] = targetNotionalValue;

            // Get current notional value (or 0 if not present)
            double currentNotional = 0.0;
            if (notionalValues.find(exchangeId.toString()) != notionalValues.end() && 
                notionalValues[exchangeId.toString()].find(tradingPair.toString()) != notionalValues[exchangeId.toString()].end()) {
                currentNotional = notionalValues[exchangeId.toString()][tradingPair.toString()];
            }
            
            // Calculate delta
            double delta = targetNotionalValue - currentNotional;
            allocationDelta[exchangeId.toString()][tradingPair.toString()] = delta;
        }
        
        // Generate rebalancing orders
        // First, identify significant deltas
        std::vector<std::tuple<std::string, std::string, double>> significantDeltas;
        
        for (const auto& [exchangeId, tradingPairs] : allocationDelta) {
            for (const auto& [tradingPair, delta] : tradingPairs) {
                // Check if delta is significant relative to total portfolio
                if (std::abs(delta) > totalPortfolioValue * m_rebalancingConfig.allocationDeviationThreshold) {
                    significantDeltas.emplace_back(exchangeId, tradingPair, delta);
                }
            }
        }
        
        // Sort deltas by absolute magnitude (largest first)
        std::sort(significantDeltas.begin(), significantDeltas.end(), 
                 [](const auto& a, const auto& b) {
                     return std::abs(std::get<2>(a)) > std::abs(std::get<2>(b));
                 });
        
        // Generate orders for significant deltas
        for (const auto& [exchangeId, tradingPair, delta] : significantDeltas) {
            if (std::abs(delta) < m_rebalancingConfig.minimumOrderSize) {
                continue; // Skip if below minimum order size
            }
            
            // Get market price
            double price = getAssetPrice(TradingPair{tradingPair});
            if (price <= 0) {
                continue; // Skip if price is not available
            }
            
            // Calculate quantity to trade
            double quantity = std::abs(delta) / price;
            
            // Extract base asset from trading pair
            std::string baseAsset = tradingPair.substr(0, tradingPair.find('/'));
            
            // Determine direction
            Direction direction = (delta > 0) ? Direction::BUY : Direction::SELL;
            
            if (direction == Direction::BUY) {
                // We need to buy on this exchange
                // Find another exchange where we can sell the same asset
                std::vector<ExchangeId> otherExchanges = getPreferredExchanges();
                
                // Remove current exchange from list
                otherExchanges.erase(
                    std::remove(otherExchanges.begin(), otherExchanges.end(), core::ExchangeId::fromString(exchangeId)),
                    otherExchanges.end()
                );
                
                // Find exchange with sufficient balance of this asset
                for (const auto& sourceExchangeId : otherExchanges) {
                    if (exchangeAssetPositions.find(sourceExchangeId.toString()) != exchangeAssetPositions.end() && 
                        exchangeAssetPositions[sourceExchangeId.toString()].find(baseAsset) != exchangeAssetPositions[sourceExchangeId.toString()].end()) {
                        
                        double availableQuantity = exchangeAssetPositions[sourceExchangeId.toString()][baseAsset];
                        
                        if (availableQuantity >= quantity) {
                            // Create a rebalancing order to move from source to target
                            RebalancingOrder order;
                            order.sourceExchangeId = sourceExchangeId;
                            order.targetExchangeId = core::ExchangeId::fromString(exchangeId);
                            order.tradingPair = TradingPair{tradingPair};
                            order.quantity = quantity;
                            order.direction = direction;
                            order.estimatedPrice = price;
                            order.reason = "Rebalancing to target allocation";
                            
                            rebalancingOrders.push_back(order);
                            
                            // Update tracking of available quantity
                            exchangeAssetPositions[sourceExchangeId.toString()][baseAsset] -= quantity;
                            
                            // Move to next delta
                            break;
                        }
                    }
                }
            } else {
                // We need to sell on this exchange
                // Find another exchange where we want to buy the same asset
                for (const auto& [otherExchangeId, tradingPairs] : allocationDelta) {
                    if (otherExchangeId != exchangeId) {
                        for (const auto& [otherTradingPair, otherDelta] : tradingPairs) {
                            // Check if this is the same asset and needs buying
                            if (otherTradingPair == tradingPair && otherDelta > 0) {
                                // Create a rebalancing order
                                RebalancingOrder order;
                                order.sourceExchangeId = core::ExchangeId::fromString(exchangeId);
                                order.targetExchangeId = core::ExchangeId::fromString(otherExchangeId);
                                order.tradingPair = TradingPair{tradingPair};
                                order.quantity = quantity;
                                order.direction = direction;
                                order.estimatedPrice = price;
                                order.reason = "Rebalancing to target allocation";
                                
                                rebalancingOrders.push_back(order);
                                
                                // Update tracking of available quantity
                                exchangeAssetPositions[exchangeId][baseAsset] -= quantity;
                                
                                // Move to next delta
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        return rebalancingOrders;
    } catch (const std::exception& e) {
        std::cerr << "Error calculating rebalancing needs: " << e.what() << std::endl;
        return rebalancingOrders;
    }
}

RebalancingResult RebalancingEngine::executeRebalancing(
    const std::optional<std::vector<RebalancingOrder>>& orders) {
    
    RebalancingResult result;
    result.success = true;
    result.timestamp = std::chrono::system_clock::now();
    
    try {
        // Get rebalancing orders if not provided
        std::vector<RebalancingOrder> rebalancingOrders;
        if (orders) {
            rebalancingOrders = *orders;
        } else {
            rebalancingOrders = calculateRebalancingNeeds();
        }
        
        if (rebalancingOrders.empty()) {
            result.success = true;
            result.errors.push_back("No rebalancing orders needed");
            
            // Update last rebalancing result
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_lastRebalancing = result;
            }
            
            return result;
        }
        
        // Execute each rebalancing order
        std::vector<std::future<bool>> orderFutures;
        for (const auto& order : rebalancingOrders) {
            // Execute orders in parallel if configured to do so
            if (m_rebalancingConfig.parallelExecution) {
                orderFutures.push_back(std::async(std::launch::async,
                    &RebalancingEngine::executeRebalancingOrder, this, order));
            } else {
                bool success = executeRebalancingOrder(order);
                if (success) {
                    result.ordersCreated.push_back(order);
                } else {
                    result.success = false;
                    result.errors.push_back("Failed to execute order for " + 
                                          order.tradingPair.toString() + " from " + 
                                          order.sourceExchangeId.toString() + " to " + 
                                          order.targetExchangeId.toString());
                }
            }
        }
        
        // Wait for parallel executions to complete
        if (m_rebalancingConfig.parallelExecution) {
            for (size_t i = 0; i < orderFutures.size(); ++i) {
                bool success = orderFutures[i].get();
                if (success) {
                    result.ordersCreated.push_back(rebalancingOrders[i]);
                } else {
                    result.success = false;
                    result.errors.push_back("Failed to execute order for " + 
                                          rebalancingOrders[i].tradingPair.toString() + " from " + 
                                          rebalancingOrders[i].sourceExchangeId.toString() + " to " + 
                                          rebalancingOrders[i].targetExchangeId.toString());
                }
            }
        }
        
        // Update last rebalancing result
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_lastRebalancing = result;
        }
        
        // Notify about rebalancing event
        notifyRebalancingEvent(result);
        
        return result;
    } catch (const std::exception& e) {
        std::cerr << "Error executing rebalancing: " << e.what() << std::endl;
        
        result.success = false;
        result.errors.push_back(std::string("Exception: ") + e.what());
        
        // Update last rebalancing result
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_lastRebalancing = result;
        }
        
        // Notify about rebalancing event
        notifyRebalancingEvent(result);
        
        return result;
    }
}
bool RebalancingEngine::scheduleRebalancingChecks() {
    try {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_rebalancingScheduled) {
            return true;
        }
        
        m_stopRequested = false;
        m_rebalancingScheduled = true;
        
        m_rebalancingThread = std::thread([this]() {
            while (true) {
                std::unique_lock<std::mutex> lock(m_stateMutex);
                
                if (m_stopRequested || !m_autoRebalancingEnabled) {
                    m_rebalancingScheduled = false;
                    break;
                }
                
                auto nextScheduled = m_nextScheduledRebalancing;
                
                m_cv.wait_until(lock, nextScheduled, [this] {
                    return m_stopRequested || !m_autoRebalancingEnabled;
                });
                
                if (m_stopRequested || !m_autoRebalancingEnabled) {
                    m_rebalancingScheduled = false;
                    break;
                }
                
                lock.unlock();
                
                if (isRebalancingNeeded()) {
                    if (m_rebalancingConfig.autoExecuteRebalancing) {
                        executeRebalancing();
                    } else {
                        RebalancingResult notification;
                        notification.success = true;
                        notification.errors.push_back("Rebalancing needed but not auto-executed");
                        notification.timestamp = std::chrono::system_clock::now();
                        notifyRebalancingEvent(notification);
                    }
                }
                
                lock.lock();
                m_nextScheduledRebalancing = std::chrono::system_clock::now() +
                    std::chrono::minutes(m_rebalancingConfig.rebalancingIntervalMinutes);
            }
        });
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error scheduling rebalancing checks: " << e.what() << std::endl;
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_rebalancingScheduled = false;
        return false;
    }
}

void RebalancingEngine::stopRebalancing() {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_stopRequested = true;
        m_autoRebalancingEnabled = false;
    }
    m_cv.notify_all();
    
    if (m_rebalancingThread.joinable()) {
        m_rebalancingThread.join();
    }
}

// bool RebalancingEngine::scheduleRebalancingChecks() {
//     try {
//         // Check if already scheduled
//         {
//             std::lock_guard<std::mutex> lock(m_stateMutex);
//             if (m_rebalancingScheduled) {
//                 return true; // Already scheduled
//             }
//             m_rebalancingScheduled = true;
//         }
        
//         // Launch a detached thread for scheduling
//         std::thread([this]() {
//             while (true) {
//                 // Check if auto-rebalancing is still enabled
//                 bool autoRebalancingEnabled;
//                 {
//                     std::lock_guard<std::mutex> lock(m_stateMutex);
//                     autoRebalancingEnabled = m_autoRebalancingEnabled;
//                     if (!autoRebalancingEnabled) {
//                         m_rebalancingScheduled = false;
//                         break; // Exit the loop if disabled
//                     }
//                 }
                
//                 // Sleep until next scheduled rebalancing (do not hold m_stateMutex while sleeping)
//                 std::chrono::system_clock::time_point nextScheduled;
//                 {
//                     std::lock_guard<std::mutex> lock(m_stateMutex);
//                     nextScheduled = m_nextScheduledRebalancing;
//                 }
//                 std::this_thread::sleep_until(nextScheduled);
                
//                 // Check if auto-rebalancing is still enabled after sleep
//                 {
//                     std::lock_guard<std::mutex> lock(m_stateMutex);
//                     if (!m_autoRebalancingEnabled) {
//                         m_rebalancingScheduled = false;
//                         break; // Exit the loop if disabled
//                     }
//                 }
                
//                 // Check if rebalancing is needed
//                 if (isRebalancingNeeded()) {
//                     // Auto-execute rebalancing if configured to do so
//                     if (m_rebalancingConfig.autoExecuteRebalancing) {
//                         executeRebalancing();
//                     } else {
//                         // Just notify that rebalancing is needed
//                         RebalancingResult notification;
//                         notification.success = true;
//                         notification.errors.push_back("Rebalancing needed but not auto-executed");
//                         notification.timestamp = std::chrono::system_clock::now();
//                         notifyRebalancingEvent(notification);
//                     }
//                 }
                
//                 // Schedule next rebalancing
//                 {
//                     std::lock_guard<std::mutex> lock(m_stateMutex);
//                     m_nextScheduledRebalancing = std::chrono::system_clock::now() + 
//                         std::chrono::minutes(m_rebalancingConfig.rebalancingIntervalMinutes);
//                 }
//             }
//         }).detach();
        
//         return true;
//     } catch (const std::exception& e) {
//         std::cerr << "Error scheduling rebalancing checks: " << e.what() << std::endl;
        
//         // Reset scheduled flag
//         {
//             std::lock_guard<std::mutex> lock(m_stateMutex);
//             m_rebalancingScheduled = false;
//         }
        
//         return false;
//     }
// }

bool RebalancingEngine::cancelScheduledRebalancingChecks() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_autoRebalancingEnabled = false;
    // The thread will exit on its own when it checks this flag
    return true;
}

bool RebalancingEngine::updateConfiguration(const RebalancingConfig& rebalancingConfig) {
    try {
        // Update configuration
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_rebalancingConfig = rebalancingConfig;
            
            // Update auto-rebalancing flag
            bool previouslyEnabled = m_autoRebalancingEnabled;
            m_autoRebalancingEnabled = rebalancingConfig.autoRebalancingEnabled;
            
            // Update target allocations
            {
                std::lock_guard<std::mutex> allocLock(m_allocationMutex);
                m_targetAllocations = rebalancingConfig.targetAllocations;
            }
            
            // Check if we need to schedule or cancel rebalancing
            if (!previouslyEnabled && m_autoRebalancingEnabled) {
                // Schedule rebalancing if newly enabled
                m_nextScheduledRebalancing = std::chrono::system_clock::now() + 
                    std::chrono::minutes(m_rebalancingConfig.rebalancingIntervalMinutes);
            }
        }
        
        // Schedule or cancel rebalancing based on new configuration
        if (m_rebalancingConfig.autoRebalancingEnabled) {
            scheduleRebalancingChecks();
        } else {
            cancelScheduledRebalancingChecks();
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error updating rebalancing configuration: " << e.what() << std::endl;
        return false;
    }
}

RebalancingConfig RebalancingEngine::getConfiguration() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_rebalancingConfig;
}

void RebalancingEngine::registerRebalancingCallback(
    const std::function<void(const RebalancingResult&)>& callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_rebalancingCallbacks.push_back(callback);
}

RebalancingResult RebalancingEngine::getLastRebalancingResult() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_lastRebalancing;
}

bool RebalancingEngine::isAutoRebalancingEnabled() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_autoRebalancingEnabled;
}

bool RebalancingEngine::setAutoRebalancing(bool enabled) {
    try {
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_autoRebalancingEnabled = enabled;
            m_rebalancingConfig.autoRebalancingEnabled = enabled;
        }
        
        // Schedule or cancel rebalancing
        if (enabled) {
            return scheduleRebalancingChecks();
        } else {
            return cancelScheduledRebalancingChecks();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error setting auto-rebalancing: " << e.what() << std::endl;
        return false;
    }
}

double RebalancingEngine::calculateExchangeAllocation(const ExchangeId& exchangeId, 
                                                    const TradingPair& tradingPair) const {
    try {
        // Get current positions
        auto positions = m_positionTracker->getAllPositions();
        
        // Calculate total portfolio value
        double totalPortfolioValue = 0.0;
        double exchangePairValue = 0.0;
        
        for (const auto& position : positions) {
            double notionalValue = position.getNotionalValue();
            totalPortfolioValue += notionalValue;
            
            if (position.getExchangeId() == exchangeId && position.getTradingPair() == tradingPair) {
                exchangePairValue = notionalValue;
            }
        }
        
        if (totalPortfolioValue <= 0) {
            return 0.0;
        }
        
        return exchangePairValue / totalPortfolioValue;
    } catch (const std::exception& e) {
        std::cerr << "Error calculating exchange allocation: " << e.what() << std::endl;
        return 0.0;
    }
}

std::vector<ExchangeId> RebalancingEngine::getPreferredExchanges() const {
    // Use configured preferred exchanges if available
    if (!m_rebalancingConfig.preferredExchanges.empty()) {
        std::vector<ExchangeId> convertedExchanges;
        for (const auto& exchangeStr : m_rebalancingConfig.preferredExchanges) {
            convertedExchanges.push_back(core::ExchangeId::fromString(exchangeStr));
        }
        return convertedExchanges;
    }
    
    // Otherwise, get all exchanges with positions
    std::vector<ExchangeId> exchanges;
    auto positions = m_positionTracker->getAllPositions();
    
    for (const auto& position : positions) {
        const auto& exchangeId = position.getExchangeId();
        
        // Add if not already in the list
        if (std::find(exchanges.begin(), exchanges.end(), exchangeId) == exchanges.end()) {
            exchanges.push_back(exchangeId);
        }
    }
    
    return exchanges;
}

bool RebalancingEngine::executeRebalancingOrder(const RebalancingOrder& order) {
    try {
        // Log the rebalancing operation
        std::cout << "Executing rebalancing order: " << order.quantity << " " 
                  << order.tradingPair.toString() << " from " 
                  << order.sourceExchangeId.toString() << " to " 
                  << order.targetExchangeId.toString() << std::endl;
        
        // In a real implementation, this would execute actual exchange transfers
        // and trades. For this implementation, we'll simulate success.
        
        // Calculate transfer fees
        double transferFee = calculateTransferFees(
            order.sourceExchangeId, order.targetExchangeId, order.tradingPair, order.quantity);
        
        // Create withdrawal order on source exchange
        if (m_executionInterface) {
            // Create and submit orders on both exchanges
            
            // 1. Sell/withdraw on source exchange
            OrderRequest sourceOrder;
            sourceOrder.exchangeId = order.sourceExchangeId;
            sourceOrder.tradingPair = order.tradingPair.toString(); // Use symbol for trading pair
            sourceOrder.quantity = order.quantity;
            sourceOrder.price = order.estimatedPrice;
            sourceOrder.side = (order.direction == Direction::BUY) ? Direction::SELL : Direction::BUY;
            sourceOrder.strategyId = "rebalancing";
            
            // 2. Buy/deposit on target exchange
            OrderRequest targetOrder;
            targetOrder.exchangeId = order.targetExchangeId;
            targetOrder.tradingPair = order.tradingPair.toString(); // Use symbol for trading pair
            targetOrder.quantity = order.quantity - transferFee; // Adjust for transfer fees
            targetOrder.price = order.estimatedPrice;
            targetOrder.side = (order.direction == Direction::BUY) ? Direction::BUY : Direction::SELL;
            targetOrder.strategyId = "rebalancing";
            
            // Submit orders
            auto sourceResponse = m_executionInterface->submitOrder(sourceOrder);
            bool sourceSuccess = (sourceResponse == "success" || sourceResponse == "SUCCESS");
            
            if (!sourceSuccess) {
                std::cerr << "Failed to execute source exchange order: " << sourceResponse << std::endl;
                return false;
            }
            
            // Short delay to ensure sequencing
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            auto targetResponse = m_executionInterface->submitOrder(targetOrder);
            bool targetSuccess = (targetResponse == "success" || targetResponse == "SUCCESS");
            
            if (!targetSuccess) {
                std::cerr << "Failed to execute target exchange order: " << targetResponse << std::endl;
                return false;
            }
            
            return sourceSuccess && targetSuccess;
        }
        
        // If no execution interface, simulate success
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error executing rebalancing order: " << e.what() << std::endl;
        return false;
    }
}

void RebalancingEngine::notifyRebalancingEvent(const RebalancingResult& result) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    for (const auto& callback : m_rebalancingCallbacks) {
        callback(result);
    }
}

double RebalancingEngine::getAssetPrice(const TradingPair& tradingPair) const {
    try {
        // Try to get from data interface first
        if (m_dataInterface) {
            auto marketData = m_dataInterface->getMarketData(tradingPair.toString());
            if (marketData) {
                return marketData->price;
            }
        }
        
        // Fallback to position data if available
        auto positions = m_positionTracker->getPositionsByTradingPair(tradingPair);
        if (!positions.empty()) {
            return positions[0].getCurrentPrice();
        }
        
        // If still not available, check for configured fixed prices
        auto it = m_rebalancingConfig.fixedPrices.find(tradingPair.toString());
        if (it != m_rebalancingConfig.fixedPrices.end()) {
            return it->second;
        }
        
        return 0.0; // Price not available
    } catch (const std::exception& e) {
        std::cerr << "Error getting asset price: " << e.what() << std::endl;
        return 0.0;
    }
}

double RebalancingEngine::calculateTransferFees(const ExchangeId& sourceExchangeId, 
                                              const ExchangeId& targetExchangeId, 
                                              const TradingPair& tradingPair, 
                                              double quantity) const {
    try {
        // Extract the base asset from the trading pair
        std::string baseAsset = tradingPair.base;
        
        // Check for configured transfer fees
        double feeRate = m_rebalancingConfig.defaultTransferFeeRate;
        
        // Check for asset-specific fee
        auto assetIt = m_rebalancingConfig.assetTransferFees.find(baseAsset);
        if (assetIt != m_rebalancingConfig.assetTransferFees.end()) {
            feeRate = assetIt->second;
        }
        
        // Check for exchange-specific fee
        std::string exchangePair = sourceExchangeId.toString() + ":" + targetExchangeId.toString();
        auto exchangeIt = m_rebalancingConfig.exchangeTransferFees.find(exchangePair);
        if (exchangeIt != m_rebalancingConfig.exchangeTransferFees.end()) {
            feeRate = exchangeIt->second;
        }
        
        // Calculate fee amount
        return quantity * feeRate;
    } catch (const std::exception& e) {
        std::cerr << "Error calculating transfer fees: " << e.what() << std::endl;
        return 0.0;
    }
}

bool RebalancingEngine::shouldRebalance(double currentAllocation, double targetAllocation) const {
    double deviation = std::abs(currentAllocation - targetAllocation);
    
    // Check if deviation exceeds threshold
    if (deviation > m_rebalancingConfig.allocationDeviationThreshold) {
        // Also check if it exceeds minimum rebalancing amount
        double portfolioValue = m_positionTracker->calculatePortfolioValue();
        double deviationValue = deviation * portfolioValue;
        
        return deviationValue >= m_rebalancingConfig.minimumRebalancingValue;
    }
    
    return false;
}

} // namespace risk
 }