// emergency_exit_example.cpp - Example of how to register emergency exit callback

#include "strategy/engine/strategy_engine.hpp"
#include "risk/portfolio/portfolio_manager.hpp"
#include "common/logger.hpp"

namespace trading::strategy {

/**
 * Example emergency exit handler that integrates with PortfolioManager
 * This would typically be registered in your main trading application
 */
class EmergencyExitHandler {
public:
    EmergencyExitHandler(risk::PortfolioManager* portfolio_manager) 
        : portfolio_manager_(portfolio_manager) {}

    /**
     * Emergency exit callback - liquidates all positions for the given symbol
     * @param symbol Symbol to liquidate (e.g., "BTC/USDT")
     * @param reason Reason for emergency exit
     * @return true if liquidation was successful
     */
    bool handleEmergencyExit(const std::string& symbol, const std::string& reason) {
        if (!portfolio_manager_) {
            LOG_ERROR("EmergencyExit: No portfolio manager available for {}", symbol);
            return false;
        }

        try {
            LOG_CRITICAL("🚨 EMERGENCY EXIT TRIGGERED: {} - Reason: {}", symbol, reason);
            
            // Get all positions for this symbol across all exchanges
            auto positions = portfolio_manager_->getPositionsForSymbol(core::Symbol::fromString(symbol));
            
            bool all_success = true;
            for (const auto& pos : positions) {
                if (pos.getQuantity() != 0.0) {
                    LOG_WARN("LIQUIDATING POSITION: {} {} qty={:.8f}", 
                             pos.getExchangeId().toString(), symbol, pos.getQuantity());
                    
                    // Send market sell order to close position immediately
                    try {
                        auto order_id = portfolio_manager_->liquidatePosition(
                            pos.getExchangeId(), 
                            pos.getTradingPair()
                        );
                        
                        if (order_id.has_value()) {
                            LOG_INFO("Emergency liquidation order sent: {} for {}", 
                                     order_id.value(), symbol);
                        } else {
                            LOG_ERROR("Failed to send emergency liquidation order for {}", symbol);
                            all_success = false;
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Exception during emergency liquidation of {}: {}", symbol, e.what());
                        all_success = false;
                    }
                }
            }

            // Additional safety: Cancel all pending buy orders for this symbol
            try {
                portfolio_manager_->cancelAllOrdersForSymbol(core::Symbol::fromString(symbol));
                LOG_INFO("Cancelled all pending orders for {}", symbol);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to cancel orders for {}: {}", symbol, e.what());
                all_success = false;
            }

            if (all_success) {
                LOG_CRITICAL("✅ EMERGENCY EXIT COMPLETED: {}", symbol);
                
                // Optional: Send alert notification
                sendEmergencyAlert(symbol, reason, positions.size());
                return true;
            } else {
                LOG_ERROR("❌ EMERGENCY EXIT PARTIAL FAILURE: {}", symbol);
                return false;
            }

        } catch (const std::exception& e) {
            LOG_ERROR("Exception in emergency exit handler for {}: {}", symbol, e.what());
            return false;
        }
    }

private:
    risk::PortfolioManager* portfolio_manager_;
    
    void sendEmergencyAlert(const std::string& symbol, const std::string& reason, size_t positions_closed) {
        // Optional: Integrate with alerting system
        LOG_CRITICAL("📱 ALERT: Emergency exit executed for {} (reason: {}, positions: {})", 
                     symbol, reason, positions_closed);
        
        // TODO: Send to Slack/Discord/Email/SMS
        // alerting_system_->sendCriticalAlert(
        //     fmt::format("🚨 EMERGENCY EXIT: {} - {}", symbol, reason)
        // );
    }
};

/**
 * Example setup function showing how to register the emergency exit callback
 */
void setupEmergencyExitHandling(StrategyEngine& strategy_engine, risk::PortfolioManager& portfolio_manager) {
    auto emergency_handler = std::make_shared<EmergencyExitHandler>(&portfolio_manager);
    
    // Register the emergency exit callback
    strategy_engine.registerEmergencyExitCallback(
        [emergency_handler](const std::string& symbol, const std::string& reason) -> bool {
            return emergency_handler->handleEmergencyExit(symbol, reason);
        }
    );
    
    LOG_INFO("Emergency exit handler registered successfully");
}

} // namespace trading::strategy