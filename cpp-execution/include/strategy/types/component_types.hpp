/**
 * @file strategy/types/component_types.hpp
 * @brief Component type aliases for the strategy module
 * 
 * This file defines type aliases for strategy components.
 * 
 * @author Trading System Team
 * @date May 13, 2025
 */

 #pragma once

 #include <memory>
 
 namespace trading {
 namespace strategy {
 
 // Forward declarations of strategy components
 class OrderBookAnalyzer;
 class ImbalanceDetectionEngine;
 class LiquidityFlowMonitor;
 class VolumeProfileAnalyzer;
 class ImbalanceSignalGenerator;
 class MomentumCorrelation;
 class ThresholdManager;
 class SignalDecayModel;
 class ExchangeCalibration;
 class AdaptiveMechanism;
 class LogisticRegressionSignalGenerator;
 class OrderBookAnalyzerManager;
 // Define pointer type aliases for easier usage
//  using OrderBookAnalyzerPtr = std::shared_ptr<OrderBookAnalyzer>;
using OrderBookAnalyzerPtr = std::shared_ptr<OrderBookAnalyzerManager>;
 using ImbalanceDetectionEnginePtr = std::shared_ptr<ImbalanceDetectionEngine>;
 using LiquidityFlowMonitorPtr = std::shared_ptr<LiquidityFlowMonitor>;
 using VolumeProfileAnalyzerPtr = std::shared_ptr<VolumeProfileAnalyzer>;
 using ImbalanceSignalGeneratorPtr = std::shared_ptr<ImbalanceSignalGenerator>;
 using MomentumCorrelationPtr = std::shared_ptr<MomentumCorrelation>;
 using ThresholdManagerPtr = std::shared_ptr<ThresholdManager>;
 using SignalDecayModelPtr = std::shared_ptr<SignalDecayModel>;
 using ExchangeCalibrationPtr = std::shared_ptr<ExchangeCalibration>;
 using AdaptiveMechanismPtr = std::shared_ptr<AdaptiveMechanism>;
 using LogisticRegressionSignalGeneratorPtr = std::shared_ptr<LogisticRegressionSignalGenerator>;
 
 } // namespace strategy
 } // namespace trading