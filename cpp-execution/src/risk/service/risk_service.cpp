/**
 * Implementation of the risk service interface
 *
 * File: risk_service.cpp
 * Created: 2025-04-29
 */

 #include "service/risk_service.hpp"
 #include <nlohmann/json.hpp>
 #include <chrono>
 #include <sstream>
 #include <iostream>
 
 namespace trading {
 namespace risk {
 
 // Using nlohmann::json for JSON processing
 using json = nlohmann::json;
 
 RiskService::RiskService(const RiskConfig& config)
     : m_config(config)
     , m_running(false)
 {
 }
 
 RiskService::~RiskService() {
     stop();
 }
 
 bool RiskService::initialize() {
     // Ensure validation service is set
     if (!m_validationService) {
         std::cerr << "Risk service initialization failed: No validation service set" << std::endl;
         return false;
     }
     
     return true;
 }
 
 bool RiskService::start() {
     if (m_running.load()) {
         return true; // Already running
     }
     
     m_running.store(true);
     m_serviceThread = std::thread(&RiskService::serviceLoop, this);
     
     return true;
 }
 
 bool RiskService::stop() {
     if (!m_running.load()) {
         return true; // Already stopped
     }
     
     m_running.store(false);
     
     if (m_serviceThread.joinable()) {
         m_serviceThread.join();
     }
     
     return true;
 }
 
 bool RiskService::isRunning() const {
     return m_running.load();
 }
 
 void RiskService::registerRiskEventCallback(const RiskEventCallback& callback) {
     std::lock_guard<std::mutex> lock(m_callbackMutex);
     m_eventCallbacks.push_back(callback);
 }
 
 void RiskService::setRiskValidationService(std::shared_ptr<RiskValidationService> validationService) {
     m_validationService = validationService;
 }
 
 std::shared_ptr<RiskValidationService> RiskService::getRiskValidationService() const {
     return m_validationService;
 }
 
 ValidationResult RiskService::validateSignal(const TradingSignal& signal) {
     if (!m_validationService) {
         ValidationResult result;
         result.signalId = signal.id;
         result.approved = false;
         
         RiskViolation violation;
         violation.type = ViolationType::SYSTEM_ERROR;
         violation.description = "Validation service unavailable";
         result.violations.push_back(violation);
         result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
         
         return result;
     }
     
     return m_validationService->validateSignal(signal);
 }
 
 RiskParameters RiskService::getRiskParameters(const ExchangeId& exchangeId, 
                                            const TradingPair& tradingPair,
                                            const StrategyId& strategyId) const {
     if (!m_validationService) {
         return RiskParameters(); // Return empty parameters
     }
     
     return m_validationService->getRiskParameters(exchangeId, tradingPair, strategyId);
 }
 
 bool RiskService::updateRiskParameters(const RiskParameters& params) {
     if (!m_validationService) {
         return false;
     }
     
     return m_validationService->updateRiskParameters(params);
 }
 
 RiskManagerMode RiskService::getMode() const {
     if (!m_validationService) {
         return RiskManagerMode::MAINTENANCE; // Default to maintenance mode for safety
     }
     
     return m_validationService->getMode();
 }
 
 bool RiskService::setMode(RiskManagerMode mode) {
     if (!m_validationService) {
         return false;
     }
     
     return m_validationService->setMode(mode);
 }
 
 std::string RiskService::getHealthStatus() const {
     json healthData;
     
     // Basic service health information
     healthData["service"] = "RiskService";
     healthData["status"] = isRunning() ? "running" : "stopped";
     healthData["validation_service"] = m_validationService ? "available" : "unavailable";
     
     if (m_validationService) {
         healthData["mode"] = static_cast<int>(getMode());
         healthData["validation_timeout_ms"] = m_validationService->getValidationTimeout();
     }
     
     healthData["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
     
     return healthData.dump(2); // Pretty print with 2-space indent
 }
 
 std::string RiskService::getMetrics() const {
     json metricsData;
     
     // Basic service metrics
     metricsData["service"] = "RiskService";
     metricsData["running"] = isRunning();
     
     // Add more detailed metrics here based on internal counters
     
     metricsData["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
     
     return metricsData.dump(2); // Pretty print with 2-space indent
 }
 
 void RiskService::serviceLoop() {
     while (m_running.load()) {
         // Handle incoming requests, process events, etc.
         
         // Sleep to avoid busy waiting
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
     }
 }
 
 void RiskService::notifyRiskEvent(const RiskEvent& event) {
     std::lock_guard<std::mutex> lock(m_callbackMutex);
     for (const auto& callback : m_eventCallbacks) {
         callback(event);
     }
 }
 
 void RiskService::handleRpcRequest(const std::string& request, std::string& response) {
     try {
         // Parse the request as JSON
         json requestJson = json::parse(request);
         
         std::string command = requestJson["command"];
         std::string payload = requestJson["payload"].dump();
         
         // Process the command
         response = processCommand(command, payload);
     } catch (const std::exception& e) {
         // Create error response
         json errorResponse;
         errorResponse["status"] = "error";
         errorResponse["message"] = std::string("Error processing request: ") + e.what();
         response = errorResponse.dump();
     }
 }
 
 std::string RiskService::processCommand(const std::string& command, const std::string& payload) {
     json responseJson;
     responseJson["command"] = command;
     
     if (command == "validateSignal") {
         try {
             // Parse the payload as a TradingSignal
             json signalJson = json::parse(payload);
             TradingSignal signal;
             
             // Populate signal from JSON
             signal.setId(signalJson["id"]);
             signal.setExchangeId(ExchangeId::fromString(signalJson["exchangeId"]));
             signal.setTradingPair(TradingPair::fromString(signalJson["tradingPair"]));
             signal.setDirection(signalJson["direction"] == "buy" ? Direction::BUY : Direction::SELL);
             signal.setStrength(signalJson["strength"]);
            //  signal.setTargetSize(signalJson["target_size"]);
             signal.setStrategyId(StrategyId(signalJson["strategy_id"]));

             // Validate the signal
             ValidationResult result = validateSignal(signal);
             
             // Create response JSON
             json resultJson;
             resultJson["signal_id"] = result.signalId;
             resultJson["approved"] = result.approved;
             resultJson["adjusted_size"] = result.adjustedSize;
             resultJson["timestamp"] = result.timestamp;
             
             json violationsJson = json::array();
             for (const auto& violation : result.violations) {
                 json violationJson;
                 violationJson["type"] = violation.type;
                 violationJson["description"] = violation.description;
                 violationJson["limit_value"] = violation.limitValue;
                 violationJson["actual_value"] = violation.actualValue;
                 violationsJson.push_back(violationJson);
             }
             resultJson["violations"] = violationsJson;
             
             responseJson["status"] = "success";
             responseJson["result"] = resultJson;
         } catch (const std::exception& e) {
             responseJson["status"] = "error";
             responseJson["message"] = std::string("Error validating signal: ") + e.what();
         }
     } else if (command == "getRiskParameters") {
         try {
             // Parse the payload
             json paramsJson = json::parse(payload);
             
             ExchangeId exchangeId = ExchangeId::fromString(paramsJson["exchangeId"]);
             TradingPair tradingPair = TradingPair::fromString(paramsJson["tradingPair"]);
             StrategyId strategyId = StrategyId::fromString(paramsJson["strategy_id"]);
             
             // Get the parameters
             RiskParameters params = getRiskParameters(exchangeId, tradingPair, strategyId);
             
             // Create response JSON
             json paramsResultJson;
             paramsResultJson["exchangeId"] = params.exchangeId.toString();
             paramsResultJson["tradingPair"] = params.tradingPair.toString();
             paramsResultJson["max_position_size"] = params.maxPositionSize;
             paramsResultJson["max_order_size"] = params.maxOrderSize;
             paramsResultJson["maxDrawdown"] = params.maxDrawdown;
             paramsResultJson["max_exposure"] = params.maxExposure;
             paramsResultJson["last_updated"] = params.lastUpdated;
             
             responseJson["status"] = "success";
             responseJson["result"] = paramsResultJson;
         } catch (const std::exception& e) {
             responseJson["status"] = "error";
             responseJson["message"] = std::string("Error getting risk parameters: ") + e.what();
         }
     } else if (command == "updateRiskParameters") {
         try {
             // Parse the payload
             json paramsJson = json::parse(payload);
             
             RiskParameters params;
             params.exchangeId = ExchangeId::fromString(paramsJson["exchangeId"]);
             params.tradingPair = TradingPair::fromString(paramsJson["tradingPair"]);
             params.maxPositionSize = paramsJson["max_position_size"];
             params.maxOrderSize = paramsJson["max_order_size"];
             params.maxDrawdown = paramsJson["maxDrawdown"];
             params.maxExposure = paramsJson["max_exposure"];
             
             // Update the parameters
             bool success = updateRiskParameters(params);
             
             responseJson["status"] = success ? "success" : "error";
             if (!success) {
                 responseJson["message"] = "Failed to update risk parameters";
             }
         } catch (const std::exception& e) {
             responseJson["status"] = "error";
             responseJson["message"] = std::string("Error updating risk parameters: ") + e.what();
         }
     } else if (command == "getMode") {
         try {
             RiskManagerMode mode = getMode();
             
             responseJson["status"] = "success";
             responseJson["result"] = {{"mode", static_cast<int>(mode)}};
         } catch (const std::exception& e) {
             responseJson["status"] = "error";
             responseJson["message"] = std::string("Error getting mode: ") + e.what();
         }
     } else if (command == "setMode") {
         try {
             // Parse the payload
             json modeJson = json::parse(payload);
             RiskManagerMode mode = static_cast<RiskManagerMode>(modeJson["mode"].get<int>());
             
             // Set the mode
             bool success = setMode(mode);
             
             responseJson["status"] = success ? "success" : "error";
             if (!success) {
                 responseJson["message"] = "Failed to set mode";
             }
         } catch (const std::exception& e) {
             responseJson["status"] = "error";
             responseJson["message"] = std::string("Error setting mode: ") + e.what();
         }
     } else if (command == "getHealthStatus") {
         try {
             std::string healthStatus = getHealthStatus();
             
             responseJson["status"] = "success";
             responseJson["result"] = json::parse(healthStatus);
         } catch (const std::exception& e) {
             responseJson["status"] = "error";
             responseJson["message"] = std::string("Error getting health status: ") + e.what();
         }
     } else if (command == "getMetrics") {
         try {
             std::string metrics = getMetrics();
             
             responseJson["status"] = "success";
             responseJson["result"] = json::parse(metrics);
         } catch (const std::exception& e) {
             responseJson["status"] = "error";
             responseJson["message"] = std::string("Error getting metrics: ") + e.what();
         }
     } else {
         responseJson["status"] = "error";
         responseJson["message"] = "Unknown command: " + command;
     }
     
     return responseJson.dump();
 }
 
 } // namespace risk
 } // namespace trading