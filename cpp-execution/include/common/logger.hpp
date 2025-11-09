// include/common/logger.hpp
#pragma once

#include <string>
#include <memory>
#include <sstream>
#include <mutex>
#include <regex>
#include <iomanip>
#include <vector>
#include <atomic>
#include <type_traits>

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

// Simple format implementation that supports both {} and {:.3f} style formatting
class SimpleFormatter {
public:
    template<typename... Args>
    static std::string format(const std::string& format_str, const Args&... args) {
        std::string result = format_str;
        std::vector<std::string> formatted_args;
        
        // Process each argument
        processArgs(formatted_args, format_str, args...);
        
        // Replace placeholders
        size_t arg_index = 0;
        size_t search_pos = 0;
        
        while (search_pos < result.length() && arg_index < formatted_args.size()) {
            size_t open_brace = result.find('{', search_pos);
            if (open_brace == std::string::npos) break;
            
            size_t close_brace = result.find('}', open_brace);
            if (close_brace == std::string::npos) break;
            
            // Replace the placeholder with the formatted argument
            result.replace(open_brace, close_brace - open_brace + 1, formatted_args[arg_index]);
            search_pos = open_brace + formatted_args[arg_index].length();
            arg_index++;
        }
        
        return result;
    }

private:
    template<typename T>
    static std::string formatSingleArg(const T& arg, const std::string& spec) {
        if constexpr (std::is_floating_point_v<T>) {
            return formatFloat(arg, spec);
        } else if constexpr (std::is_integral_v<T>) {
            return formatInteger(arg, spec);
        } else if constexpr (std::is_convertible_v<T, std::string>) {
            return std::string(arg);
        } else {
            std::ostringstream ss;
            ss << arg;
            return ss.str();
        }
    }
    
    template<typename T>
    static std::string formatFloat(const T& value, const std::string& spec) {
        std::ostringstream ss;
        
        // Parse format specifier (e.g., ":.3f", ":.2e")
        std::regex format_regex(R"(:?\.?(\d+)?([fFeEgG])?)");
        std::smatch matches;
        
        if (std::regex_search(spec, matches, format_regex)) {
            if (matches[1].matched) {
                int precision = std::stoi(matches[1].str());
                ss << std::setprecision(precision);
            }
            
            if (matches[2].matched) {
                char format_type = matches[2].str()[0];
                switch (format_type) {
                    case 'f': case 'F':
                        ss << std::fixed;
                        break;
                    case 'e': case 'E':
                        ss << std::scientific;
                        break;
                    case 'g': case 'G':
                        ss.unsetf(std::ios::floatfield);
                        break;
                }
            } else {
                ss << std::fixed;  // Default to fixed for precision
            }
        }
        
        ss << value;
        return ss.str();
    }
    
    template<typename T>
    static std::string formatInteger(const T& value, const std::string& spec) {
        std::ostringstream ss;
        
        if (spec.find('x') != std::string::npos) {
            ss << std::hex << value;
        } else if (spec.find('o') != std::string::npos) {
            ss << std::oct << value;
        } else {
            ss << value;
        }
        
        return ss.str();
    }
    
    // Extract format specifiers and format arguments accordingly
    template<typename T, typename... Rest>
    static void processArgs(std::vector<std::string>& formatted_args, 
                           const std::string& format_str, 
                           const T& arg, const Rest&... rest) {
        
        size_t placeholder_index = formatted_args.size();
        std::string format_spec = extractFormatSpec(format_str, placeholder_index);
        
        formatted_args.push_back(formatSingleArg(arg, format_spec));
        
        if constexpr (sizeof...(rest) > 0) {
            processArgs(formatted_args, format_str, rest...);
        }
    }
    
    static std::string extractFormatSpec(const std::string& format_str, size_t placeholder_index) {
        size_t current_placeholder = 0;
        size_t pos = 0;
        
        while (pos < format_str.length()) {
            size_t open_brace = format_str.find('{', pos);
            if (open_brace == std::string::npos) break;
            
            size_t close_brace = format_str.find('}', open_brace);
            if (close_brace == std::string::npos) break;
            
            if (current_placeholder == placeholder_index) {
                return format_str.substr(open_brace + 1, close_brace - open_brace - 1);
            }
            
            current_placeholder++;
            pos = close_brace + 1;
        }
        
        return "";  // No format specifier found
    }
};

