#include <iostream>
#include "trading_system.hpp"

using namespace trading;
// Global demo instance for signal handling
std::unique_ptr<Trading_system> E8Instance;

void signalHandler(int sig) {
    std::cout << "\nShutdown signal received..." << std::endl;
    if (E8Instance) {
        E8Instance->shutdown();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    // Initialize logger early and allow overriding via environment variable
    // Use HYBRID_LOG_LEVEL (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF)
    auto parseLogLevel = [](const std::string& s) {
        if (s == "TRACE" || s == "trace") return trading::common::LogLevel::TRACE;
        if (s == "DEBUG" || s == "debug") return trading::common::LogLevel::DEBUG;
        if (s == "INFO" || s == "info") return trading::common::LogLevel::INFO;
        if (s == "WARN" || s == "warn" || s == "WARNING" || s == "warning") return trading::common::LogLevel::WARN;
        if (s == "ERROR" || s == "error") return trading::common::LogLevel::ERROR;
        if (s == "CRITICAL" || s == "critical") return trading::common::LogLevel::CRITICAL;
        if (s == "OFF" || s == "off") return trading::common::LogLevel::OFF;
        return trading::common::LogLevel::INFO;
    };

    const char* envLevel = std::getenv("HYBRID_LOG_LEVEL");
    if (envLevel) {
        trading::common::Logger::initialize("TradingApp", parseLogLevel(envLevel));
    } else {
        // Default logger level - set to INFO to reduce noisy DEBUG logs
        trading::common::Logger::initialize("TradingApp", trading::common::LogLevel::INFO);
    }
    try {
        std::cout << "🚀 STARTING FULL TRADING SYSTEM 🚀" << std::endl;
        std::cout << "=====================================================" << std::endl;        
        E8Instance = std::make_unique<Trading_system>();
        
        // Initialize system
        if (!E8Instance->initialize()) {
            std::cerr << "Failed to initialize trading system" << std::endl;
            return 1;
        }
        // PTCA: Set up periodic statistics reporting
      
        // Start system
        if (!E8Instance->start()) {
            std::cerr << "Failed to start trading system" << std::endl;
            return 1;
        }        
        // Run demo
        E8Instance->runIndefinitely();
        // Cleanup
        E8Instance->shutdown();
        E8Instance.reset();
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        if (E8Instance) {
            E8Instance->shutdown();
        }
        return 1;
    }
    
    return 0;
}