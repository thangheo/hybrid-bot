/**
 * @file strategy/types/strategy_performance.hpp
 * @brief Strategy performance tracking structures
 * 
 * This file defines structures for tracking strategy performance.
 * 
 * @author Trading System Team
 * @date May 13, 2025
 */

 #pragma once

 #include <chrono>
 #include <string>
//  #include "core/types.hpp"
    #include "core/market_types.hpp"
    #include "core/direction.hpp"
    #include "core/confidence.hpp"

 #include "execution/models/order.hpp"
 namespace trading {
 namespace strategy {

 /**
  * StrategyState enum for tracking the lifecycle of the strategy engine
  */
 enum class StrategyState {
     CREATED,       // Initially created
     INITIALIZED,   // Initialized but not running
     RUNNING,       // Currently running
     PAUSED,        // Temporarily paused
     STOPPED,       // Stopped but can be started again
     ERROR          // Error state
 };
 
 /**
  * Execution status of a trading signal
  */
 enum class SignalExecutionStatus {
     PENDING,      // Waiting to be executed
     EXECUTING,    // Currently executing
     COMPLETED,    // Successfully completed
     FAILED,       // Failed to execute
     EXPIRED,      // Expired before execution
     CANCELLED     // Cancelled before completion
 };
 
 /**
  * Result of executing a trading signal
  */
 struct SignalExecutionResult {
     std::string signal_id;                             // ID of the signal
     SignalExecutionStatus status;                      // Status of execution
    core::Direction direction;                         // Direction of the signal
        double signalStrength;                             // Strength of the signal
        core::ConfidenceLevel confidenceLevel;             // Confidence level of the signal
        core::ExchangeId exchangeId;                       // Exchange identifier
        core::Symbol symbol;                               // Trading pair symbol
        trading::execution::models::OrderType orderType;                         // Type of order (e.g., market, limit)
        double orderPrice;                                 // Price at which order was executed
        double orderQuantity;                              // Quantity of asset to be traded
    //  marketState
    core::MarketState marketState;                     // Market state at execution
     double pnl;                                        // Profit and loss
     std::chrono::system_clock::time_point startTime;   // When execution started
     std::chrono::system_clock::time_point completionTime; // When execution completed
     std::string executionDetails;                      // Additional details
     std::string errorMessage;                          // Error message if failed
     
     // Default constructor
     SignalExecutionResult()
         : signal_id(""), 
           status(SignalExecutionStatus::PENDING), 
           pnl(0.0),
           startTime(std::chrono::system_clock::now()),
           completionTime(std::chrono::system_clock::now()),
           executionDetails(""),
           errorMessage("") {}
 };
 
 /**
  * State of a trading pair within the strategy
  */
 struct TradingPairState {
     enum State {
         INACTIVE,   // Not currently trading
         ACTIVE,     // Actively trading
         SUSPENDED   // Temporarily suspended
     };
     
     std::string symbol;                                // Trading pair symbol
     core::ExchangeId exchangeId;                       // Exchange identifier
     State state;                                       // Current state
     std::chrono::system_clock::time_point lastUpdateTime; // Last update time
     double lastPrice;                                  // Last observed price
     double previousPrice;                              // Previous price for comparison
     
     // Default constructor
     TradingPairState()
         : symbol(""), 
           exchangeId(core::ExchangeId()), 
           state(INACTIVE),
           lastUpdateTime(std::chrono::system_clock::now()),
           lastPrice(0.0),
           previousPrice(0.0) {}
 };
 
 /**
  * Overall performance metrics for a strategy
  */
 struct StrategyPerformance {
     uint64_t signalsGenerated;                         // Total signals generated
     uint64_t signalsExecuted;                          // Total signals executed
     uint64_t successfulSignals;                        // Successfully executed signals
     uint64_t failedSignals;                            // Failed signals
     double totalPnl;                                   // Total profit and loss
     double averageSignalStrength;                      // Average signal strength
     std::chrono::milliseconds averageSignalDuration;   // Average signal duration
     
     // Default constructor
     StrategyPerformance()
         : signalsGenerated(0),
           signalsExecuted(0),
           successfulSignals(0),
           failedSignals(0),
           totalPnl(0.0),
           averageSignalStrength(0.0),
           averageSignalDuration(std::chrono::milliseconds(0)) {}
 };
 
 } // namespace strategy
 } // namespace trading