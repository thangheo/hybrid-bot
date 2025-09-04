/**
 * Simple Risk Manager Example
 * Demonstrates basic usage of ExecutionRiskManager for order validation
 */

#include <iostream>
#include <memory>
#include <chrono>
#include <vector>
// Minimal includes for the example
namespace trading::risk {

// Simplified structures for the example
struct FatFingerConfig {
    double maxPriceDeviation = 0.05;        // 5%
    double maxOrderSize = 10.0;
    double notionalValueLimit = 10000.0;
    bool enforceAdditionalVerification = true;
};

struct OrderValidationConfig {
    bool validateMinOrderSize = true;
    bool validatePriceRange = true;
    double maxPriceDeviationPercent = 0.1;   // 10%
    std::vector<std::string> symbolWhitelist = {"BTC/USDT", "ETH/USDT"};
};

struct CircuitBreakerConfig {
    double maxSlippageThreshold = 0.02;      // 2%
    double maxRejectionRate = 0.1;           // 10%
    int maxConsecutiveRejections = 5;
    std::chrono::seconds coolingPeriod{300}; // 5 minutes
    bool autoResume = true;
    int maxTriggersPerDay = 5;
};

struct ExecutionLimitsConfig {
    double maxOrderSize = 1000.0;
    double maxMarketImpact = 0.01;           // 1%
    double maxPriceDeviation = 0.05;         // 5%
    int maxOrdersPerSecond = 10;
    bool allowMarketOrders = true;
};

struct RiskConfig {
    FatFingerConfig fatFingerConfig;
    OrderValidationConfig orderValidationConfig;
    CircuitBreakerConfig circuitBreakerConfig;
    ExecutionLimitsConfig executionLimitsConfig;
    
    RiskConfig() {
        // Initialize with defaults - already set in struct definitions
    }
};

struct OrderRequest {
    std::string orderId;
    std::string tradingPair;
    std::string side;                        // "BUY" or "SELL"
    std::string orderType;                   // "MARKET" or "LIMIT"
    double quantity;
    double price;                            // For limit orders
    std::string exchangeId;
    std::chrono::system_clock::time_point timestamp;
    
    OrderRequest(const std::string& pair, const std::string& orderSide, 
                 double qty, double prc = 0.0)
        : orderId("order_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())),
          tradingPair(pair), side(orderSide), quantity(qty), price(prc),
          exchangeId("binance"), timestamp(std::chrono::system_clock::now()) {
        orderType = (price > 0) ? "LIMIT" : "MARKET";
    }
};

struct MarketData {
    std::string symbol;
    double currentPrice;
    double bidPrice;
    double askPrice;
    double volume24h;
    double volatility;
    std::chrono::system_clock::time_point timestamp;
    
    MarketData(const std::string& sym, double price)
        : symbol(sym), currentPrice(price), 
          bidPrice(price * 0.999), askPrice(price * 1.001),
          volume24h(1000000.0), volatility(0.02),
          timestamp(std::chrono::system_clock::now()) {}
};

struct ValidationResult {
    bool approved = false;
    std::string rejectionReason;
    std::optional<double> adjustedQuantity;
    std::optional<double> maxPriceImpact;
    std::chrono::microseconds validationTime{0};
    
    ValidationResult() = default;
    ValidationResult(bool approve) : approved(approve) {}
    ValidationResult(bool approve, const std::string& reason) 
        : approved(approve), rejectionReason(reason) {}
};

// Simplified Risk Manager class
class SimpleRiskManager {
private:
    RiskConfig config_;
    std::unordered_map<std::string, MarketData> marketData_;
    std::unordered_map<std::string, int> orderCounts_;
    std::atomic<bool> emergencyStop_{false};
    
public:
    explicit SimpleRiskManager(const RiskConfig& config) : config_(config) {}
    
    bool initialize() {
        // Simulate market data initialization
        marketData_["BTC/USDT"] = MarketData("BTC/USDT", 45000.0);
        marketData_["ETH/USDT"] = MarketData("ETH/USDT", 3000.0);
        marketData_["DOGE/USDT"] = MarketData("DOGE/USDT", 0.08);
        
        std::cout << "Risk Manager initialized with market data for " 
                  << marketData_.size() << " symbols\n";
        return true;
    }
    
