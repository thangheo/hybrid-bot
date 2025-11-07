#include <iostream>
#include "strategy/engine/strategy_engine.hpp"

using namespace trading::strategy;

int main() {
    std::cout << "Testing volatility config loading..." << std::endl;
    
    // Create strategy engine instance
    StrategyEngine engine(StrategyId("test"));
    
    // Test loading config
    std::string configPath = "config/symbol_volatility_config_test.json";
    bool success = engine.loadSymbolVolatilityConfig(configPath);
    
    if (success) {
        std::cout << "✅ Config loaded successfully!" << std::endl;
        
        // Test retrieving thresholds
        std::cout << "Testing symbol threshold retrieval:" << std::endl;
        std::cout << "BTCUSDT threshold: " << engine.getSymbolVolatilityThreshold("BTCUSDT") << std::endl;
        std::cout << "ETHUSDT threshold: " << engine.getSymbolVolatilityThreshold("ETHUSDT") << std::endl;
        std::cout << "ADAUSDT threshold: " << engine.getSymbolVolatilityThreshold("ADAUSDT") << std::endl;
        std::cout << "UNKNOWN threshold: " << engine.getSymbolVolatilityThreshold("UNKNOWN") << std::endl;
    } else {
        std::cout << "❌ Config loading failed!" << std::endl;
        return 1;
    }
    
    return 0;
}