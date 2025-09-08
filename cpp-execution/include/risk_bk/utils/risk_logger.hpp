#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "core/time.hpp"
#include "core/types.hpp"
#include "risk_types.hpp"
#include <string>
#include <optional>
#include <memory>
#include <sstream>
#include <map>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

namespace trading {
namespace risk {

    /**
 * Enum for log levels
 */
enum class LogLevel : int {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

/**
 * Structure to represent a log entry
 */
struct LogEntry {
    core::Timestamp timestamp;
    LogLevel level;
    std::string component;
    std::string message;
    std::map<std::string, std::string> context;
};

/**
 * RiskLogger provides specialized logging for risk events.
 * It ensures audit trail for risk decisions and supports compliance requirements.
 */
class RiskLogger {
public:
    /**
     * Get singleton instance
     * @return Reference to the singleton instance
     */
    static RiskLogger& getInstance();
    
    /**
     * Initialize the logger
     * @param config Risk configuration containing logging settings
     * @return true if initialization was successful, false otherwise
     */
    bool initialize(const RiskConfig& config);
    
    /**
     * Log a debug message
     * @param component The component name
     * @param message The log message
     * @param context Optional context information
     */
    void debug(const std::string& component, const std::string& message, 
             const std::map<std::string, std::string>& context = {});
    
    /**
     * Log an info message
     * @param component The component name
     * @param message The log message
     * @param context Optional context information
     */
    void info(const std::string& component, const std::string& message, 
            const std::map<std::string, std::string>& context = {});
    
    /**
     * Log a warning message
     * @param component The component name
     * @param message The log message
     * @param context Optional context information
     */
    void warn(const std::string& component, const std::string& message, 
            const std::map<std::string, std::string>& context = {});
    
    /**
     * Log an error message
     * @param component The component name
     * @param message The log message
     * @param context Optional context information
     */
    void error(const std::string& component, const std::string& message, 
             const std::map<std::string, std::string>& context = {});
    
    /**
     * Log a fatal message
     * @param component The component name
     * @param message The log message
     * @param context Optional context information
     */
    void fatal(const std::string& component, const std::string& message, 
             const std::map<std::string, std::string>& context = {});
    
    /**
     * Set log level
     * @param level The new log level
     */
    void setLogLevel(LogLevel level);
    
    /**
     * Get current log level
     * @return The current log level
     */
    LogLevel getLogLevel() const;
    
    /**
     * Register a log callback
     * @param callback The function to call when logs are generated
     */
    void registerLogCallback(const std::function<void(const LogEntry&)>& callback);
    
    /**
     * Get recent log entries
     * @param count Maximum number of entries to retrieve
     * @param level Optional level to filter entries
     * @return Vector of log entries
     */
    std::vector<LogEntry> getRecentLogs(int count, const std::optional<LogLevel>& level = std::nullopt) const;
    
    /**
     * Save logs to file
     * @param filename The file path
     * @return true if saving was successful, false otherwise
     */
    bool saveLogsToFile(const std::string& filename) const;
    
    /**
     * Clear log buffer
     */
    void clearLogs();

private:
    // Private constructor for singleton
    RiskLogger();
    
    // Prevent copying and assignment
    RiskLogger(const RiskLogger&) = delete;
    RiskLogger& operator=(const RiskLogger&) = delete;
    
    // Logger configuration
    RiskLoggingConfig m_loggingConfig;
    std::atomic<LogLevel> m_logLevel;
    bool m_initialized;
    
    // Log storage
    static constexpr size_t MAX_LOG_BUFFER_SIZE = 10000;
    mutable std::mutex m_logsMutex;
    std::vector<LogEntry> m_logBuffer;
    
    // Callbacks
    mutable std::mutex m_callbackMutex;
    std::vector<std::function<void(const LogEntry&)>> m_logCallbacks;
    
    // File output
    std::unique_ptr<std::ostream> m_fileStream;
    mutable std::mutex m_fileStreamMutex;
    
    // Helper methods
    void logInternal(LogLevel level, const std::string& component, const std::string& message, 
                    const std::map<std::string, std::string>& context);
    std::string levelToString(LogLevel level) const;
    LogLevel stringToLevel(const std::string& levelStr) const;
    std::string formatLogEntry(const LogEntry& entry) const;
    void notifyLogCallbacks(const LogEntry& entry);
    void writeToFile(const LogEntry& entry);
    void limitBufferSize();
};

/**
 * Macro to get the logger instance
 */
#define RISK_LOGGER risk::RiskLogger::getInstance()

/**
 * Macros for logging
 */
#define RISK_LOG_DEBUG(component, message, ...) RISK_LOGGER.debug(component, message, __VA_ARGS__)
#define RISK_LOG_INFO(component, message, ...) RISK_LOGGER.info(component, message, __VA_ARGS__)
#define RISK_LOG_WARN(component, message, ...) RISK_LOGGER.warn(component, message, __VA_ARGS__)
#define RISK_LOG_ERROR(component, message, ...) RISK_LOGGER.error(component, message, __VA_ARGS__)
#define RISK_LOG_FATAL(component, message, ...) RISK_LOGGER.fatal(component, message, __VA_ARGS__)

} // namespace risk
} // namespace trading