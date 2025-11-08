#include "strategy/exit/optimal_exit_strategy.hpp"
#include "common/logger.hpp"

// Forward declaration from imbalance_detection_engine.hpp
void registerGlobalOptimalExitCallback(const trading::strategy::OptimalExitStrategy::ExitCallback& callback);

namespace trading {
namespace strategy {

// Global instance of OptimalExitStrategy for legacy compatibility
// This provides the same interface as the monolithic backup but in modular form
static OptimalExitStrategy g_optimalExit;

} // namespace strategy  
} // namespace trading

// Global function implementation matching declaration in imbalance_detection_engine.hpp
void registerGlobalOptimalExitCallback(const trading::strategy::OptimalExitStrategy::ExitCallback& callback) {
    LOG_INFO("Registering global optimal exit callback");
    trading::strategy::g_optimalExit.registerExitCallback(callback);
}