    ValidationResult validateOrder(const OrderRequest& order) {
        auto startTime = std::chrono::high_resolution_clock::now();
        ValidationResult result;
        
        // 1. Emergency stop check
        if (emergencyStop_.load()) {
            result.rejectionReason = "Emergency stop active";
            return result;
        }
        
        // 2. Symbol validation
        if (marketData_.find(order.tradingPair) == marketData_.end()) {
            result.rejectionReason = "Unknown trading pair: " + order.tradingPair;
            return result;
        }
        
        // 3. Order size validation
        if (order.getQuantity() > config_.executionLimitsConfig.maxOrderSize) {
            result.rejectionReason = "Order size exceeds limit (" + 
                std::to_string(config_.executionLimitsConfig.maxOrderSize) + ")";
            result.adjustedQuantity = config_.executionLimitsConfig.maxOrderSize;
            // Could still approve with adjustment
        }
        
        // 4. Fat finger protection
        const auto& marketData = marketData_[order.tradingPair];
        if (order.orderType == "LIMIT" && order.getPrice() > 0) {
            double priceDeviation = std::abs(order.getPrice() - marketData.currentPrice) / marketData.currentPrice;
            if (priceDeviation > config_.fatFingerConfig.maxPriceDeviation) {
                result.rejectionReason = "Price deviation too large: " + 
                    std::to_string(priceDeviation * 100) + "% (limit: " +
                    std::to_string(config_.fatFingerConfig.maxPriceDeviation * 100) + "%)";
                return result;
            }
        }
        
        // 5. Notional value check
        double notionalValue = order.getQuantity() * marketData.currentPrice;
        if (notionalValue > config_.fatFingerConfig.notionalValueLimit) {
            result.rejectionReason = "Notional value exceeds limit: $" + 
                std::to_string(notionalValue) + " (limit: $" +
                std::to_string(config_.fatFingerConfig.notionalValueLimit) + ")";
            return result;
        }
        
        // 6. Rate limiting
        std::string rateKey = order.exchangeId + ":" + order.tradingPair;
        orderCounts_[rateKey]++;
        if (orderCounts_[rateKey] > config_.executionLimitsConfig.maxOrdersPerSecond) {
            result.rejectionReason = "Rate limit exceeded for " + order.tradingPair;
            return result;
        }
        
        // Order approved
        result.approved = true;
        if (!result.rejectionReason.empty()) {
            result.rejectionReason = "Approved with adjustments";
        }
        
        // Calculate validation time
        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationTime = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime);
        
        return result;
    }
    
    void updateMarketData(const std::string& symbol, double newPrice) {
        if (marketData_.find(symbol) != marketData_.end()) {
            marketData_[symbol].currentPrice = newPrice;
            marketData_[symbol].bidPrice = newPrice * 0.999;
            marketData_[symbol].askPrice = newPrice * 1.001;
            marketData_[symbol].timestamp = std::chrono::system_clock::now();
        }
    }
    
    void triggerEmergencyStop(const std::string& reason) {
        emergencyStop_.store(true);
        std::cout << "EMERGENCY STOP TRIGGERED: " << reason << std::endl;
    }
    
    void resetEmergencyStop() {
        emergencyStop_.store(false);
        std::cout << "Emergency stop cleared\n";
    }
    
    void resetRateLimits() {
        orderCounts_.clear();
    }
    
    MarketData getMarketData(const std::string& symbol) const {
        auto it = marketData_.find(symbol);
        return (it != marketData_.end()) ? it->second : MarketData(symbol, 0.0);
    }
};

} // namespace trading::risk

