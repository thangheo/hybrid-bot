/* * @file order_imbalance_system/include/strategy/pattern/analyzer.hpp
 * @file analyzer.hpp
 * @brief Base interface for all analyzer components in the strategy engine
 * 
 * @details
 * Defines the common interface for all analyzer components in the strategy system.
 * Analyzers are responsible for processing order book data and extracting 
 * meaningful metrics and patterns.
 * 
 * Created on: May 5, 2023
 * Author: Trading System Team
 */
#pragma once

#include "data_ingestion/orderbook/order_book.hpp"
#include "strategy/types.hpp"
#include "analyzer_types.hpp"
#include <string>
#include <memory>

namespace trading {
namespace strategy {

/**
 * @class Analyzer
 * @brief Base class for all analyzer components
 */
class Analyzer {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~Analyzer() = default;
    
    /**
     * @brief Initialize the analyzer with configuration parameters
     * @param config Configuration parameters for the analyzer
     * @return True if initialization successful, false otherwise
     */
    virtual bool initialize(const AnalyzerConfig& config) = 0;
    
    /**
     * @brief Process a new order book update
     * @param orderBook The order book to analyze
     * @return True if processing was successful, false otherwise
     */
    virtual bool processOrderBook(const data_ingestion::OrderBook& orderBook) = 0;
    
    /**
     * @brief Reset analyzer state
     */
    virtual void reset() = 0;
    
    /**
     * @brief Check if analyzer has enough data to provide reliable results
     * @return True if analyzer has processed enough data, false otherwise
     */
    virtual bool hasStableMetrics() const = 0;
    
    /**
     * @brief Get analyzer name
     * @return Analyzer name
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Get analyzer type
     * @return Analyzer type enumeration
     */
    virtual AnalyzerType getType() const = 0;
    
    /**
     * @brief Get current status of the analyzer
     * @return Status information as a JSON string
     */
    virtual std::string getStatus() const = 0;
};

// Convenience type definitions
using AnalyzerPtr = std::shared_ptr<Analyzer>;
using AnalyzerConstPtr = std::shared_ptr<const Analyzer>;

} // namespace strategy
} // namespace trading