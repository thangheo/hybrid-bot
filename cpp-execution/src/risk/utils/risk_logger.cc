/**
 * Implementation of specialized logging for risk events
 *
 * File: risk_logger.cpp
 * Created: 2025-04-29
 */

 #include "risk/utils/risk_logger.hpp"
 #include <chrono>
 #include <fstream>
 #include <iostream>
 #include <iomanip>
 #include <filesystem>
 #include <sstream>

 namespace trading{
 namespace risk {
 
 RiskLogger& RiskLogger::getInstance() {
     static RiskLogger instance;
     return instance;
 }
 
 RiskLogger::RiskLogger()
     : m_logLevel(LogLevel::INFO)
     , m_initialized(false) {
 }
 
 bool RiskLogger::initialize(const RiskConfig& config) {
     std::lock_guard<std::mutex> lock(m_logsMutex);
     
     if (m_initialized) {
         // Already initialized
         return true;
     }
     
     try {
         m_loggingConfig = config.getLoggingConfig();
         
         // Set log level from config
         m_logLevel = stringToLevel(m_loggingConfig.logLevel);
         
         // Initialize file logging if enabled
         if (m_loggingConfig.logToFile) {
             std::filesystem::path logDir(m_loggingConfig.logDirectory);
             
             // Create directory if it doesn't exist
             if (!std::filesystem::exists(logDir)) {
                 std::filesystem::create_directories(logDir);
             }
             
             // Create log file path
             std::string filename = m_loggingConfig.logFileName;
             if (filename.empty()) {
                 auto now = std::chrono::system_clock::now();
                 auto time = std::chrono::system_clock::to_time_t(now);
                 std::stringstream ss;
                 ss << "risk_" << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S") << ".log";
                 filename = ss.str();
             }
             
             std::filesystem::path logFile = logDir / filename;
             
             // Open log file
             auto fileStream = std::make_unique<std::ofstream>(logFile.string(), std::ios::out | std::ios::app);
             if (!fileStream->is_open()) {
                 std::cerr << "Failed to open log file: " << logFile.string() << std::endl;
                 return false;
             }
             
             m_fileStream = std::move(fileStream);
         }
         
         m_initialized = true;
         
         // Log initialization
         info("RiskLogger", "Risk logger initialized", {
             {"log_level", levelToString(m_logLevel)},
             {"log_to_file", m_loggingConfig.logToFile ? "true" : "false"},
             {"log_buffer_size", std::to_string(MAX_LOG_BUFFER_SIZE)}
         });
         
         return true;
     }
     catch (const std::exception& ex) {
         std::cerr << "Failed to initialize risk logger: " << ex.what() << std::endl;
         return false;
     }
 }
 
 void RiskLogger::debug(const std::string& component, const std::string& message,
                      const std::map<std::string, std::string>& context) {
     logInternal(LogLevel::DEBUG, component, message, context);
 }
 
 void RiskLogger::info(const std::string& component, const std::string& message,
                     const std::map<std::string, std::string>& context) {
     logInternal(LogLevel::INFO, component, message, context);
 }
 
 void RiskLogger::warn(const std::string& component, const std::string& message,
                     const std::map<std::string, std::string>& context) {
     logInternal(LogLevel::WARN, component, message, context);
 }
 
 void RiskLogger::error(const std::string& component, const std::string& message,
                      const std::map<std::string, std::string>& context) {
     logInternal(LogLevel::ERROR, component, message, context);
 }
 
 void RiskLogger::fatal(const std::string& component, const std::string& message,
                      const std::map<std::string, std::string>& context) {
     logInternal(LogLevel::FATAL, component, message, context);
 }
 
 void RiskLogger::setLogLevel(LogLevel level) {
     m_logLevel = level;
     
     info("RiskLogger", "Log level changed", {
         {"new_level", levelToString(level)}
     });
 }
 
 LogLevel RiskLogger::getLogLevel() const {
     return m_logLevel;
 }
 
 void RiskLogger::registerLogCallback(const std::function<void(const LogEntry&)>& callback) {
     std::lock_guard<std::mutex> lock(m_callbackMutex);
     m_logCallbacks.push_back(callback);
 }
 
 std::vector<LogEntry> RiskLogger::getRecentLogs(int count, const std::optional<LogLevel>& level) const {
     std::lock_guard<std::mutex> lock(m_logsMutex);
     
     std::vector<LogEntry> result;
     
     // Start from the most recent log entries
     auto it = m_logBuffer.rbegin();
     int processed = 0;
     
     while (it != m_logBuffer.rend() && processed < count) {
         if (!level || it->level == *level) {
             result.push_back(*it);
             processed++;
         }
         ++it;
     }
     
     return result;
 }
 
 bool RiskLogger::saveLogsToFile(const std::string& filename) const {
     std::lock_guard<std::mutex> lock(m_logsMutex);
     
     try {
         std::ofstream file(filename);
         if (!file.is_open()) {
             return false;
         }
         
         for (const auto& entry : m_logBuffer) {
             file << formatLogEntry(entry) << std::endl;
         }
         
         return true;
     }
     catch (const std::exception&) {
         return false;
     }
 }
 
 void RiskLogger::clearLogs() {
     std::lock_guard<std::mutex> lock(m_logsMutex);
     m_logBuffer.clear();
 }
 
 void RiskLogger::logInternal(LogLevel level, const std::string& component, const std::string& message,
                            const std::map<std::string, std::string>& context) {
     // Check if we should log at this level
     if (level < m_logLevel) {
         return;
     }
     
     // Create log entry
     LogEntry entry;
     entry.timestamp = std::chrono::system_clock::now();
     entry.level = level;
     entry.component = component;
     entry.message = message;
     entry.context = context;
     
     // Add to buffer
     {
         std::lock_guard<std::mutex> lock(m_logsMutex);
         m_logBuffer.push_back(entry);
         limitBufferSize();
     }
     
     // Write to file if enabled
     if (m_loggingConfig.logToFile && m_fileStream) {
         writeToFile(entry);
     }
     
     // Console output if enabled
     if (m_loggingConfig.logToConsole) {
         std::cout << formatLogEntry(entry) << std::endl;
     }
     
     // Notify callbacks
     notifyLogCallbacks(entry);
 }
 
 std::string RiskLogger::levelToString(LogLevel level) const {
     switch (level) {
         case LogLevel::DEBUG: return "DEBUG";
         case LogLevel::INFO:  return "INFO";
         case LogLevel::WARN:  return "WARN";
         case LogLevel::ERROR: return "ERROR";
         case LogLevel::FATAL: return "FATAL";
         default:              return "UNKNOWN";
     }
 }
 
 LogLevel RiskLogger::stringToLevel(const std::string& levelStr) const {
     if (levelStr == "DEBUG") return LogLevel::DEBUG;
     if (levelStr == "INFO")  return LogLevel::INFO;
     if (levelStr == "WARN")  return LogLevel::WARN;
     if (levelStr == "ERROR") return LogLevel::ERROR;
     if (levelStr == "FATAL") return LogLevel::FATAL;
     
     // Default level
     return LogLevel::INFO;
 }
 
 std::string RiskLogger::formatLogEntry(const LogEntry& entry) const {
     std::stringstream ss;
     
     // Format timestamp
     auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
     auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
         entry.timestamp.time_since_epoch() % std::chrono::seconds(1)).count();
     
     ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "." 
        << std::setfill('0') << std::setw(3) << ms << " "
        << "[" << levelToString(entry.level) << "] "
        << "[" << entry.component << "] "
        << entry.message;
     
     // Add context if any
     if (!entry.context.empty()) {
         ss << " {";
         bool first = true;
         for (const auto& [key, value] : entry.context) {
             if (!first) {
                 ss << ", ";
             }
             ss << key << "=" << value;
             first = false;
         }
         ss << "}";
     }
     
     return ss.str();
 }
 
 void RiskLogger::notifyLogCallbacks(const LogEntry& entry) {
     std::lock_guard<std::mutex> lock(m_callbackMutex);
     
     for (const auto& callback : m_logCallbacks) {
         try {
             callback(entry);
         }
         catch (const std::exception&) {
             // Ignore callback exceptions
         }
     }
 }
 
 void RiskLogger::writeToFile(const LogEntry& entry) {
     std::lock_guard<std::mutex> lock(m_fileStreamMutex);
     
     if (m_fileStream) {
         try {
             (*m_fileStream) << formatLogEntry(entry) << std::endl;
             m_fileStream->flush();
         }
         catch (const std::exception&) {
             // Handle file write errors
             std::cerr << "Failed to write to log file" << std::endl;
         }
     }
 }
 
 void RiskLogger::limitBufferSize() {
     if (m_logBuffer.size() > MAX_LOG_BUFFER_SIZE) {
         m_logBuffer.erase(m_logBuffer.begin(), m_logBuffer.begin() + (m_logBuffer.size() - MAX_LOG_BUFFER_SIZE));
     }
 }
 
 } // namespace risk
 } // namespace trading