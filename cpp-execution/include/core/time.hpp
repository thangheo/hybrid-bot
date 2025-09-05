/**
 * @file time.hpp
 * @brief Core time-related types and utilities
 *
 * This file defines common time types used across all components
 * of the trading system.
 *
 * @author Trading System Team
 * @date May 12, 2025
 */
#pragma once
#include <chrono>
#include <string>
#include <iomanip>
#include <sstream>
#include <cstdint>  // Add this include

namespace trading {
namespace core {

/**
 * Standard timestamp type used throughout the system
 */
using Timestamp = std::chrono::time_point<std::chrono::system_clock>;

/**
 * Standard duration type used throughout the system
 */
using Duration = std::chrono::system_clock::duration;

/**
 * Convert Timestamp to milliseconds since epoch
 */
inline uint64_t timestampToMillis(const Timestamp& timestamp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
}

/**
 * Convert Timestamp to nanoseconds since epoch  
 */
inline uint64_t timestampToNanos(const Timestamp& timestamp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        timestamp.time_since_epoch()).count();
}

/**
 * Convert milliseconds since epoch to Timestamp
 */
inline Timestamp timestampFromMillis(uint64_t millis) {
    return Timestamp(std::chrono::milliseconds(millis));
}

/**
 * Convert nanoseconds since epoch to Timestamp
 */
inline Timestamp timestampFromNanos(uint64_t nanos) {
    return Timestamp(std::chrono::nanoseconds(nanos));
}

/**
 * Converts a timestamp to ISO 8601 string format
 *
 * @param time The timestamp to convert
 * @return String representation in ISO 8601 format (YYYY-MM-DDTHH:MM:SS.mmmZ)
 */
inline std::string timestampToIsoString(const Timestamp& time) {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        time - std::chrono::system_clock::from_time_t(time_t)).count();
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms << 'Z';
    return ss.str();
}

/**
 * Parses an ISO 8601 string into a timestamp
 *
 * @param iso_string String in ISO 8601 format (YYYY-MM-DDTHH:MM:SS.mmmZ)
 * @return Corresponding timestamp
 */
inline Timestamp timestampFromIsoString(const std::string& iso_string) {
    std::tm tm = {};
    std::stringstream ss(iso_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    auto time = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    
    // Extract milliseconds if present
    if (ss.peek() == '.') {
        ss.ignore();
        int ms;
        ss >> ms;
        time += std::chrono::milliseconds(ms);
    }
    
    return time;
}

/**
 * Get current timestamp
 *
 * @return Current timestamp
 */
inline Timestamp now() {
    return std::chrono::system_clock::now();
}

} // namespace core
} // namespace trading