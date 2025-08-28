/**
 * @file adapter_interface.hpp
 * @brief Abstract interface for protocol adapter implementations
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */

#ifndef ADAPTER_INTERFACE_HPP
#define ADAPTER_INTERFACE_HPP

#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"

using json = nlohmann::json;
using namespace trading::data_ingestion;

// TODO: Implement Abstract interface for protocol adapter implementations
// Interface for all protocol adapters
class AdapterInterface {
    public:
        virtual ~AdapterInterface() = default;
        
        // Convert raw exchange data to internal format
        virtual OrderBookUpdate convertToStandardFormat(const ExchangeData& data) = 0;
        
        // Identify the exchange and data type
        virtual bool canHandle(const ExchangeData& data) = 0;
        
        // Get adapter name/identifier
        virtual std::string getName() const = 0;
    };
    
    // Specific implementations for each exchange
    // class BinanceAdapter : public AdapterInterface {
    // public:
    //     OrderBookUpdate convertToStandardFormat(const ExchangeData& data) override;
    //     bool canHandle(const ExchangeData& data) override;
    //     std::string getName() const override { return "Binance"; }
    // private:
    //     // Exchange-specific parsing logic
    //     BidAskPair parsePriceLevel(const json& level);
    //     Timestamp parseTimestamp(const json& data);
    // };
    
    // Similar adapters for other exchanges
    // class CoinbaseAdapter : public AdapterInterface;
    // class FTXAdapter : public AdapterInterface;
#endif // ADAPTER_INTERFACE_HPP
