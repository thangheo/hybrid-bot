/**
 * @file execution_service.cpp
 * @brief Implementation of the main execution service interface
 * 
 * Created on: May 5, 2025
 */
 #include "execution/execution_service.hpp"
 #include "execution/execution_types.hpp"
 #include "core/market_types.hpp"
 #include <chrono>
 #include <sstream>
 #include <nlohmann/json.hpp>
 #include <mutex>
 #include <algorithm>
 #include <cctype>
 #include <iostream>
 #include <atomic>
#include "execution/preflight/preflight.hpp"
#include "execution/balance_validator_config.hpp"
#include "risk/risk_config.hpp"
#include "risk/interfaces/market_data_interface.hpp"
#include <util/parse_safe.hpp>
#include <mutex>
#include "execution/http_balance_client.hpp"
#include "common/logger.hpp"
 using namespace trading::core;
 namespace trading {
 namespace execution {

 ExecutionService::ExecutionService(
    std::shared_ptr<execution::ExchangeGateway> exchangeGateway,
    std::shared_ptr<OrderGenerator> order_generator,
    std::shared_ptr<SmartOrderRouter> smart_order_router,
    std::shared_ptr<OrderTypeSelector> order_type_selector,
    std::shared_ptr<ExecutionAlgorithmFactory> execution_algorithm_factory,
    std::shared_ptr<OrderLifecycleTracker> order_lifecycle_tracker,
    std::shared_ptr<FillReconciliation> fill_reconciliation)
    : exchangeGateway_(std::move(exchangeGateway))    // NEW: Store gateway
    , order_generator_(std::move(order_generator))
    , smart_order_router_(std::move(smart_order_router))
    , order_type_selector_(std::move(order_type_selector))
    , execution_algorithm_factory_(std::move(execution_algorithm_factory))
    , order_lifecycle_tracker_(std::move(order_lifecycle_tracker))
    , fill_reconciliation_(std::move(fill_reconciliation))
    , api_client_manager_(nullptr)                   // NEW: No direct API manager
{
    if (!exchangeGateway_) {
        throw std::invalid_argument("ExchangeGateway cannot be null");
    }
    // Get the underlying API client manager from the gateway for compatibility
    api_client_manager_ = exchangeGateway_->getApiClientManager();
    
    // Initialize balance validator with configuration from file
    BalanceValidatorConfig balanceConfig = BalanceValidatorConfigLoader::loadFromFile("config/balance_validation.json");
    // Initialize the balance validator instance
    balanceValidator_ = std::make_unique<BalanceValidator>(balanceConfig);
    // Provide the exchange gateway to the validator so it can fetch fresh balances when needed
    balanceValidator_->setExchangeGateway(exchangeGateway_);
}

// Legacy constructor: direct ApiClientManager (no ExchangeGateway)
ExecutionService::ExecutionService(
    std::shared_ptr<OrderGenerator> order_generator,
    std::shared_ptr<SmartOrderRouter> smart_order_router,
    std::shared_ptr<OrderTypeSelector> order_type_selector,
    std::shared_ptr<ExecutionAlgorithmFactory> execution_algorithm_factory,
    std::shared_ptr<OrderLifecycleTracker> order_lifecycle_tracker,
    std::shared_ptr<FillReconciliation> fill_reconciliation,
    std::shared_ptr<ApiClientManager> api_client_manager)
    : exchangeGateway_(nullptr)
    , order_generator_(std::move(order_generator))
    , smart_order_router_(std::move(smart_order_router))
    , order_type_selector_(std::move(order_type_selector))
    , execution_algorithm_factory_(std::move(execution_algorithm_factory))
    , order_lifecycle_tracker_(std::move(order_lifecycle_tracker))
    , fill_reconciliation_(std::move(fill_reconciliation))
    , api_client_manager_(std::move(api_client_manager))
{
    // Initialize balance validator
    BalanceValidatorConfig balanceConfig = BalanceValidatorConfigLoader::loadFromFile("config/balance_validation.json");
    balanceValidator_ = std::make_unique<BalanceValidator>(balanceConfig);
    // No gateway available; validator may use api clients at runtime when provided
}

ExecutionService::~ExecutionService() {
    try {
        shutdown();
    } catch (...) {
        // swallow
    }
}

bool ExecutionService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_initialized_) {
        return true; // Already initialized
    }

    // Initialize all components
    bool success = true;

    // Initialize API client manager first
    if (api_client_manager_) {
        success &= api_client_manager_->initialize();
        if (!success) {
            std::cerr << "Failed to initialize API Client Manager." << std::endl;
            return false;
        }
    }

    // Initialize other components
    if (order_lifecycle_tracker_) {
        success &= order_lifecycle_tracker_->initialize();
        std::cout << "Order Lifecycle Tracker initialized." << std::endl;
    }
    if (fill_reconciliation_) {
        success &= fill_reconciliation_->initialize();
        std::cout << "Fill Reconciliation initialized." << std::endl;
    }

    // Start reconciliation process
    if (success) {
        is_initialized_ = true;
        is_running_ = true;

        if (fill_reconciliation_) {
            fill_reconciliation_->start();
        }
    }

    return success;
}
 
 void ExecutionService::shutdown() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (!is_running_) {
         return; // Already shut down
     }
     
     // Stop fill reconciliation
     fill_reconciliation_->stop();
     
     // Shutdown components in reverse order
     api_client_manager_->shutdown();
     order_lifecycle_tracker_->shutdown();
     
     is_running_ = false;
 }
 std::shared_ptr<ExchangeApiClient> ExecutionService::getExchangeClient(const core::ExchangeId& exchangeId) {
    if (exchangeGateway_) {
        // Use gateway's underlying API client manager
        return exchangeGateway_->getApiClientManager()->getClient(exchangeId);
    } else {
        // Use direct API client manager (legacy mode)
        return api_client_manager_->getClient(exchangeId);
    }
}

// EXAMPLE: Modified executeSignal method (only the API call part changes!)
ExecutionResponse ExecutionService::executeSignal(const TradingSignal& signal) {
    auto start = std::chrono::steady_clock::now();
    LOG_TRACE("ExecutionService::executeSignal START: id={} sym={} direction={} targetSize={} conf={}",
              signal.id, signal.symbol.toString(), (signal.direction==core::Direction::BUY?"BUY":"SELL"), signal.targetSize, signal.confidence);
    std::unique_lock<std::mutex> lk(mutex_);
    // Generate order from signal first so we can initialize a safe ExecutionResponse
    auto order = createOrderFromSignal(signal);

    ExecutionResponse response;
    // Initialize response with safe defaults so any early callback or error path
    // does not see uninitialized memory (avoids weird values in position tracker)
    response.client_order_id = order.getClientOrderId();
    response.exchange_order_id = "";
    response.success = false;
    response.error_message = "";
    response.timestamp = std::chrono::system_clock::now();
    response.order_status = "";
    // default to the parent order's symbol and quantity
    response.symbol = order.getTradingPair().toString();
    response.exchange_id = "";
    response.quantity = order.getQuantity();
    response.filled_quantity = 0.0;

    try {
        // 1. Generate order from signal (UNCHANGED)
        // auto order = createOrderFromSignal(signal);
        LOG_TRACE("ExecutionService::executeSignal: created order id={} symbol={}", order.getOrderId(), order.getTradingPair().toString());
        
        // 2. Validate order (UNCHANGED)
        if (!validateOrder(order)) {
            LOG_INFO("ExecutionService::executeSignal", "Order validation failed for signal id={} clientOrderId={}", signal.id, order.getClientOrderId());
            return ExecutionResponse{order.getOrderId(), "", false, "Order validation failed", 
                std::chrono::system_clock::now(), "", signal.symbol.toString(), signal.exchangeId.toString(), order.getQuantity(), 0.0};
        }
        
        // 3. Place order - delegate to placeOrder to ensure consistent pre-submit validation,
        //    routing and lifecycle handling. This prevents bypassing the BalanceValidator
        //    when callers use executeSignal().
        // Unlock before performing the potentially blocking placeOrder call so that
        // other threads can still interact with the service (e.g., query active orders)
        // during network I/O. We re-lock only for a brief moment if needed.
        lk.unlock();
        ExecutionResponse res = placeOrder(order);
        lk.lock();
        auto end = std::chrono::steady_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        LOG_TRACE("ExecutionService::executeSignal END: id={} dur_ms={} success={}", signal.id, dur, res.success);
        return res;
        
    } catch (const std::exception& e) {
        return ExecutionResponse{"", "", false, e.what(), std::chrono::system_clock::now(), "", signal.symbol.toString(), signal.exchangeId.toString(), 0.0, 0.0};
    }
}
 
 std::future<ExecutionResponse> ExecutionService::executeSignalAsync(const TradingSignal& signal) {
     return std::async(std::launch::async, [this, signal]() {
         return this->executeSignal(signal);
     });
 }
// createErrorResponse()
ExecutionResponse validateBalancesForOrder(const models::Order& order){
    // Placeholder implementation
    return ExecutionResponse{
        order.getClientOrderId(), "", true, "",
        std::chrono::system_clock::now(),
        "",
        order.getTradingPair().toString(), order.getExchangeId().toString(),
        order.getQuantity(), 0.0
    };
}
// handleRejection()
ExecutionResponse ExecutionService::createErrorResponse(const models::Order& order, const std::string& error_message){
    return ExecutionResponse{
        order.getClientOrderId(), "", false, error_message,
        std::chrono::system_clock::now(),
        "",
        order.getTradingPair().toString(), order.getExchangeId().toString(),
        order.getQuantity(), 0.0
    };
}
//     ExecutionResponse validateBalancesForOrder(const models::Order& order);

