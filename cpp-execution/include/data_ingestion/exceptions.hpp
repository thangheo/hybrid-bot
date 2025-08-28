/**
 * @file exceptions.hpp
 * @brief Custom exception classes for data ingestion errors
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */
#ifndef DATA_INGESTION_EXCEPTIONS_HPP
#define DATA_INGESTION_EXCEPTIONS_HPP

#include <stdexcept>
#include <string>

namespace trading {
namespace data_ingestion {

// Base class for all data ingestion exceptions
class DataIngestionException : public std::runtime_error {
public:
    explicit DataIngestionException(const std::string& message)
        : std::runtime_error(message) {}
};

// Connection-related exceptions
class ConnectionException : public DataIngestionException {
public:
    explicit ConnectionException(const std::string& message)
        : DataIngestionException("Connection error: " + message) {}
};

// WebSocket-specific exceptions
class WebSocketException : public ConnectionException {
public:
    explicit WebSocketException(const std::string& message)
        : ConnectionException("WebSocket: " + message) {}
};

// REST API exceptions
class RestApiException : public ConnectionException {
public:
    explicit RestApiException(const std::string& message)
        : ConnectionException("REST API: " + message) {}
};

// Protocol parsing exceptions
class ProtocolException : public DataIngestionException {
public:
    explicit ProtocolException(const std::string& message)
        : DataIngestionException("Protocol error: " + message) {}
};

// Order book exceptions
class OrderBookException : public DataIngestionException {
public:
    explicit OrderBookException(const std::string& message)
        : DataIngestionException("Order book error: " + message) {}
};

// Data quality exceptions
class DataQualityException : public DataIngestionException {
public:
    explicit DataQualityException(const std::string& message)
        : DataIngestionException("Data quality error: " + message) {}
};

// Configuration exceptions
class ConfigException : public DataIngestionException {
public:
    explicit ConfigException(const std::string& message)
        : DataIngestionException("Configuration error: " + message) {}
};

} // namespace data_ingestion
} // namespace trading
#endif // DATA_INGESTION_EXCEPTIONS_HPP