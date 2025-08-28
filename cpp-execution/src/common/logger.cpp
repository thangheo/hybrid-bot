// src/common/logger.cpp
#include "common/logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace trading {
namespace common {

// Static instance initialization
std::shared_ptr<Logger> Logger::instance_;

// Concrete logger implementation
class ConsoleLogger : public Logger {
public:
    explicit ConsoleLogger(const std::string& appName, LogLevel level)
        : appName_(appName), level_(level) {}
    
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
    std::mutex mutex_;
    
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

// Factory method to create the logger
void Logger::initialize(const std::string& appName, LogLevel level) {
    if (!instance_) {
        instance_ = std::make_shared<ConsoleLogger>(appName, level);
    }
}

// Get the logger instance (with auto-initialization if needed)
std::shared_ptr<Logger> Logger::getLogger() {
    if (!instance_) {
        // Auto-initialize with default settings
        initialize("Application", LogLevel::INFO);
    }
    return instance_;
}

} // namespace common
} // namespace trading