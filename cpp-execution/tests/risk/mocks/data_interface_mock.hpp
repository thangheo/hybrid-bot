/**
 * Mock classes for testing
 * 
 * File: mocks/data_interface_mock.hpp
 * Created: 2025-04-30
 */

 #pragma once

 #include <gmock/gmock.h>
 #include "../../include/risk/interfaces/data_interface.hpp"
 
 namespace risk {
 namespace test {
 
 class MockDataInterface : public DataInterface {
 public:
     MOCK_METHOD(std::optional<MarketData>, getMarketData, (const std::string&), (const, override));
     MOCK_METHOD(std::vector<MarketData>, getLatestMarketData, (), (const, override));
     MOCK_METHOD(std::optional<MarketDepth>, getMarketDepth, (const std::string&, int), (const, override));
     MOCK_METHOD(std::vector<HistoricalPrice>, getHistoricalPrices, 
                 (const std::string&, Timestamp, Timestamp, const std::string&), (const, override));
 };
 
 } // namespace test
 } // namespace risk