// Logger interface
class Logger {
public:
    static void initialize(const std::string& appName, LogLevel level);
    static std::shared_ptr<Logger> getLogger();
    static void shutdown(); // Explicit shutdown to prevent use-after-free
    virtual ~Logger() = default;
    
    // Simple string logging (backward compatible)
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
    
    // Format-style logging with variable arguments (supports both {} and {:.3f} styles)
    template<typename... Args>
    void trace(const std::string& format, const Args&... args) {
        if (shouldLog(LogLevel::TRACE)) {
            logMessage(LogLevel::TRACE, SimpleFormatter::format(format, args...));
        }
    }
    
    template<typename... Args>
    void debug(const std::string& format, const Args&... args) {
        if (shouldLog(LogLevel::DEBUG)) {
            logMessage(LogLevel::DEBUG, SimpleFormatter::format(format, args...));
        }
    }
    
    template<typename... Args>
    void info(const std::string& format, const Args&... args) {
        if (shouldLog(LogLevel::INFO)) {
            logMessage(LogLevel::INFO, SimpleFormatter::format(format, args...));
        }
    }
    
    template<typename... Args>
    void warn(const std::string& format, const Args&... args) {
        if (shouldLog(LogLevel::WARN)) {
            logMessage(LogLevel::WARN, SimpleFormatter::format(format, args...));
        }
    }
    
    template<typename... Args>
    void error(const std::string& format, const Args&... args) {
        if (shouldLog(LogLevel::ERROR)) {
            logMessage(LogLevel::ERROR, SimpleFormatter::format(format, args...));
        }
    }
    
    template<typename... Args>
    void critical(const std::string& format, const Args&... args) {
        if (shouldLog(LogLevel::CRITICAL)) {
            logMessage(LogLevel::CRITICAL, SimpleFormatter::format(format, args...));
        }
    }
    
    // Set log level
    virtual void setLevel(LogLevel level) = 0;
    
    // Check if should log at level
    virtual bool shouldLog(LogLevel level) const = 0;
    
protected:
    Logger() = default;
    
    // Log formatted message
    virtual void logMessage(LogLevel level, const std::string& message) = 0;
    
private:
    // Thread-safe singleton implementation
    static std::shared_ptr<Logger>& getInstance();
    
    // Use function-local statics to avoid ODR violations
    static std::once_flag& getInitializedFlag() {
        static std::once_flag initialized_;
        return initialized_;
    }
    
    static std::atomic<bool>& getShutdownFlag() {
        static std::atomic<bool> shutdown_called_{false};
        return shutdown_called_;
    }
};

// Simple logging macros - fully backward compatible
#define LOG_TRACE(...) do { \
    auto logger = trading::common::Logger::getLogger(); \
    if (logger) { \
        logger->trace(__VA_ARGS__); \
    } \
} while(0)

#define LOG_DEBUG(...) do { \
    auto logger = trading::common::Logger::getLogger(); \
    if (logger) { \
        logger->debug(__VA_ARGS__); \
    } \
} while(0)

#define LOG_INFO(...) do { \
    auto logger = trading::common::Logger::getLogger(); \
    if (logger) { \
        logger->info(__VA_ARGS__); \
    } \
} while(0)

#define LOG_WARN(...) do { \
    auto logger = trading::common::Logger::getLogger(); \
    if (logger) { \
        logger->warn(__VA_ARGS__); \
    } \
} while(0)

#define LOG_ERROR(...) do { \
    auto logger = trading::common::Logger::getLogger(); \
    if (logger) { \
        logger->error(__VA_ARGS__); \
    } \
} while(0)

#define LOG_CRITICAL(...) do { \
    auto logger = trading::common::Logger::getLogger(); \
    if (logger) { \
        logger->critical(__VA_ARGS__); \
    } \
} while(0)

} // namespace common
} // namespace trading