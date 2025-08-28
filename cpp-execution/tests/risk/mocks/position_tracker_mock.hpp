/**
 * Mock classes for testing
 * 
 * File: mocks/position_tracker_mock.hpp
 * Created: 2025-04-30
 */

 #pragma once

 #include <gmock/gmock.h>
 #include "../../include/risk/portfolio/position_tracker.hpp"
 
 namespace risk {
 namespace test {
 
 class MockPositionTracker : public PositionTracker {
 public:
     MockPositionTracker() : PositionTracker(RiskConfig(), nullptr) {}
     
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(void, processExecutionReport, (const ExecutionReport&), (override));
     MOCK_METHOD(void, updateMarketData, (const MarketData&), (override));
     MOCK_METHOD(std::vector<Position>, getAllPositions, (), (const, override));
     MOCK_METHOD(std::vector<Position>, getExchangePositions, (const ExchangeId&), (const, override));
     MOCK_METHOD(std::vector<Position>, getPositionsByTradingPair, (const TradingPair&), (const, override));
     MOCK_METHOD(std::optional<Position>, getPosition, (const ExchangeId&, const TradingPair&), (const, override));
     MOCK_METHOD(std::vector<Position>, getStrategyPositions, (const StrategyId&), (const, override));
     MOCK_METHOD(bool, updatePosition, 
                 (const ExchangeId&, const TradingPair&, double, std::optional<double>), (override));
     MOCK_METHOD(double, calculatePortfolioValue, (), (const, override));
     MOCK_METHOD(double, calculateTotalExposure, (), (const, override));
     MOCK_METHOD(void, registerPositionUpdateCallback, (const PositionUpdateCallback&), (override));
     MOCK_METHOD(void, clearPositions, (), (override));
 };
 
 } // namespace test
 } // namespace risk