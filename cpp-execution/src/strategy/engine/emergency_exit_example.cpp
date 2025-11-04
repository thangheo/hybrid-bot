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
            // PortfolioManager does not expose a getPositionsForSymbol API in the current interface;
            // fallback: log that position inspection is not supported and skip automatic liquidation.
            bool all_success = false;
            size_t positions_count = 0;
            LOG_WARN("PortfolioManager::getPositionsForSymbol(...) not available; skipping automatic position liquidation for {}", symbol);

            // Additional safety: Cancel all pending buy orders for this symbol
            try {
                // TODO: Implement order cancellation when PortfolioManager supports it
                // portfolio_manager_->cancelAllOrdersForSymbol(core::Symbol::fromString(symbol));
                LOG_INFO("Order cancellation skipped - method not available for {}", symbol);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to cancel orders for {}: {}", symbol, e.what());
                all_success = false;
            }

            if (all_success) {
                LOG_CRITICAL("✅ EMERGENCY EXIT COMPLETED: {}", symbol);
                
                // Optional: Send alert notification
                sendEmergencyAlert(symbol, reason, positions_count);
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
    // Pointer to the portfolio manager used to inspect/cancel positions/orders
    risk::PortfolioManager* portfolio_manager_ = nullptr;

    // Minimal alert helper (stub) — replace with real alerting integration as needed
    void sendEmergencyAlert(const std::string& symbol, const std::string& reason, size_t positions_count) {
        LOG_WARN("Emergency alert: {} - {} ({} positions)", symbol, reason, positions_count);
        // TODO: integrate with Slack/Discord/Email/SMS alerting system
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