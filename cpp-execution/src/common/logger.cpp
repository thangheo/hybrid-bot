// src/common/logger.cpp
#include "common/logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace trading {
namespace common {

// Static member initialization
std::once_flag Logger::initialized_;
std::atomic<bool> Logger::shutdown_called_{false};

// Concrete logger implementation
class ConsoleLogger : public Logger {
public:
    explicit ConsoleLogger(const std::string& appName, LogLevel level)
        : appName_(appName), level_(level) {}
    
    ~ConsoleLogger() override {
        // Ensure any pending output is flushed
        std::cout.flush();
    }
    
    void setLevel(LogLevel level) override {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }
    
    bool shouldLog(LogLevel level) const override {
        return level >= level_;
    }
    
protected:
    void logMessage(LogLevel level, const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        // Format time as string
        char timeBuffer[25];
        std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
        
        std::stringstream ss;
        ss << "[" << appName_ << "] "
           << "[" << timeBuffer << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
           << "[" << levelToString(level) << "] "
           << message;
           
        std::cout << ss.str() << std::endl;
    }
    
private:
    std::string appName_;
    LogLevel level_;
    mutable std::mutex mutex_;
    
    std::string levelToString(LogLevel level) const {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::CRITICAL: return "CRITICAL";
            case LogLevel::OFF: return "OFF";
            default: return "UNKNOWN";
        }
    }
};

// Thread-safe singleton using function-local static
std::shared_ptr<Logger>& Logger::getInstance() {
    static std::shared_ptr<Logger> instance;
    return instance;
}

// Factory method to create the logger
void Logger::initialize(const std::string& appName, LogLevel level) {
    std::call_once(initialized_, [&appName, level]() {
        if (!shutdown_called_.load()) {
            getInstance() = std::make_shared<ConsoleLogger>(appName, level);
        }
    });
}

// Get the logger instance (with auto-initialization if needed)
std::shared_ptr<Logger> Logger::getLogger() {
    // Check if shutdown was called
    if (shutdown_called_.load()) {
        return nullptr;
    }
    
    auto& instance = getInstance();
    if (!instance) {
        // Auto-initialize with default settings if not explicitly initialized
        initialize("Application", LogLevel::INFO);
    }
    
    // Return a copy to prevent use-after-free during shutdown
    return instance;
}

// Explicit shutdown method to clean up before static destruction
void Logger::shutdown() {
    shutdown_called_.store(true);
    
    // Clear the instance
    auto& instance = getInstance();
    if (instance) {
        instance.reset();
    }
}

} // namespace common
} // namespace trading