// isExitOrder()
bool ExecutionService::isExitOrder(const models::Order& order){
    // Placeholder logic: assume orders with "EXIT" in strategy ID are exit orders
    std::string strategyId = order.getStrategyId();
    std::transform(strategyId.begin(), strategyId.end(), strategyId.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return strategyId.find("EXIT") != std::string::npos;
}
// shouldBypassBalanceCheck()
bool ExecutionService::shouldBypassBalanceCheck(const models::Order& order){
    // Placeholder logic: bypass if order has extra param "bypass_balance_check" set to "true"
    std::string bypassParam = order.getExtraParam("bypass_balance_check");
    std::transform(bypassParam.begin(), bypassParam.end(), bypassParam.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return bypassParam == "true";
}
// tryAdjustQuantity()
bool ExecutionService::tryAdjustQuantity(models::Order& order){
    // Placeholder implementation: reduce quantity by 10% as an example
    double currentQty = order.getQuantity();
    double adjustedQty = currentQty * 0.9;
    if (adjustedQty < 0.0001) { // Minimum threshold
        return false;
    }
    order.setQuantity(adjustedQty);
    return true;
}


// processExecutionReport()
ExecutionResponse ExecutionService::processExecutionReport(ExecutionResponse& response, const models::Order& order){
    // Placeholder implementation: log the execution report
    LOG_INFO("Execution Report: clientOrderId={} exchangeOrderId={} success={} errorMessage={}",
             response.client_order_id, response.exchange_order_id,
             response.success, response.error_message);
    return response;
}
bool ExecutionService::validatePriceDeviation(const models::Order& order, const core::ExchangeId& exchangeId){
    // Placeholder implementation: always return true
    return true;
}
models::Order ExecutionService::createExchangeOrder(const models::Order& order, const core::ExchangeId& exchangeId){
    models::Order exchange_order = order; // Copy original order
    exchange_order.setExchangeId(exchangeId);
    // Additional exchange-specific customization can be done here
    return exchange_order;
}

// ExecutionResponse ExecutionService::placeOrder(const models::Order& order) {
//     // 1. Early validations
//     if (!is_running_) {
//         return createErrorResponse(order, "Execution service not running");
//     }
    
//     if (!validateOrder(order)) {
//         return createErrorResponse(order, "Invalid order parameters");
//     }

//     // 2. Route order
//     auto order_ptr = std::make_shared<models::Order>(order);
//     RoutingResult routing_result = smart_order_router_->routeOrder(order_ptr);
//     ExchangeId exchangeId = routing_result.exchangeId;
    
//     if (!exchangeId.isValid()) {
//         return createErrorResponse(order, "No suitable exchange found");
//     }

//     // 3. Create exchange order (fix duplicate setOrderId) use order generator if needed
//     auto exchange_order = createExchangeOrder(order, exchangeId);
//     order_lifecycle_tracker_->addOrder(exchange_order); // Track exchange-level order

//     // 4. Get API client
//     auto api_client = api_client_manager_->getClient(exchangeId);
//     if (!api_client) {
//         return createErrorResponse(exchange_order, "Exchange API client not available");
//     }

//     // 5. Pre-submit price validation (keep as-is, this is good)
//     if (!validatePriceDeviation(exchange_order, exchangeId)) {
//         return createErrorResponse(exchange_order, "Price deviation too large");
//     }

//     // 6. Balance validation - SIMPLIFIED to ONE check with optional retry
//     bool isExitSignal = isExitOrder(exchange_order);
//     bool skipBalance = balanceValidator_->skipBalanceForExits() && isExitSignal;
    
//     if (!skipBalance) {
//         // Try cached first
//         auto balanceResult = balanceValidator_->validateOrder(exchange_order, exchangeId, false);
        
//         if (!balanceResult.isValid) {
//             // Try fresh validation once
//             balanceResult = balanceValidator_->validateOrder(exchange_order, exchangeId, true);
            
//             if (!balanceResult.isValid) {
//                 // Handle quantity adjustment if possible
//                 if (balanceResult.canAdjust && balanceResult.suggestedQuantity > 0) {
//                         if (!tryAdjustQuantity(exchange_order)) {
//                         return createErrorResponse(exchange_order, balanceResult.reason);
//                     }
//                     } else if (shouldBypassBalanceCheck(exchange_order)) {
//                     LOG_WARN("Bypassing balance check for exit order");
//                 } else {
//                     return createErrorResponse(exchange_order, balanceResult.reason);
//                 }
//             }
//         }
//     }

//     // 7. Submit order (remove redundant code)
//     try {
//         execution::models::ExecutionReport executionReport;
        
//         if (exchangeGateway_) {
//             executionReport = exchangeGateway_->placeOrder(exchange_order).get();
//         } else {
//             executionReport = api_client->placeOrder(exchange_order).get();
//         }
        
//         // 8. Handle result
//         ExecutionResponse response = (
//             ExecutionResponse{
//                 exchange_order.getClientOrderId(),
//                 executionReport.exchangeOrderId(),
//                 executionReport.status() != OrderState::REJECTED,
//                 executionReport.rejectReason(),
//                 std::chrono::system_clock::now(),
//                 executionReport.isComplete() ? "COMPLETED" : "PENDING",
//                 exchange_order.getTradingPair().toString(),
//                 exchangeId.toString(),
//                 exchange_order.getQuantity(),
//                 executionReport.filledQuantity()
//             }
//         );
        
//         // 9. Trigger callback AFTER execution succeeds
//         if (execution_callback_ && response.success) {
//             try {
//                 execution_callback_(response);
//             } catch (const std::exception& e) {
//                 LOG_ERROR("Execution callback failed: {}", e.what());
//             }
//         }
        
//         return response;
        
//     } catch (const std::exception& e) {
//         return createErrorResponse(exchange_order, 
//             std::string("Exception during order placement: ") + e.what());
//     }
// }

 ExecutionResponse ExecutionService::placeOrder(const models::Order& order) {
    // Validate service state
    if (!is_running_) {
        LOG_WARN("ExecutionService::placeOrder - Execution service not running for clientOrderId={}", order.getClientOrderId());
        return ExecutionResponse{
            "", "", false, "Execution service not running",
            std::chrono::system_clock::now(),
            "",
            "", "", 0.0, 0.0
        };
    }
    
    // Validate order
    if (!validateOrder(order)) {
        LOG_INFO("ExecutionService::placeOrder - Order validation failed for clientOrderId={}", order.getClientOrderId());
        return ExecutionResponse{
            order.getClientOrderId(), "", false,
            "Invalid order parameters",
            std::chrono::system_clock::now(),
            "",
            "", "", order.getQuantity(), 0.0
        };
    }
    ExecutionResponse response;
    auto order_ptr = std::make_shared<models::Order>(order);
    RoutingResult routing_result = smart_order_router_->routeOrder(order_ptr);
    ExchangeId exchangeId = routing_result.exchangeId;
    
    // NOTE: Do not call execution callback here; wait until after we have a full
    // execution response to avoid downstream components receiving incomplete
    // data (e.g. missing symbol leading to '/USDT' parsing issues).
    if (!exchangeId.isValid()) {
        LOG_WARN("ExecutionService::placeOrder - No suitable exchange found for clientOrderId={}", order.getClientOrderId());
        return ExecutionResponse{
            order.getClientOrderId(), "", false,
            "No suitable exchange found",
            std::chrono::system_clock::now(),
            "",
            "", "", order.getQuantity(), 0.0
        };
    }
    
    // Create a copy of the order with the exchange ID
    // Use a shared_ptr to avoid copy constructor issues
    auto exchange_order = std::make_shared<models::Order>();
    // *exchange_order = order;  // Copy the order
        // Do this:
    exchange_order->setOrderId(order.getOrderId());
    exchange_order->setClientOrderId(order.getClientOrderId());
    exchange_order->setTradingPair(order.getTradingPair());
    exchange_order->setSide(order.getSide());
    exchange_order->setType(order.getType());
    exchange_order->setQuantity(order.getQuantity());
    exchange_order->setPrice(order.getPrice());
    exchange_order->setStopPrice(order.getStopPrice());
    exchange_order->setTimeInForce(order.getTimeInForce());
    exchange_order->setExpiryTime(order.getExpiryTime());
    exchange_order->setStrategyId(order.getStrategyId());
    exchange_order->setSignalId(order.getSignalId());
    exchange_order->setExchangeId(exchangeId);
    exchange_order->setExchangeOrderId(order.getExchangeOrderId());
    exchange_order->setState(order.getState());
    // for (const auto& param : order.getExtraParams()) {
    //     exchange_order->setExtraParam(param.first, param.second);
    // }
    // exchange_order->setExtraParam(order.getExtraParams());

    // Register the order with the lifecycle tracker
    order_lifecycle_tracker_->addOrder(*exchange_order);
    // Diagnostic trace so we can correlate lifecycle entries with incoming logs
    LOG_TRACE("ExecutionService::placeOrder - Order registered in lifecycle (clientOrderId={} exchange={}) qty={} price={}",
              exchange_order->getClientOrderId(), exchange_order->getExchangeId().toString(), exchange_order->getQuantity(), exchange_order->getPrice());
    
    // Get the exchange API client
    auto api_client = api_client_manager_->getClient(exchangeId);
    if (!api_client) {
        LOG_WARN("ExecutionService::placeOrder - No API client available for exchange {} for clientOrderId={}", exchangeId.toString(), order.getClientOrderId());
        order_lifecycle_tracker_->updateOrderState(
            exchange_order->getClientOrderId(), 
            models::OrderState::REJECTED, 
            exchange_order->getExchangeOrderId(),
            "Exchange API client not available"
        );
        
        return ExecutionResponse{
            exchange_order->getClientOrderId(),
            "",
            false,
            "Exchange API client not available",
            std::chrono::system_clock::now(),
            "",
            "",
            "",
            exchange_order->getQuantity(),
            0.0
        };
    }
    
    // === Pre-submit validation ===
    // Prevent obvious price/symbol mismatches from being sent to the exchange.
    // Heuristic: for LIMIT orders, compare requested price to current market price
    // from the exchange (if available). If deviation is too large, reject the
    // order locally to avoid exchange -1013 / filter rejections.
    try {
        LOG_INFO("ExecutionService::placeOrder - Performing pre-submit validation for clientOrderId={}", exchange_order->getClientOrderId());
        if (exchange_order->getType() == models::OrderType::LIMIT) {
            const double requestedPrice = exchange_order->getPrice();
            const std::string normalizedSymbol = trading::execution::util::normalizeSymbolForExchange(exchange_order->getTradingPair().toString());
            double marketPrice = 0.0;

            // Try to get market price via gateway (preferred) or via client
            if (exchangeGateway_) {
                LOG_TRACE("ExecutionService::placeOrder - Fetching market price via ExchangeGateway for clientOrderId={}", exchange_order->getClientOrderId());
                auto fut = exchangeGateway_->getCurrentPrice(exchangeId, exchange_order->getTradingPair(), false);
                marketPrice = fut.get();
            } else if (api_client) {
                LOG_TRACE("ExecutionService::placeOrder - Fetching market price via ApiClient for clientOrderId={}", exchange_order->getClientOrderId());
                auto fut = api_client->getCurrentPrice(exchange_order->getTradingPair());
                marketPrice = fut.get();
            }
            else{
                LOG_WARN("ExecutionService::placeOrder - No method available to fetch market price for pre-submit validation for clientOrderId={}", exchange_order->getClientOrderId());
                
            }

            if (marketPrice > 0.0 && requestedPrice > 0.0) {
                LOG_INFO("ExecutionService::placeOrder - Market price fetched for pre-submit validation for clientOrderId={} (symbol={}) requestedPrice={} marketPrice={}",
                         exchange_order->getClientOrderId(), exchange_order->getTradingPair().toString(), requestedPrice, marketPrice);
                double deviation = std::abs(requestedPrice - marketPrice) / marketPrice;
                const double kMaxDeviation = 0.50; // 50% default pre-submit threshold (configurable later)
                if (deviation > kMaxDeviation) {
                    std::ostringstream oss;
                    oss << "Pre-submit validation failed: price deviation too large for " << normalizedSymbol
                        << " (requested=" << requestedPrice << ", market=" << marketPrice
                        << ", deviation=" << (deviation * 100.0) << "%)";
                    const std::string reason = oss.str();

                    // Mark order rejected in lifecycle tracker
                    order_lifecycle_tracker_->updateOrderState(
                        exchange_order->getClientOrderId(),
                        models::OrderState::REJECTED,
                        exchange_order->getExchangeOrderId(),
                        reason
                    );
                    LOG_WARN("ExecutionService::placeOrder - Pre-submit validation failed for clientOrderId={} reason={}", exchange_order->getClientOrderId(), reason);
                    return ExecutionResponse{
                        exchange_order->getClientOrderId(),
                        "",
                        false,
                        reason,
                        std::chrono::system_clock::now(),
                        "REJECTED_PRE_SUBMIT",
                        "",
                        "",
                        exchange_order->getQuantity(),
                        0.0
                    };
                }
            } else {
                // marketPrice not available or requestedPrice invalid; log a warning and continue
                LOG_WARN("ExecutionService::placeOrder -Could not fetch market price for pre-submit validation for clientOrderId={} (symbol={}) or invalid requested price={}. Proceeding without exchange filter checks.",
                         exchange_order->getClientOrderId(), exchange_order->getTradingPair().toString(), exchange_order->getPrice());
            }
        }
    } catch (const std::exception& e) {
        // If validation cannot complete (temporary error), do not block submission; log and continue.
        LOG_ERROR("ExecutionService::placeOrder - Exception during pre-submit validation for clientOrderId={}: {}. Proceeding without exchange filter checks.",
                  exchange_order->getClientOrderId(), e.what());
    }

    // Submit the order to the exchange
    // auto future_result = api_client->placeOrder(*exchange_order);
    execution::models::ExecutionReport executionReport;
    
    // Read min-notional from risk config (orderValidation.minTradeValue) once and cache it
    static double cachedMinNotional = 0.0;
    static std::mutex cachedMinNotionalMutex;

    // === Professional Balance Validation ===
    bool skipAllBalanceValidation = false;
    bool isExitSignalFlag = false;
    try {
        LOG_TRACE("ExecutionService::placeOrder - Checking is_exit_signal for clientOrderId={}", exchange_order->getClientOrderId());
        std::string v = exchange_order->getExtraParam("is_exit_signal");
        if (!v.empty() && v == "true") isExitSignalFlag = true;
    } catch (...) { /* ignore */ }
    try {
        bool cfgSkip = false;
        try { cfgSkip = balanceValidator_->skipBalanceForExits(); } catch (...) { cfgSkip = false; }
        if (cfgSkip) {
            try {
                std::string v = exchange_order->getExtraParam("is_exit_signal");
                if (!v.empty() && v == "true") skipAllBalanceValidation = true;
            } catch (...) { /* ignore */ }
        }
        LOG_TRACE("ExecutionService::placeOrder - skipAllBalanceValidation={}", skipAllBalanceValidation);
        if (skipAllBalanceValidation) {
            std::cout << "[BALANCE] SKIPPING all balance validation for exit order " << exchange_order->getClientOrderId() << std::endl;
        } else {
            // Use the new balance validator for comprehensive checking (cached read)
            std::cout << "[BALANCE] Preflight (cached) validation for order " << exchange_order->getClientOrderId()
                      << " on exchange " << exchangeId.toString() << std::endl;
            auto balanceResult = balanceValidator_->validateOrder(*exchange_order, exchangeId, false);
            auto bstart = std::chrono::steady_clock::now();
            LOG_TRACE("BalanceValidator: cached validateOrder called for {} (exchange={})", exchange_order->getClientOrderId(), exchangeId.toString());
            
            if (!balanceResult.isValid) {
                std::cerr << "❌ Order rejected due to insufficient balance:" << std::endl;
                // Log for debugging to make it easy to trace failures in CI/UI
                LOG_INFO("ExecutionService::placeOrder - BalanceValidator rejected clientOrderId={} reason='{}' required={} available={} canAdjust={}",
                         exchange_order->getClientOrderId(), balanceResult.reason, balanceResult.requiredBalance, balanceResult.availableBalance, balanceResult.canAdjust);

                // If the only reason is minimum notional requirement and this is an
                // exit flow, bypass that check (user requested exits to be tolerant).
                if (isExitSignalFlag && balanceResult.reason.find("minimum notional") != std::string::npos) {
                    std::cout << "[BALANCE] Bypassing min-notional rejection for exit order " << exchange_order->getClientOrderId() << std::endl;
                    // Treat as valid and continue (do not early-return)
                } else {
                    // Suggest quantity adjustment if possible
                    if (balanceResult.canAdjust && balanceResult.suggestedQuantity > 0) {
                        std::cout << "   💡 Suggested quantity: " << balanceResult.suggestedQuantity << std::endl;

                        // Attempt to adjust the order quantity to suggestedQuantity and re-run validation.
                        try {
                            double origQty = exchange_order->getQuantity();
                            exchange_order->setQuantity(balanceResult.suggestedQuantity);

                            std::cout << "[BALANCE] Attempting adjusted quantity " << balanceResult.suggestedQuantity
                                    << " for order " << exchange_order->getClientOrderId() << std::endl;

                            auto adjResult = balanceValidator_->validateOrder(*exchange_order, exchangeId, false);
                            if (adjResult.isValid) {
                                std::cout << "✅ Adjusted quantity passed balance validation; proceeding with adjusted order." << std::endl;
                                // overwrite balanceResult so subsequent logging reflects adjusted quantity/requirements
                                balanceResult = adjResult;

                                // update lifecycle tracker with new quantity (best-effort)
                                order_lifecycle_tracker_->updateOrderState(
                                    exchange_order->getClientOrderId(),
                                    models::OrderState::PENDING
                                );
                                // continue to submission with adjusted quantity
                            } else {
                                // restore original quantity for reporting
                                exchange_order->setQuantity(origQty);
                                std::cout << "Adjusted quantity still insufficient: " << adjResult.reason << std::endl;
                                order_lifecycle_tracker_->updateOrderState(
                                    exchange_order->getClientOrderId(),
                                    models::OrderState::REJECTED,
                                    exchange_order->getExchangeOrderId(),
                                    adjResult.reason
                                );

                                LOG_INFO("ExecutionService::placeOrder - Final decision: rejecting order {} after attempted adjustment: reason='{}' suggestedQty={}",
                                         exchange_order->getClientOrderId(), adjResult.reason, adjResult.suggestedQuantity);
                                return ExecutionResponse{
                                    exchange_order->getClientOrderId(),
                                    "",
                                    false,
                                    adjResult.reason,
                                    std::chrono::system_clock::now(),
                                    "REJECTED_INSUFFICIENT_BALANCE",
                                    "", "", exchange_order->getQuantity(), 0.0
                                };
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "Warning: failed to attempt suggested quantity adjustment: " << e.what() << std::endl;
                            // Fallback to original rejection path below
                            order_lifecycle_tracker_->updateOrderState(
                                exchange_order->getClientOrderId(),
                                models::OrderState::REJECTED,
                                exchange_order->getExchangeOrderId(),
                                balanceResult.reason
                            );

                            LOG_INFO("ExecutionService::placeOrder - Final decision: rejecting order {} due to balance validation failure: reason='{}'",
                                     exchange_order->getClientOrderId(), balanceResult.reason);
                            return ExecutionResponse{
                                exchange_order->getClientOrderId(),
                                "",
                                false,
                                balanceResult.reason,
                                std::chrono::system_clock::now(),
                                "REJECTED_INSUFFICIENT_BALANCE",
                                "", "", exchange_order->getQuantity(), 0.0
                            };
                    }
                        
                } 
                else {
                    // Before rejecting based on cached data, perform one forced fresh validation
                    // This helps avoid false negatives when a recent fill hasn't propagated to
                    // the cached DataInterface or local cache yet (common after immediate fills).
                    try {
                        std::cout << "[BALANCE] Cached validation failed - attempting forced fresh validation for order "
                                  << exchange_order->getClientOrderId() << std::endl;
                        auto freshStart = std::chrono::steady_clock::now();
                        LOG_TRACE("BalanceValidator: forced fresh validateOrder called for {} (exchange={})", exchange_order->getClientOrderId(), exchangeId.toString());
                        auto freshResult = balanceValidator_->validateOrder(*exchange_order, exchangeId, true);
                        auto freshEnd = std::chrono::steady_clock::now();
                        LOG_TRACE("BalanceValidator: forced fresh validation duration_ms={} for {}", std::chrono::duration_cast<std::chrono::milliseconds>(freshEnd - freshStart).count(), exchange_order->getClientOrderId());
                        if (freshResult.isValid) {
                            std::cout << "\u2705 Fresh balance validation PASSED after cache refresh; proceeding with order." << std::endl;
                            // overwrite balanceResult so subsequent logging reflects fresh data
                            balanceResult = freshResult;
                            // continue to submission
                        } 
                        else {
                            // If this is an exit signal and the failure is due solely to
                            // minimum notional, bypass and proceed. Otherwise reject.
                            if (isExitSignalFlag && freshResult.reason.find("minimum notional") != std::string::npos) {
                                std::cout << "[BALANCE] Fresh validation failed only due to min-notional; bypassing for exit order "
                                          << exchange_order->getClientOrderId() << std::endl;
                                // allow submission to proceed
                                balanceResult = freshResult; // keep the reason for logs but continue
                            } else {
                                // Still insufficient after fresh fetch - reject
                                order_lifecycle_tracker_->updateOrderState(
                                    exchange_order->getClientOrderId(),
                                    models::OrderState::REJECTED,
                                    exchange_order->getExchangeOrderId(),
                                    freshResult.reason
                                );

                                LOG_INFO("ExecutionService::placeOrder - Final decision: rejecting order {} after fresh validation failed: reason='{}' required={} available={}",
                                         exchange_order->getClientOrderId(), freshResult.reason, freshResult.requiredBalance, freshResult.availableBalance);
                                return ExecutionResponse{
                                    exchange_order->getClientOrderId(),
                                    "",
                                    false,
                                    freshResult.reason,
                                    std::chrono::system_clock::now(),
                                    "REJECTED_INSUFFICIENT_BALANCE",
                                    "", "", exchange_order->getQuantity(), 0.0
                                };
                            }
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Warning: forced fresh balance validation threw: " << e.what()
                                  << ". Falling back to original rejection reason." << std::endl;
                        order_lifecycle_tracker_->updateOrderState(
                            exchange_order->getClientOrderId(),
                            models::OrderState::REJECTED,
                            exchange_order->getExchangeOrderId(),
                            balanceResult.reason
                        );

                        LOG_INFO("ExecutionService::placeOrder - Final decision: rejecting order {} after forced fresh validation threw: reason='{}'",
                                 exchange_order->getClientOrderId(), balanceResult.reason);
                        return ExecutionResponse{
                            exchange_order->getClientOrderId(),
                            "",
                            false,
                            balanceResult.reason,
                            std::chrono::system_clock::now(),
                            "REJECTED_INSUFFICIENT_BALANCE",
                            "", "", exchange_order->getQuantity(), 0.0
                        };
                    }
                }
            }
        }
            
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: balance validation failed with error: " << e.what() << ". Proceeding with legacy checks." << std::endl;

        
        // === Final Balance Check with Fresh Data ===
        // Perform one more balance validation with forced refresh before submitting
        if (!skipAllBalanceValidation) {
            auto finalBalanceResult = balanceValidator_->validateOrder(*exchange_order, exchangeId, true);
                LOG_TRACE("BalanceValidator: final forced validation result for {}: isValid={} reason={}", exchange_order->getClientOrderId(), finalBalanceResult.isValid, finalBalanceResult.reason);
            
            if (!finalBalanceResult.isValid) {
                    std::cout << "❌ Final balance check failed with fresh data:" << std::endl;
                    std::cout << "   Reason: " << finalBalanceResult.reason << std::endl;
                    std::cout << "   Required: " << finalBalanceResult.requiredBalance << std::endl;
                    std::cout << "   Available: " << finalBalanceResult.availableBalance << std::endl;

                    // If this is an exit signal, be tolerant and bypass insufficient-balance
                    // rejections (user requested immediate exits). Only bypass when the
                    // reason indicates insufficient balance or min-notional.
                    if (isExitSignalFlag && (finalBalanceResult.reason.find("Insufficient") != std::string::npos ||
                                             finalBalanceResult.reason.find("minimum notional") != std::string::npos)) {
                        std::cout << "[BALANCE] Bypassing final balance rejection for exit order " << exchange_order->getClientOrderId() << std::endl;
                        // allow execution to continue
                    } else {
                        order_lifecycle_tracker_->updateOrderState(
                            exchange_order->getClientOrderId(),
                            models::OrderState::REJECTED,
                            exchange_order->getExchangeOrderId(),
                            "Final balance check failed: " + finalBalanceResult.reason
                        );

                        LOG_INFO("ExecutionService::placeOrder - Final decision: rejecting order {} after final balance check failed: reason='{}' required={} available={}",
                                 exchange_order->getClientOrderId(), finalBalanceResult.reason, finalBalanceResult.requiredBalance, finalBalanceResult.availableBalance);
                        return ExecutionResponse{
                            exchange_order->getClientOrderId(),
                            "",
                            false,
                            "Final balance check failed: " + finalBalanceResult.reason,
                            std::chrono::system_clock::now(),
                            "REJECTED_FINAL_BALANCE_CHECK",
                            "", ""
                        };
                    }
                }
            
            std::cout << "✅ Final balance check passed - proceeding to submit order" << std::endl;
        } else {
            std::cout << "[BALANCE] Skipped final forced balance validation for exit order " << exchange_order->getClientOrderId() << std::endl;
        }
    }

        // Before submitting, perform a forced fresh validation to avoid cached-stale passes
        try {
            auto finalValidation = balanceValidator_->validateOrder(*exchange_order, exchangeId, true);
            if (!finalValidation.isValid) {
                std::cout << "❌ Final forced balance validation failed before submit: " << finalValidation.reason << std::endl;

                // If this is an exit order, allow bypass for common non-fatal reasons
                bool bypassFinal = false;
                if (isExitSignalFlag) {
                    std::string low = finalValidation.reason;
                    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                    if (low.find("insufficient") != std::string::npos || low.find("minimum notional") != std::string::npos) {
                        bypassFinal = true;
                    }
                }

                if (bypassFinal) {
                    std::cout << "[BALANCE] Bypassing final forced validation for exit order " << exchange_order->getClientOrderId() << std::endl;
                } else {
                    order_lifecycle_tracker_->updateOrderState(
                        exchange_order->getClientOrderId(),
                        models::OrderState::REJECTED,
                        exchange_order->getExchangeOrderId(),
                        "Final balance validation failed before submit: " + finalValidation.reason
                    );

                    return ExecutionResponse{
                        exchange_order->getClientOrderId(),
                        "",
                        false,
                        std::string("Final balance validation failed before submit: ") + finalValidation.reason,
                        std::chrono::system_clock::now(),
                        "REJECTED_FINAL_BALANCE_CHECK",
                        "", ""
                    };
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: exception during final balance validation: " << e.what() << " - proceeding to submit (best-effort)" << std::endl;
        }

        if (exchangeGateway_) {
            // NEW: Use ExchangeGateway (with caching and rate limiting)
                LOG_TRACE("ExecutionService::placeOrder: calling ExchangeGateway::placeOrder for clientOrderId={}", exchange_order->getClientOrderId());
            auto executionFuture = exchangeGateway_->placeOrder(*exchange_order);
                LOG_TRACE("ExecutionService::placeOrder: ExchangeGateway::placeOrder returned future for clientOrderId={}", exchange_order->getClientOrderId());
            executionReport = executionFuture.get();
                LOG_TRACE("ExecutionService::placeOrder: ExchangeGateway::placeOrder finished for clientOrderId={}", exchange_order->getClientOrderId());
        } else {
            // EXISTING: Use direct API client (legacy mode)
            auto client = api_client_manager_->getClient(exchange_order->getExchangeId());
            if (!client) {
                order_lifecycle_tracker_->updateOrderState(
                    exchange_order->getClientOrderId(),
                    models::OrderState::REJECTED,
                    exchange_order->getExchangeOrderId(),
                    "Exchange API client not available"
                );

                return ExecutionResponse{
                    exchange_order->getClientOrderId(), "", false,
                    "Exchange API client not available",
                    std::chrono::system_clock::now(),
                    "",
                    "", ""
                };
            }

            LOG_TRACE("ExecutionService::placeOrder: direct api_client->placeOrder for clientOrderId={}", exchange_order->getClientOrderId());
            auto executionFuture = client->placeOrder(*exchange_order);
            LOG_TRACE("ExecutionService::placeOrder: direct api_client->placeOrder returned future for clientOrderId={}", exchange_order->getClientOrderId());
            executionReport = executionFuture.get();
            LOG_TRACE("ExecutionService::placeOrder: api_client->placeOrder get() finished for clientOrderId={}", exchange_order->getClientOrderId());
        }
    
    // Wait for the result
    models::ExecutionReport result;
    try {
        result = executionReport;  // Wait for the async operation to complete

        // Diagnostic: print execution report summary for debugging
        try {
            std::cerr << "[ExecutionService] Received ExecutionReport: " << result.toString() << std::endl;
        } catch (...) {}
        // Also emit structured log so it shows in configured log sinks
        try {
            LOG_INFO("ExecutionService::placeOrder - Received ExecutionReport for clientOrderId={} => {}",
                     exchange_order->getClientOrderId(), result.toString());
        } catch (...) {}
        
        // Update order state based on exchange response
        if (result.status() != models::OrderState::REJECTED) {
            // Update with exchange order ID
            std::string clientOrderId = exchange_order->getClientOrderId();
            models::Order& order_ref = *exchange_order;
            order_lifecycle_tracker_->updateOrderWithExchangeId(
                order_ref,
                result.exchangeOrderId()
            );
            
            // Update order state to match the execution report
            order_lifecycle_tracker_->updateOrderState(
                clientOrderId,
                result.status()
            );
        } else {
            // Update order state to REJECTED
            std::string clientOrderId = exchange_order->getClientOrderId();
            order_lifecycle_tracker_->updateOrderState(
                clientOrderId,
                models::OrderState::REJECTED,
                result.exchangeOrderId(),
                result.rejectReason()
            );

            // If the rejection was due to insufficient balance (e.g., Binance -2010),
            // force-refresh cached balances so the system does not rely on stale data.
            try {
                const std::string& reason = result.rejectReason();
                bool looksLikeInsufficient = false;
                if (!reason.empty()) {
                    std::string low = reason;
                    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                    if (low.find("insufficient") != std::string::npos || low.find("-2010") != std::string::npos) {
                        looksLikeInsufficient = true;
                    }
                }

                if (looksLikeInsufficient) {
                    std::cout << "🔄 Insufficient balance detected, refreshing cache for exchange: " << exchangeId.toString() << std::endl;

                    // Use our balance validator to refresh the cache
                    try {
                        balanceValidator_->clearBalanceCache(exchangeId);
                        std::cout << "✅ Balance cache cleared for exchange: " << exchangeId.toString() << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "Warning: failed to clear balance cache after rejection: " << e.what() << std::endl;
                    }

                    // After clearing cache, revalidate with fresh data and optionally retry once
                    try {
                        std::cout << "[BALANCE] Performing forced fresh validation before optional retry for order " << exchange_order->getClientOrderId() << std::endl;
                        auto freshValidation = balanceValidator_->validateOrder(*exchange_order, exchangeId, true);
                        if (freshValidation.isValid) {
                            std::cout << "[BALANCE] Fresh validation PASSED, attempting one retry submit for " << exchange_order->getClientOrderId() << std::endl;
                                try {
                                    // perform one resubmit attempt using the possibly-adjusted exchange_order
                                    if (exchangeGateway_) {
                                        auto retryFuture = exchangeGateway_->placeOrder(*exchange_order);
                                        executionReport = retryFuture.get();
                                    } else {
                                        auto client = api_client_manager_->getClient(exchangeId);
                                        if (client) {
                                            auto retryFuture = client->placeOrder(*exchange_order);
                                            executionReport = retryFuture.get();
                                        } else {
                                            std::cerr << "Warning: cannot retry submit - api client unavailable for exchange: " << exchangeId.toString() << std::endl;
                                        }
                                    }

                                // Refresh our result to the retry outcome
                                try {
                                    result = executionReport;
                                } catch (...) {
                                    // If executionReport cannot be converted for some reason, ignore and leave original result
                                }

                                std::cout << "[RETRY] Resubmit attempt completed for " << exchange_order->getClientOrderId() << std::endl;
                            } catch (const std::exception& e) {
                                std::cerr << "Warning: retry submit failed with exception: " << e.what() << std::endl;
                            }
                        } else {
                            std::cout << "[BALANCE] Fresh validation still fails after cache refresh: " << freshValidation.reason << std::endl;
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Warning: fresh validation before retry failed: " << e.what() << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                // Non-critical; just log
                std::cerr << "Warning: error while handling rejection refresh: " << e.what() << std::endl;
            }
        }
        
        // Prepare response
        ExecutionResponse response;
        response.client_order_id = exchange_order->getClientOrderId();
        response.exchange_order_id = result.exchangeOrderId();
        response.success = (result.status() != models::OrderState::REJECTED);
        response.error_message = result.rejectReason();
        response.order_status = orderStateToString(result.status());
        response.timestamp = std::chrono::system_clock::now();
        response.symbol = exchange_order->getTradingPair().toString();
        response.exchange_id = exchange_order->getExchangeId().toString();
        // Populate quantity fields so downstream components (position tracker, risk)
        // can inspect declared and filled quantities. These are vital for integration
        // tests where filled quantity may be zero but declared quantity is non-zero.
        response.quantity = exchange_order->getQuantity();
        try {
            response.filled_quantity = result.filledQuantity();
        } catch (...) {
            response.filled_quantity = 0.0;
        }
        
        // Trigger execution callback AFTER the response is fully populated so
        // downstream components receive a consistent execution report.
        if (execution_callback_) {
            try {
                execution_callback_(response);
            } catch (const std::exception& e) {
                LOG_ERROR("Execution callback failed: {}", e.what());
            }
        }

        return response;
    } catch (const std::exception& e) {
        // Handle exception from the async operation
        order_lifecycle_tracker_->updateOrderState(
            exchange_order->getClientOrderId(),
            models::OrderState::REJECTED,
            std::string("Exception during order placement: ") + e.what()
        );
        
        return ExecutionResponse{
            exchange_order->getClientOrderId(), "", false, 
            std::string("Exception during order placement: ") + e.what(), std::chrono::system_clock::now(),
            "",
            "", ""
        };
    }
}

// getExchangeGateway
std::shared_ptr<execution::ExchangeGateway> ExecutionService::getExchangeGateway() {
    return exchangeGateway_;
}

CancelResponse ExecutionService::cancelOrder(const std::string& client_order_id) {
    // Validate service state
    if (!is_running_) {
        return CancelResponse{
            client_order_id, "", false, 
            "Execution service not running", 
            std::chrono::system_clock::now()
            
        };
    }
    
    // Get order details from lifecycle tracker
    auto order_opt = order_lifecycle_tracker_->getOrderByClientId(client_order_id);
    if (!order_opt) {
        return CancelResponse{
            client_order_id, "", false, 
            "Order not found", 
            std::chrono::system_clock::now()
        };
    }
    
    // Get the order - create a copy to avoid modifying the original
    auto order = std::make_shared<models::Order>(*order_opt);
    
    // Check if order can be canceled
    if (!order->canCancel()) {
        return CancelResponse{
            client_order_id, order->getExchangeOrderId(), false, 
            "Order cannot be canceled in current state: " + order->toString(), 
            std::chrono::system_clock::now()
        };
    }
    
    // Get exchange API client
    auto api_client = api_client_manager_->getClient(order->getExchangeId());
    if (!api_client) {
        return CancelResponse{
            client_order_id, order->getExchangeOrderId(), false, 
            "Exchange API client not available", 
            std::chrono::system_clock::now()
        };
    }
    
    // Update order state to PENDING_CANCEL
    order_lifecycle_tracker_->updateOrderState(
        client_order_id,
        models::OrderState::PENDING_CANCEL
    );
    
    // Send cancel request to exchange
    // auto future_result = api_client->cancelOrder(order->getExchangeOrderId(), order->getTradingPair());
    execution::models::ExecutionReport executionReport;
        
    if (exchangeGateway_) {
        // NEW: Use ExchangeGateway
        auto cancelFuture = exchangeGateway_->cancelOrder(client_order_id, order->getTradingPair(), order->getExchangeId());
        executionReport = cancelFuture.get();
    } else {
        // EXISTING: Use direct API client
        auto client = api_client_manager_->getClient(order->getExchangeId());
        auto cancelFuture = client->cancelOrder(client_order_id, order->getTradingPair());
        executionReport = cancelFuture.get();
    }
    try {
        // Wait for the result
        models::ExecutionReport result = executionReport;

        // Update order state based on exchange response
        if (result.status() == models::OrderState::CANCELED) {
            order_lifecycle_tracker_->updateOrderState(
                client_order_id,
                models::OrderState::CANCELED
            );
            
            // Prepare success response
            CancelResponse response;
            response.client_order_id = client_order_id;
            response.exchange_order_id = order->getExchangeOrderId();
            response.success = true;
            response.error_message = "";
            response.timestamp = std::chrono::system_clock::now();
            
            return response;
        } else {
            // Revert to previous state if cancel failed
            order_lifecycle_tracker_->revertToPreviousState(client_order_id ,
                models::OrderState::OPEN);
            
            // Prepare failure response
            CancelResponse response;
            response.client_order_id = client_order_id;
            response.exchange_order_id = order->getExchangeOrderId();
            response.success = false;
            response.error_message = "Cancel request rejected by exchange: " + result.rejectReason();
            response.timestamp = std::chrono::system_clock::now();
            
            return response;
        }
    } catch (const std::exception& e) {
        // Revert to previous state if an exception occurred
        order_lifecycle_tracker_->revertToPreviousState(client_order_id,
            models::OrderState::OPEN);
        
        // Prepare error response
        CancelResponse response;
        response.client_order_id = client_order_id;
        response.exchange_order_id = order->getExchangeOrderId();
        response.success = false;
        response.error_message = std::string("Exception during cancel request: ") + e.what();
        response.timestamp = std::chrono::system_clock::now();
        
        return response;
    }
}

// Helper function to convert OrderState to string
std::string ExecutionService::orderStateToString(models::OrderState state) {
    switch (state) {
        case models::OrderState::CREATED: return "CREATED";
        case models::OrderState::PENDING: return "PENDING";
        case models::OrderState::OPEN: return "OPEN";
        case models::OrderState::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case models::OrderState::FILLED: return "FILLED";
        case models::OrderState::CANCELED: return "CANCELED";
        case models::OrderState::REJECTED: return "REJECTED";
        case models::OrderState::PENDING_CANCEL: return "PENDING_CANCEL";
        case models::OrderState::EXPIRED: return "EXPIRED";
        default: return "UNKNOWN";
    }
}
 
 CancelResponse ExecutionService::cancelOrderByExchangeId(
     const std::string& exchange_order_id, 
     const core::ExchangeId& exchangeId) {
     
     // Validate service state
     if (!is_running_) {
         return CancelResponse{
             "", exchange_order_id, false, 
             "Execution service not running", 
             std::chrono::system_clock::now()
         };
     }
     
     // Get client order ID from exchange order ID
     auto client_order_id = order_lifecycle_tracker_->getClientOrderId(exchange_order_id, exchangeId);
     if (client_order_id.empty()) {
         return CancelResponse{
             "", exchange_order_id, false, 
             "models::Order not found", 
             std::chrono::system_clock::now()
         };
     }
     
     // Use the standard cancel method
     return cancelOrder(client_order_id);
 }
  
    std::vector<CancelResponse> ExecutionService::cancelAllOrdersForSymbol(
    const core::Symbol& tradingPair,
    const core::ExchangeId& exchangeId) {
    
    std::vector<CancelResponse> responses;
    
    // Validate service state
    if (!is_running_) {
        responses.push_back(CancelResponse{
            "", "", false, 
            "Execution service not running", 
            std::chrono::system_clock::now()
        });
        return responses;
    }
    
    // Get all active orders for the trading pair and exchange
    std::vector<std::shared_ptr<models::Order>> active_orders;
    
    // Get all active orders first
    auto all_active_orders = order_lifecycle_tracker_->getActiveOrders();
    
    // Filter by trading pair and exchange if needed
    for (const auto& order : all_active_orders) {
        if (order->getTradingPair() == tradingPair) {
            if (exchangeId.toString().empty() || order->getExchangeId() == exchangeId) {
                active_orders.push_back(order);
            }
        }
    }
    
    // Cancel each order
    for (const auto& order : active_orders) {
        responses.push_back(cancelOrder(order->getClientOrderId()));
    }
    
    // If no orders were found, add a response indicating that
    if (responses.empty()) {
        std::string message = "No active orders found for " + tradingPair.toString();
        if (!exchangeId.toString().empty()) {
            message += " on exchange " + exchangeId.toString();
        }
        
        responses.push_back(CancelResponse{
            "", "", true, message, std::chrono::system_clock::now()
        });
    }
    
    return responses;
}

// Update the getOrderStatus method to use shared_ptr
OrderStatusResponse ExecutionService::getOrderStatus(const std::string& client_order_id) {
    // Validate service state
    if (!is_running_) {
        return OrderStatusResponse{
            nullptr, false, "Execution service not running"
        };
    }
    
    // Get order from lifecycle tracker
    auto order_opt = order_lifecycle_tracker_->getOrderByOrderId(client_order_id);
    if (!order_opt) {
        return OrderStatusResponse{
            nullptr, false, "Order not found"
        };
    }
    
    // For active orders, refresh status from exchange
    const auto& order = *order_opt;
    if (order.isActive()) {
        // Get exchange API client
        auto api_client = api_client_manager_->getClient(order.getExchangeId());
        if (api_client) {
            // Query exchange for latest status
            api_client->getOrderStatus(order.getClientOrderId(), order.getTradingPair());
            // Refresh our local copy after potential update
            order_opt = order_lifecycle_tracker_->getOrderByOrderId(client_order_id);
        }
    }
    
    // Prepare response with a shared copy of the order
    OrderStatusResponse response;
    response.order = std::make_shared<models::Order>(*order_opt);
    response.found = true;
    response.error_message = "";
    
    return response;
}
 //order_lifecycle_tracker_

 std::vector<models::Order> ExecutionService::getActiveOrders() {
     if (!is_running_) {
         return {};
     }
     
     auto orderPtrs = order_lifecycle_tracker_->getActiveOrders();
     std::vector<models::Order> orders;
     orders.reserve(orderPtrs.size());
     
     for (const auto& orderPtr : orderPtrs) {
         orders.push_back(*orderPtr);
     }
     
     return orders;
 }
 
 std::vector<models::Order> ExecutionService::getFilledOrders(
     const std::chrono::system_clock::time_point& start_time,
     const std::chrono::system_clock::time_point& end_time) {
     
     if (!is_running_) {
         return {};
     }
     
     auto orderPtrs = order_lifecycle_tracker_->getFilledOrders(start_time, end_time);
     std::vector<models::Order> orders;
     orders.reserve(orderPtrs.size());
     
     for (const auto& orderPtr : orderPtrs) {
         orders.push_back(*orderPtr);
     }
     
     return orders;
 }
 
 std::string ExecutionService::getHealthStatus() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     nlohmann::json health;
     health["service"] = "execution_service";
     health["initialized"] = is_initialized_;
     health["running"] = is_running_;
     
     // Add component statuses
     health["components"]["api_client_manager"] = api_client_manager_->getHealthStatus();
     health["components"]["order_lifecycle_tracker"] = order_lifecycle_tracker_->getHealthStatus();
     health["components"]["fill_reconciliation"] = fill_reconciliation_->getHealthStatus();
     
     // Add exchange connection statuses
     health["exchanges"] = api_client_manager_->getExchangeStatuses();
     
     // Add metrics
     health["metrics"]["active_orders"] = order_lifecycle_tracker_->getActiveOrderCount();
     health["metrics"]["orders_placed_last_hour"] = order_lifecycle_tracker_->getOrdersPlacedInLastHour();
     health["metrics"]["fills_last_hour"] = order_lifecycle_tracker_->getFillsInLastHour();
     
     return health.dump(2);
 }
 // Add this method to ExecutionService class
std::string ExecutionService::executeSignalJson(const std::string& signal_json) {
    try {
        TradingSignal signal = parseTradingSignalFromJson(signal_json);
        ExecutionResponse response = executeSignal(signal);
        return executionResponseToJson(response);
    } catch (const std::exception& e) {
        ExecutionResponse error_response{
            "", "", false, std::string("JSON parsing error: ") + e.what(), 
            std::chrono::system_clock::now(), "", "", ""
        };
        return executionResponseToJson(error_response);
    }
}

std::string ExecutionService::placeOrderJson(const std::string& order_json) {
    try {
        models::Order order = parseOrderFromJson(order_json);
        ExecutionResponse response = placeOrder(order);
        return executionResponseToJson(response);
    } catch (const std::exception& e) {
        ExecutionResponse error_response{
            "", "", false, std::string("JSON parsing error: ") + e.what(), 
            std::chrono::system_clock::now(), "", "", ""
        };
        return executionResponseToJson(error_response);
    }
}

std::string ExecutionService::cancelOrderJson(const std::string& request_json) {
    try {
        nlohmann::json j = nlohmann::json::parse(request_json);
        std::string client_order_id = j["client_order_id"];
        CancelResponse response = cancelOrder(client_order_id);
        return cancelResponseToJson(response);
    } catch (const std::exception& e) {
        CancelResponse error_response{
            "", "", false, std::string("JSON parsing error: ") + e.what(), 
            std::chrono::system_clock::now()
        };
        return cancelResponseToJson(error_response);
    }
}

// std::string ExecutionService::cancelAllOrdersForExchangeId(const ExchangeId& exchangeId) {
//     try {
//         nlohmann::json j = nlohmann::json::parse(request_json);
//         std::string client_order_id = j["client_order_id"];
//         CancelResponse response = cancelOrder(client_order_id);
//         return cancelResponseToJson(response);
//     } catch (const std::exception& e) {
//         CancelResponse error_response{
//             "", "", false, std::string("JSON parsing error: ") + e.what(),
//             std::chrono::system_clock::now()
//         };
//         return cancelResponseToJson(error_response);
//     }
// }

std::string ExecutionService::getOrderStatusJson(const std::string& request_json) {
    try {
        nlohmann::json j = nlohmann::json::parse(request_json);
        std::string client_order_id = j["client_order_id"];
        OrderStatusResponse response = getOrderStatus(client_order_id);
        return orderStatusResponseToJson(response);
    } catch (const std::exception& e) {
        OrderStatusResponse error_response{
            nullptr, false, std::string("JSON parsing error: ") + e.what()
        };
        return orderStatusResponseToJson(error_response);
    }
}

std::string ExecutionService::getActiveOrdersJson(const std::string& request_json) {
    try {
        std::vector<models::Order> orders = getActiveOrders();
        return activeOrdersToJson(orders);
    } catch (const std::exception& e) {
        nlohmann::json error_response;
        error_response["orders"] = nlohmann::json::array();
        error_response["error"] = std::string("Error: ") + e.what();
        return error_response.dump();
    }
}

// Helper methods for JSON conversion
TradingSignal ExecutionService::parseTradingSignalFromJson(const std::string& json) {
    nlohmann::json j = nlohmann::json::parse(json);
    
    TradingSignal signal;
    signal.id = j["signal_id"];
    signal.setTradingPair(j["symbol"]["base"].get<std::string>() + "/" + j["symbol"]["quote"].get<std::string>());
    signal.setStrategyId(StrategyId(j["strategy_id"], j.value("strategy_version", "")));
    // signal.setStrategyVersion(j.value("strategy_version", ""));
    signal.setTargetSize(j["target_size"]);
    signal.setStrength(j["strength"]);
    signal.setTtl(std::chrono::milliseconds(j["signal_ttl_ms"]));

    // Convert direction
    std::string direction_str = j["direction"];
    if (direction_str == "BUY" || direction_str == "1") {
        signal.direction = core::Direction::BUY;
    } else if (direction_str == "SELL" || direction_str == "2") {
        signal.direction = core::Direction::SELL;
    } else {
        signal.direction = core::Direction::NEUTRAL;
    }
    
    // Convert signal metrics
    if (j.contains("signalMetrics")) {
        signal.signalMetrics = j["signalMetrics"];
    }
    
    return signal;
}

models::Order ExecutionService::parseOrderFromJson(const std::string& json) {
    nlohmann::json j = nlohmann::json::parse(json);
    
    models::Order order(j["client_order_id"]);
    
    // Set basic fields
    if (j.contains("symbol")) {
        core::Symbol trading_pair(j["symbol"]["base"].get<std::string>(), j["symbol"]["quote"].get<std::string>());
        order.setTradingPair(trading_pair);
    }
    
    // Convert side
    int side_int = j["side"];
    order.setSide(side_int == 1 ? core::Direction::BUY : core::Direction::SELL);
    
    // Convert type
    int type_int = j["type"];
    switch (type_int) {
        case 1: order.setType(models::OrderType::MARKET); break;
        case 2: order.setType(models::OrderType::LIMIT); break;
        case 3: order.setType(models::OrderType::STOP); break;
        case 4: order.setType(models::OrderType::STOP_LIMIT); break;
        case 5: order.setType(models::OrderType::ICEBERG); break;
        default: order.setType(models::OrderType::MARKET); break;
    }
    
    order.setQuantity(j["quantity"]);
    if (j.contains("price")) {
        order.setPrice(j["price"]);
    }
    if (j.contains("stop_price")) {
        order.setStopPrice(j["stop_price"]);
    }
    
    return order;
}

std::string ExecutionService::executionResponseToJson(const ExecutionResponse& response) {
    nlohmann::json j;
    j["client_order_id"] = response.client_order_id;
    j["exchange_order_id"] = response.exchange_order_id;
    j["success"] = response.success;
    j["error_message"] = response.error_message;
    j["order_status"] = response.order_status;
    j["symbol"] = response.symbol;
    j["exchange_id"] = response.exchange_id;
    
    // Convert timestamp
    auto duration = response.timestamp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
    j["timestamp"]["seconds"] = seconds.count();
    j["timestamp"]["nanos"] = nanos.count();

    
    return j.dump();
}

std::string ExecutionService::cancelResponseToJson(const CancelResponse& response) {
    nlohmann::json j;
    j["client_order_id"] = response.client_order_id;
    j["exchange_order_id"] = response.exchange_order_id;
    j["success"] = response.success;
    j["error_message"] = response.error_message;
    
    // Convert timestamp
    auto duration = response.timestamp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
    j["timestamp"]["seconds"] = seconds.count();
    j["timestamp"]["nanos"] = nanos.count();
    
    return j.dump();
}

std::string ExecutionService::orderStatusResponseToJson(const OrderStatusResponse& response) {
    nlohmann::json j;
    j["found"] = response.found;
    j["error_message"] = response.error_message;
    
    if (response.found && response.order) {
        const auto& order = *response.order;
        nlohmann::json order_json;
        order_json["order_id"] = order.getOrderId();
        order_json["client_order_id"] = order.getClientOrderId();
        order_json["trading_pair"] = order.getTradingPair().toString();
        order_json["side"] = (order.getSide() == core::Direction::BUY) ? "BUY" : "SELL";
        order_json["quantity"] = order.getQuantity();
        order_json["price"] = order.getPrice();
        order_json["filled_quantity"] = order.getFilledQuantity();
        order_json["state"] = static_cast<int>(order.getState());
        
        j["order"] = order_json;
    }
    
    return j.dump();
}

std::string ExecutionService::activeOrdersToJson(const std::vector<models::Order>& orders) {
    nlohmann::json j;
    j["orders"] = nlohmann::json::array();
    
    for (const auto& order : orders) {
        nlohmann::json order_json;
        order_json["order_id"] = order.getOrderId();
        order_json["client_order_id"] = order.getClientOrderId();
        order_json["trading_pair"] = order.getTradingPair().toString();
        order_json["side"] = (order.getSide() == core::Direction::BUY) ? "BUY" : "SELL";
        order_json["quantity"] = order.getQuantity();
        order_json["price"] = order.getPrice();
        order_json["filled_quantity"] = order.getFilledQuantity();
        order_json["state"] = static_cast<int>(order.getState());
        
        j["orders"].push_back(order_json);
    }
    
    return j.dump();
}

models::Order ExecutionService::createOrderFromSignal(const TradingSignal& signal) {
    try {
        // Validate input signal first
        if (signal.getTradingPair().isEmpty()) {
            throw std::invalid_argument("Signal trading pair is empty");
        }
        if (signal.getTargetSize() <= 0.0) {
            throw std::invalid_argument("Signal target size must be positive: " + std::to_string(signal.getTargetSize()));
        }
        if (signal.getStrength() < 0.0 || signal.getStrength() > 1.0) {
            throw std::invalid_argument("Signal strength must be between 0.0 and 1.0: " + std::to_string(signal.getStrength()));
        }

    // Create order generation parameters
    OrderGenerationParams params;
    params.tradingPair = signal.getTradingPair();
    params.side = signal.getDirection();
    // Prefer an explicit absolute quantity resolved earlier (signal.quantity). If not set,
    // fall back to the targetSize (which may be fractional) so downstream components
    // can decide how to interpret it.
    double sigQty = signal.getQuantity();
    if (sigQty > 0.0) params.quantity = sigQty; else params.quantity = signal.getTargetSize();
    params.strategy_id = signal.getStrategyId();
    params.signal_id = signal.getSignalId();
    params.signal_strength = signal.getStrength();
    params.target_size = signal.getTargetSize();
        params.signal_ttl = signal.getTtl();
        // Default to MARKET; OrderTypeSelector will convert to LIMIT and provide a price when appropriate
        params.type = models::OrderType::MARKET;
        params.time_in_force = models::TimeInForce::IOC;
        // Only populate params.price if the signal provided a positive price metric
        if (signal.signalMetrics.count("bidPrice") && signal.signalMetrics.at("bidPrice") > 0.0) {
            params.price = signal.signalMetrics.at("bidPrice");
        } else if (signal.price > 0.0) {
            params.price = signal.price;
        } else {
            params.price = 0.0; // no price provided by strategy/signal
        }
        // Check if order_generator_ is valid
        if (!order_generator_) {
            throw std::runtime_error("Order generator is null");
        }

        // Generate the order
        auto order = order_generator_->createOrder(params);
        if (!order) {
            throw std::runtime_error("Failed to create order from parameters");
        }

        // Propagate signal metadata into order extra params so callers can
        // communicate runtime flags (e.g., is_exit_signal) to the execution layer.
        try {
            for (const auto &kv : signal.metadata) {
                if (kv.first.empty()) continue;
                order->setExtraParam(kv.first, kv.second);
            }
        } catch (...) {
            // Non-fatal: metadata propagation is best-effort
        }
        
        // ADD DEBUG HERE:
        std::cout << "DEBUG: About to select exchange for pair: " << signal.getTradingPair() << std::endl;

        // Check if smart_order_router_ is valid
        if (!smart_order_router_) {
            throw std::runtime_error("Smart order router is null");
        }

        // Select the best exchange
        RoutingResult routing_result = smart_order_router_->selectExchange(order);

        // ADD MORE DEBUG:
        std::cout << "DEBUG: Routing result - ExchangeId: " << routing_result.exchangeId.toString() << std::endl;
        std::cout << "DEBUG: Routing result - Score: " << routing_result.score << std::endl;
        std::cout << "DEBUG: Routing result - Reason: " << routing_result.reason << std::endl;
        std::cout << "DEBUG: ExchangeId isValid(): " << routing_result.exchangeId.isValid() << std::endl;

        // if (!routing_result.exchangeId.isValid()) {
        //     throw std::runtime_error("No suitable exchange found: " + routing_result.reason);
        // }
        if (!routing_result.exchangeId.isValid()) { // Add the ! (NOT operator)
            throw std::runtime_error("No suitable exchange found: " + routing_result.reason);
        }
        // Set exchange ID
        order->setExchangeId(routing_result.exchangeId);

        // Check if order_type_selector_ is valid  
        if (!order_type_selector_) {
            throw std::runtime_error("Order type selector is null");
        }

        // Create MarketCondition from signal metrics
        MarketCondition market_condition;
        auto getMetric = [&](const std::string& key, double defaultValue = 0.0) -> double {
            auto it = signal.signalMetrics.find(key);
            return (it != signal.signalMetrics.end()) ? it->second : defaultValue;
        };
        
        market_condition.bidPrice = getMetric("bidPrice", 0.0);
        market_condition.askPrice = getMetric("askPrice", 0.0);
        double mid_price = (market_condition.bidPrice + market_condition.askPrice) / 2.0;
        double spreadRatio = 0.0;
        if (mid_price > 0.0) {
            spreadRatio = (market_condition.askPrice - market_condition.bidPrice) / mid_price;
        }
        market_condition.spread = spreadRatio;
        market_condition.volatility = getMetric("volatility", 0.02);
        market_condition.volume = getMetric("bidVolume", 100.0) + getMetric("askVolume", 100.0);
        market_condition.liquidityProfileScore = getMetric("liquidityProfileScore", 0.8);
        market_condition.market_impact_estimate = getMetric("market_impact_estimate", 0.005); //no such thing yet
        // if bidPrice is zero, set to price
        if (market_condition.bidPrice <= 0.0 || market_condition.askPrice <= 0.0) {
            // Try to obtain cached price first (non-blocking fast path), then force refresh if necessary.
            try {
                double cached_price = 0.0;
                if (exchangeGateway_) {
                    try {
                        auto fut_cached = exchangeGateway_->getCurrentPrice(order->getExchangeId(), order->getTradingPair(), false);
                        cached_price = fut_cached.get();
                    } catch (...) {
                        // ignore cached fetch errors
                        cached_price = 0.0;
                    }
                } else {
                    auto client = getExchangeClient(order->getExchangeId());
                    if (client) {
                        try {
                            auto fut_cached = client->getCurrentPrice(order->getTradingPair());
                            cached_price = fut_cached.get();
                        } catch (...) {
                            cached_price = 0.0;
                        }
                    }
                }

                if (cached_price > 0.0) {
                    // populate both sides conservatively
                    if (market_condition.bidPrice <= 0.0) market_condition.bidPrice = cached_price;
                    if (market_condition.askPrice <= 0.0) market_condition.askPrice = cached_price;
                } else {
                    // If no cached price, attempt a forced refresh (may block briefly)
                    double refreshed_price = 0.0;
                    if (exchangeGateway_) {
                        try {
                            auto fut = exchangeGateway_->getCurrentPrice(order->getExchangeId(), order->getTradingPair(), true);
                            refreshed_price = fut.get();
                        } catch (const std::exception& e) {
                            std::cerr << "createOrderFromSignal: failed to refresh price (gateway): " << e.what() << std::endl;
                        }
                    } else {
                        auto client = getExchangeClient(order->getExchangeId());
                        if (client) {
                            try {
                                auto fut = client->getCurrentPrice(order->getTradingPair());
                                refreshed_price = fut.get();
                            } catch (const std::exception& e) {
                                std::cerr << "createOrderFromSignal: failed to refresh price (client): " << e.what() << std::endl;
                            }
                        }
                    }

                    if (refreshed_price > 0.0) {
                        if (market_condition.bidPrice <= 0.0) market_condition.bidPrice = refreshed_price;
                        if (market_condition.askPrice <= 0.0) market_condition.askPrice = refreshed_price;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "createOrderFromSignal: price lookup failed: " << e.what() << std::endl;
            }

            // If still missing, fall back to params.price if available
            if (market_condition.bidPrice <= 0.0) {
                market_condition.bidPrice = params.price;
            }
            if (market_condition.askPrice <= 0.0) {
                market_condition.askPrice = params.price;
            }
        }
        // Enhanced debug logging for price issues
        std::cout << "Market Data for " << order->getTradingPair().toString() 
                  << ": Bid=" << market_condition.bidPrice 
                  << ", Ask=" << market_condition.askPrice
                  << ", Spread=" << market_condition.spread
                  << ", Vol=" << market_condition.volatility
                  << ", LiquidityScore=" << market_condition.liquidityProfileScore
                  << ", MarketImpact=" << market_condition.market_impact_estimate
                  << ", Side=" << (order->getSide() == models::OrderSide::BUY ? "BUY" : "SELL") << std::endl;
        
        if (market_condition.bidPrice <= 0.0 || market_condition.askPrice <= 0.0) {
            std::cerr << "WARNING: Invalid market data for " << order->getTradingPair().toString()
                     << ": Bid=" << market_condition.bidPrice
                     << ", Ask=" << market_condition.askPrice 
                     << " - this may cause pricing issues!" << std::endl;
        }
        // Select the most appropriate order type
        OrderTypeSelection selection = order_type_selector_->selectOrderType(order, market_condition);

        // Apply the selected order type and parameters
        // If selector chose a LIMIT but didn't provide a valid price, handle according to policy
        if (selection.type == models::OrderType::LIMIT && selection.price <= 0.0) {
            if (limit_order_handling_ == LimitOrderHandling::REJECT_IF_NO_PRICE) {
                std::ostringstream oss;
                oss << "OrderTypeSelector selected LIMIT but returned no price for " << order->getTradingPair().toString();
                std::string msg = oss.str();
                std::cerr << "createOrderFromSignal: " << msg << std::endl;
                throw std::invalid_argument(msg);
            }

            // Otherwise REVERT_TO_MARKET: attempt a forced price fetch (to handle cache misses),
            // then try to derive a fallback price.
            double fallback_price = 0.0;

            // Try to refresh market price from ExchangeGateway or direct client to handle cache misses
            try {
                double refreshed_price = 0.0;
                if (exchangeGateway_) {
                    try {
                        auto fut = exchangeGateway_->getCurrentPrice(order->getExchangeId(), order->getTradingPair(), true);
                        refreshed_price = fut.get();
                    } catch (const std::exception& e) {
                        std::cerr << "createOrderFromSignal: failed to refresh price via ExchangeGateway: " << e.what() << std::endl;
                    }
                } else {
                    auto client = getExchangeClient(order->getExchangeId());
                    if (client) {
                        try {
                            auto fut = client->getCurrentPrice(order->getTradingPair());
                            refreshed_price = fut.get();
                        } catch (const std::exception& e) {
                            std::cerr << "createOrderFromSignal: failed to refresh price via client: " << e.what() << std::endl;
                        }
                    }
                }

                if (refreshed_price > 0.0) {
                    // Use refreshed price to improve fallback accuracy
                    if (order->getSide() == models::OrderSide::BUY) {
                        market_condition.askPrice = refreshed_price;
                    } else {
                        market_condition.bidPrice = refreshed_price;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "createOrderFromSignal: exception while attempting price refresh: " << e.what() << std::endl;
            }

            // Now derive fallback price from possibly refreshed market condition
            if (order->getSide() == models::OrderSide::BUY) {
                // Prefer askPrice for buys (we will pay the ask or slightly above)
                if (market_condition.askPrice > 0.0) fallback_price = market_condition.askPrice * 1.0;
                else if (market_condition.bidPrice > 0.0) fallback_price = market_condition.bidPrice * 1.001;
            } else {
                // For sells prefer bidPrice (we'll sell at the bid or slightly below)
                if (market_condition.bidPrice > 0.0) fallback_price = market_condition.bidPrice * 1.0;
                else if (market_condition.askPrice > 0.0) fallback_price = market_condition.askPrice * 0.999;
            }

            if (fallback_price > 0.0) {
                std::cerr << "createOrderFromSignal: OrderTypeSelector returned LIMIT without price; derived fallback price=" << fallback_price
                          << " for " << order->getTradingPair().toString() << ". Proceeding as LIMIT." << std::endl;
                order->setType(models::OrderType::LIMIT);
                order->setPrice(fallback_price);
            } else {
                std::cerr << "createOrderFromSignal: OrderTypeSelector returned LIMIT without price and no market price available; reverting to MARKET for "
                          << order->getTradingPair().toString() << "." << std::endl;
                order->setType(models::OrderType::MARKET);
                order->setPrice(0.0);
            }           
            
            if (order->getSide() == models::OrderSide::BUY) {
                if (market_condition.askPrice > 0.0) fallback_price = market_condition.askPrice * 1.0;
                else if (market_condition.bidPrice > 0.0) fallback_price = market_condition.bidPrice * 1.001;
            } else {
                if (market_condition.bidPrice > 0.0) fallback_price = market_condition.bidPrice * 1.0;
                else if (market_condition.askPrice > 0.0) fallback_price = market_condition.askPrice * 0.999;
            }

            if (fallback_price > 0.0) {
                std::cerr << "createOrderFromSignal: OrderTypeSelector returned LIMIT without price; derived fallback price=" << fallback_price
                          << " for " << order->getTradingPair().toString() << ". Proceeding as LIMIT." << std::endl;
                order->setType(models::OrderType::LIMIT);
                order->setPrice(fallback_price);
            } else {
                std::cerr << "createOrderFromSignal: OrderTypeSelector returned LIMIT without price and no market price available; reverting to MARKET for "
                          << order->getTradingPair().toString() << "." << std::endl;
                order->setType(models::OrderType::MARKET);
                order->setPrice(0.0);
            }
        } else {
            order->setType(selection.type);
            if (selection.type == models::OrderType::LIMIT && selection.price > 0.0) {
                order->setPrice(selection.price);
            }
        }

        // CRITICAL SAFETY CHECK: Never allow SELL orders with price = 0
        if (order->getSide() == models::OrderSide::SELL && 
            order->getType() == models::OrderType::LIMIT && 
            order->getPrice() <= 0.0) {
            
            std::cerr << "CRITICAL ERROR: SELL order has price = 0! Converting to MARKET order for safety." << std::endl;
            std::cerr << "PREVENTED SELL ORDER WITH PRICE=0 for " << order->getTradingPair().toString() << std::endl;
            
            order->setType(models::OrderType::MARKET);
            order->setPrice(0.0);  // Market orders have 0 price by design
            order->setTimeInForce(models::TimeInForce::IOC);
        }

        order->setTimeInForce(selection.time_in_force);

        return *order;

    } catch (const std::exception& e) {
        std::cerr << "Exception in createOrderFromSignal: " << e.what() << std::endl;
        throw; // Re-throw the exception
    } catch (...) {
        std::cerr << "Unknown exception in createOrderFromSignal" << std::endl;
        throw std::runtime_error("Unknown exception in createOrderFromSignal");
    }
}

  bool ExecutionService::validateOrder(const models::Order& order) {
     // Basic validation checks
     if (order.getTradingPair().isEmpty()) {
         return false;
     }
     
     if (order.getQuantity() <= 0.0) {
         return false;
     }
     
     // Price validation for limit orders
     if (order.getType() == models::OrderType::LIMIT && order.getPrice() <= 0.0) {
         return false;
     }
     
     // More complex validation checks can be added here
     
     return true;
 }
 
ExecutionResponse ExecutionService::executeOrderWithAlgorithm(const models::Order& order) {
    // Simple market and limit orders can be placed directly
    if (order.getType() == models::OrderType::MARKET || 
        order.getType() == models::OrderType::LIMIT) {
        return placeOrder(order);
    }
    
    // Create algorithm parameters
    auto params = std::make_shared<DefaultAlgorithmParams>();
    params->exchangeId = order.getExchangeId();
    params->tradingPair = order.getTradingPair();
    params->side = order.getSide();
    params->order_type = order.getType();
    params->quantity = order.getQuantity();
    params->price = order.getPrice();
    
    // Determine algorithm type based on order type or explicit algorithm parameter
    std::string algorithmName = order.getExtraParam("algorithm");
    
    if (order.getType() == models::OrderType::ICEBERG) {
        params->algorithm_type = AlgorithmType::ICEBERG;
        // For icebergs, you might want to set visible quantity
        std::string visibleQty = order.getExtraParam("visible_quantity");
        if (!visibleQty.empty()) {
            params->visible_quantity = std::stod(visibleQty);
        }
    }
    else if (!algorithmName.empty()) {
        // Use explicitly specified algorithm
        if (algorithmName == "TWAP") {
            params->algorithm_type = AlgorithmType::TWAP;
            std::string duration = order.getExtraParam("duration");
            if (!duration.empty()) {
                params->duration_seconds = ::util::parseIntOr(duration, 0);
            }
        } 
        else if (algorithmName == "VWAP") {
            params->algorithm_type = AlgorithmType::VWAP;
            params->start_time = order.getExtraParam("start_time");
            params->end_time = order.getExtraParam("end_time");
        }
        else if (algorithmName == "POV") {
            params->algorithm_type = AlgorithmType::POV;
            std::string participation = order.getExtraParam("participation_rate");
            if (!participation.empty()) {
                params->participation_rate = std::stod(participation);
            }
        }
        else {
            // logger_->warn("Unsupported algorithm specified: {}", algorithmName);
            return ExecutionResponse{
                order.getClientOrderId(), "", false,
                "Unsupported algorithm type: " + algorithmName, 
                std::chrono::system_clock::now(),
                "", "",
                ""
            };
        }
    }
    else {
        // Default handling for other order types
        switch (order.getType()) {
            case models::OrderType::STOP:
            case models::OrderType::STOP_LIMIT:
            case models::OrderType::TRAILING_STOP:
            case models::OrderType::FILL_OR_KILL:
            case models::OrderType::IMMEDIATE_OR_CANCEL:
            case models::OrderType::POST_ONLY:
                // These can be placed directly as they're usually exchange features
                return placeOrder(order);
            default:
                // logger_->error("No algorithm defined for order type");
                return ExecutionResponse{
                    order.getClientOrderId(), "", false,
                    "No algorithm available for order type", 
                    std::chrono::system_clock::now(),
                    "", "", ""
                };
        }
    }
    
    // Create and execute the algorithm
    auto algorithm = execution_algorithm_factory_->createAlgorithm(params);
    if (!algorithm) {
        // logger_->error("Failed to create algorithm for order: {}", order.getClientOrderId());
        return ExecutionResponse{
            order.getClientOrderId(), "", false,
            "Failed to create execution algorithm", 
            std::chrono::system_clock::now(),
            "", "", ""
        };
    }
    
    // Execute using the algorithm
    return algorithm->execute(order, *this);
} 
 } // namespace execution
 } // namespace trading