// Example usage
int main() {
    using namespace trading::risk;
    
    std::cout << "=== Risk Manager Example ===\n\n";
    
    // 1. Create risk configuration
    RiskConfig config;
    config.fatFingerConfig.maxPriceDeviation = 0.05;      // 5% max deviation
    config.fatFingerConfig.notionalValueLimit = 50000.0;  // $50k limit
    config.executionLimitsConfig.maxOrderSize = 1.0;      // 1 BTC max
    
    // 2. Initialize risk manager
    SimpleRiskManager riskManager(config);
    if (!riskManager.initialize()) {
        std::cerr << "Failed to initialize risk manager\n";
        return 1;
    }
    
    std::cout << "Risk Manager initialized successfully\n\n";
    
    // 3. Test valid orders
    std::cout << "=== Testing Valid Orders ===\n";
    
    // Valid BTC order
    OrderRequest validOrder("BTC/USDT", "BUY", 0.5, 45500.0);
    auto result = riskManager.validateOrder(validOrder);
    std::cout << "BTC Order (0.5 @ $45,500): " 
              << (result.approved ? "APPROVED" : "REJECTED");
    if (!result.rejectionReason.empty()) {
        std::cout << " - " << result.rejectionReason;
    }
    std::cout << " (validation time: " << result.validationTime.count() << "μs)\n";
    
    // Valid ETH order
    OrderRequest validEthOrder("ETH/USDT", "SELL", 5.0, 2980.0);
    result = riskManager.validateOrder(validEthOrder);
    std::cout << "ETH Order (5.0 @ $2,980): " 
              << (result.approved ? "APPROVED" : "REJECTED");
    if (!result.rejectionReason.empty()) {
        std::cout << " - " << result.rejectionReason;
    }
    std::cout << " (validation time: " << result.validationTime.count() << "μs)\n\n";
    
    // 4. Test rejection scenarios
    std::cout << "=== Testing Risk Violations ===\n";
    
    // Order too large
    OrderRequest largeOrder("BTC/USDT", "BUY", 2.0, 45000.0);  // Exceeds 1.0 BTC limit
    result = riskManager.validateOrder(largeOrder);
    std::cout << "Large BTC Order (2.0): " 
              << (result.approved ? "APPROVED" : "REJECTED") 
              << " - " << result.rejectionReason << "\n";
    
    // Price deviation too high
    OrderRequest expensiveOrder("BTC/USDT", "BUY", 0.1, 50000.0);  // >5% above market
    result = riskManager.validateOrder(expensiveOrder);
    std::cout << "Expensive BTC Order ($50,000): " 
              << (result.approved ? "APPROVED" : "REJECTED") 
              << " - " << result.rejectionReason << "\n";
    
    // High notional value
    OrderRequest highValueOrder("BTC/USDT", "BUY", 1.5, 45000.0);  // $67.5k notional
    result = riskManager.validateOrder(highValueOrder);
    std::cout << "High Value Order ($67.5k): " 
              << (result.approved ? "APPROVED" : "REJECTED") 
              << " - " << result.rejectionReason << "\n";
    
    // Unknown symbol
    OrderRequest unknownOrder("UNKNOWN/USDT", "BUY", 1.0, 100.0);
    result = riskManager.validateOrder(unknownOrder);
    std::cout << "Unknown Symbol Order: " 
              << (result.approved ? "APPROVED" : "REJECTED") 
              << " - " << result.rejectionReason << "\n\n";
    
    // 5. Test emergency scenarios
    std::cout << "=== Testing Emergency Controls ===\n";
    
    // Trigger emergency stop
    riskManager.triggerEmergencyStop("Market volatility exceeded threshold");
    
    OrderRequest emergencyOrder("ETH/USDT", "BUY", 1.0, 3000.0);
    result = riskManager.validateOrder(emergencyOrder);
    std::cout << "Order during emergency: " 
              << (result.approved ? "APPROVED" : "REJECTED") 
              << " - " << result.rejectionReason << "\n";
    
    // Clear emergency stop
    riskManager.resetEmergencyStop();
    result = riskManager.validateOrder(emergencyOrder);
    std::cout << "Order after emergency cleared: " 
              << (result.approved ? "APPROVED" : "REJECTED") << "\n\n";
    
    // 6. Market data update simulation
    std::cout << "=== Market Data Updates ===\n";
    
    auto btcData = riskManager.getMarketData("BTC/USDT");
    std::cout << "Current BTC price: $" << btcData.currentPrice << "\n";
    
    // Simulate price movement
    riskManager.updateMarketData("BTC/USDT", 47000.0);
    btcData = riskManager.getMarketData("BTC/USDT");
    std::cout << "Updated BTC price: $" << btcData.currentPrice << "\n";
    
    // Test order with updated price
    OrderRequest updatedPriceOrder("BTC/USDT", "LIMIT", 0.2, 46000.0);
    result = riskManager.validateOrder(updatedPriceOrder);
    std::cout << "Order at old price level: " 
              << (result.approved ? "APPROVED" : "REJECTED");
    if (!result.rejectionReason.empty()) {
        std::cout << " - " << result.rejectionReason;
    }
    std::cout << "\n\n";
    
    std::cout << "=== Risk Manager Example Complete ===\n";
    
    return 0;
}