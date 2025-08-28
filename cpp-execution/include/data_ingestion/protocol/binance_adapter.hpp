/**
 * @file binance_adapter.hpp
 * @brief Binance-specific message format conversion adapter
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * Converts Binance-specific order book message formats to the system's standard format.
 * 
 * Created on: April 30, 2025
 */
#pragma once

 #include "data_ingestion/protocol/adapter_interface.hpp"
 #include "core/market_types.hpp"
 
namespace trading {
namespace data_ingestion {


class BinanceAdapter : public AdapterInterface {
public:
    // ✅ Add default constructor
    BinanceAdapter() = default;
    
    // ✅ Add virtual destructor  
    virtual ~BinanceAdapter() = default;
    
    /**
     * @brief Checks if the data is from Binance and can be handled by this adapter
     */
    bool canHandle(const ExchangeData& data) override;
    
    /**
     * @brief Converts Binance-specific order book data to standard format
     */
    OrderBookUpdate convertToStandardFormat(const ExchangeData& data) override;
    
    /**
     * @brief Get adapter name
     */
    std::string getName() const override {
        return "BinanceAdapter";
    }
};
} // namespace data_ingestion
} // namespace trading