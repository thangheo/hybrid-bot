// include/common/logger.hpp
#pragma once

#include <string>
#include <memory>
#include <sstream>
#include <mutex>

namespace trading {
namespace common {

// Log levels
enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL,
    OFF
};

// Helper for converting any type to string
template <typename T>
std::string toString(const T& value) {
    if constexpr (std::is_convertible_v<T, std::string>) {
        return value;
    } else {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }
}

// Logger interface
class Logger {
public:
    static void initialize(const std::string& appName, LogLevel level);
    static std::shared_ptr<Logger> getLogger();
    virtual ~Logger() = default;
    
    // Simple string logging
    void trace(const std::string& message) {
        if (shouldLog(LogLevel::TRACE)) {
            logMessage(LogLevel::TRACE, message);
        }
    }
    
    void debug(const std::string& message) {
        if (shouldLog(LogLevel::DEBUG)) {
            logMessage(LogLevel::DEBUG, message);
        }
    }
    
    void info(const std::string& message) {
        if (shouldLog(LogLevel::INFO)) {
            logMessage(LogLevel::INFO, message);
        }
    }
    
    void warn(const std::string& message) {
        if (shouldLog(LogLevel::WARN)) {
            logMessage(LogLevel::WARN, message);
        }
    }
    
    void error(const std::string& message) {
        if (shouldLog(LogLevel::ERROR)) {
            logMessage(LogLevel::ERROR, message);
        }
    }
    
    void critical(const std::string& message) {
        if (shouldLog(LogLevel::CRITICAL)) {
            logMessage(LogLevel::CRITICAL, message);
        }
    }
    
    // Format-style logging with variable arguments
    template<typename Arg1, typename... Args>
    void trace(const std::string& format, const Arg1& arg1, const Args&... args) {
        if (shouldLog(LogLevel::TRACE)) {
            logMessage(LogLevel::TRACE, formatString(format, arg1, args...));
        }
    }
    
    template<typename Arg1, typename... Args>
    void debug(const std::string& format, const Arg1& arg1, const Args&... args) {
        if (shouldLog(LogLevel::DEBUG)) {
            logMessage(LogLevel::DEBUG, formatString(format, arg1, args...));
        }
    }
    
    template<typename Arg1, typename... Args>
    void info(const std::string& format, const Arg1& arg1, const Args&... args) {
        if (shouldLog(LogLevel::INFO)) {
            logMessage(LogLevel::INFO, formatString(format, arg1, args...));
        }
    }
    
    template<typename Arg1, typename... Args>
    void warn(const std::string& format, const Arg1& arg1, const Args&... args) {
        if (shouldLog(LogLevel::WARN)) {
            logMessage(LogLevel::WARN, formatString(format, arg1, args...));
        }
    }
    
    template<typename Arg1, typename... Args>
    void error(const std::string& format, const Arg1& arg1, const Args&... args) {
        if (shouldLog(LogLevel::ERROR)) {
            logMessage(LogLevel::ERROR, formatString(format, arg1, args...));
        }
    }
    
    template<typename Arg1, typename... Args>
    void critical(const std::string& format, const Arg1& arg1, const Args&... args) {
        if (shouldLog(LogLevel::CRITICAL)) {
            logMessage(LogLevel::CRITICAL, formatString(format, arg1, args...));
        }
    }
    
    // Set log level
    virtual void setLevel(LogLevel level) = 0;
    
    // Check if should log at level
    virtual bool shouldLog(LogLevel level) const = 0;
    
protected:
    Logger() = default;
    
    // Format string with arguments
    template<typename Arg1, typename... Args>
    std::string formatString(const std::string& format, const Arg1& arg1, const Args&... args) {
        std::ostringstream ss;
        size_t pos = 0;
        size_t next_pos = format.find("{}", pos);
        
        // Add everything before the first placeholder
        ss << format.substr(pos, next_pos - pos);
        
        // Add the first argument
        ss << toString(arg1);
        pos = next_pos + 2;  // Skip {}
        
        // Process remaining arguments
        formatStringHelper(ss, format, pos, args...);
        
        return ss.str();
    }
    
    // Helper for formatting strings - terminal case
    void formatStringHelper(std::ostringstream& ss, const std::string& format, size_t pos) {
        ss << format.substr(pos);
    }
    
    // Helper for formatting strings - recursive case
    template<typename Arg, typename... Args>
    void formatStringHelper(std::ostringstream& ss, const std::string& format, 
                          size_t pos, const Arg& arg, const Args&... args) {
        size_t next_pos = format.find("{}", pos);
        if (next_pos == std::string::npos) {
            ss << format.substr(pos);
            return;
        }
        
        ss << format.substr(pos, next_pos - pos);
        ss << toString(arg);
        pos = next_pos + 2;  // Skip {}
        
        formatStringHelper(ss, format, pos, args...);
    }
    
    // Log formatted message
    virtual void logMessage(LogLevel level, const std::string& message) = 0;
    
private:
    static std::shared_ptr<Logger> instance_;
};

// Convenience macros
#define LOG_TRACE(...) trading::common::Logger::getLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) trading::common::Logger::getLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...) trading::common::Logger::getLogger()->info(__VA_ARGS__)
#define LOG_WARN(...) trading::common::Logger::getLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) trading::common::Logger::getLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) trading::common::Logger::getLogger()->critical(__VA_ARGS__)

// Source location logging macros
#define LOG_TRACE_LOC(format, ...) trading::common::Logger::getLogger()->trace( \
                                    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + format, ##__VA_ARGS__)
#define LOG_DEBUG_LOC(format, ...) trading::common::Logger::getLogger()->debug( \
                                   std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + format, ##__VA_ARGS__)
#define LOG_INFO_LOC(format, ...) trading::common::Logger::getLogger()->info( \
                                  std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + format, ##__VA_ARGS__)
#define LOG_WARN_LOC(format, ...) trading::common::Logger::getLogger()->warn( \
                                  std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + format, ##__VA_ARGS__)
#define LOG_ERROR_LOC(format, ...) trading::common::Logger::getLogger()->error( \
                                   std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + format, ##__VA_ARGS__)
#define LOG_CRITICAL_LOC(format, ...) trading::common::Logger::getLogger()->critical( \
                                      std::string(__FILE__) + ":" + std::to_string(__LINE__) + " - " + format, ##__VA_ARGS__)

} // namespace common
} // namespace trading