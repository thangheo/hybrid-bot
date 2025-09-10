#pragma once

#include "execution/algorithms/twap_config.hpp"
#include "execution/algorithms/vwap_algorithm.hpp"
#include "execution/algorithms/iceberg_algorithm.hpp"
#include "execution/algorithms/pov_algorithm.hpp"
#include "execution/execution_forward_decl.hpp"
#include "execution/execution_types.hpp"
#include <core/market_types.hpp>

#include "models/order.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace trading {
namespace execution {

    using namespace core;
/**
 * Enumeration of supported execution algorithm types
 */
enum class AlgorithmType {
    TWAP,
    VWAP,
    ICEBERG,
    POV,         // Percentage of Volume
    IMPLEMENTATION_SHORTFALL,
    ADAPTIVE
};

/**
 * Base class for algorithm parameters
 */
struct AlgorithmParams {
    virtual ~AlgorithmParams() = default;
    virtual AlgorithmType getType() const = 0;
};

/**
 * Default algorithm parameters with basic trading information
 */
struct DefaultAlgorithmParams : public AlgorithmParams {
    ExchangeId exchangeId;
    core::Symbol tradingPair;
    models::OrderSide side;
    models::OrderType order_type;
    double quantity;
    double price;
    std::string client_order_id;
    models::TimeInForce time_in_force;
    std::string start_time; // Optional start time for execution
    std::string end_time;   // Optional end time for execution
    double visible_quantity; // Optional visible size for iceberg orders
    double duration_seconds;
    double participation_rate = 0.0; // For POV algorithm
    std::string signal_id;
    std::unordered_map<std::string, std::string> extra_params;
    std::chrono::system_clock::time_point created_at;
    AlgorithmType algorithm_type = AlgorithmType::TWAP; // Default algorithm
    
    AlgorithmType getType() const override { return algorithm_type; }
};

/**
 * TWAP algorithm parameters
 */
struct TwapParams : public AlgorithmParams {
    algorithms::TwapConfig config;
    AlgorithmType getType() const override { return AlgorithmType::TWAP; }
};

/**
 * VWAP algorithm parameters
 */
struct VwapParams : public AlgorithmParams {
    algorithms::VwapConfig config;
    AlgorithmType getType() const override { return AlgorithmType::VWAP; }
};

/**
 * Iceberg algorithm parameters
 */
struct IcebergParams : public AlgorithmParams {
    algorithms::IcebergConfig config;
    AlgorithmType getType() const override { return AlgorithmType::ICEBERG; }
};

/**
 * POV algorithm parameters
 */
struct PovParams : public AlgorithmParams {
    algorithms::PovConfig config;
    AlgorithmType getType() const override { return AlgorithmType::POV; }
};

/**
 * Base class for execution algorithms
 */
class ExecutionAlgorithm {
public:
    virtual ~ExecutionAlgorithm() = default;
    
    // Main execution method
    virtual ExecutionResponse execute(const models::Order& order, ExecutionService& execution_service) = 0;

    // Common interface methods
    virtual bool start() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual bool cancel() = 0;
    virtual bool isRunning() const = 0;
    virtual bool isComplete() const = 0;
    virtual AlgorithmType getType() const = 0;
    virtual std::string getId() const = 0;
    virtual std::vector<models::OrderPtr> getChildOrders() const = 0;
    virtual void handleOrderUpdate(const models::OrderPtr& order) = 0;
};

/**
 * Factory for creating execution algorithms
 */
class ExecutionAlgorithmFactory {
public:
    // Constructor
    ExecutionAlgorithmFactory();
    
    // Create algorithm instance
    std::shared_ptr<ExecutionAlgorithm> createAlgorithm(
        const std::shared_ptr<DefaultAlgorithmParams>& params);
    
    // Get algorithm by ID
    std::shared_ptr<ExecutionAlgorithm> getAlgorithm(const std::string& id) const;
    
    // Register callbacks for all algorithms
    void registerOrderCallback(
        const std::function<void(const models::OrderPtr&)>& callback);
    
    void registerCompletionCallback(
        const std::function<void(const std::string&, bool, const std::string&)>& callback);
    
    // Register volume callback for VWAP and POV algorithms
    void registerVolumeCallback(
        const std::function<double(const std::string&, std::chrono::minutes)>& callback);
    
private:
    // Algorithm wrapper classes - defined in cpp file
    class TwapAlgorithmWrapper;
    class VwapAlgorithmWrapper;
    class IcebergAlgorithmWrapper;
    class PovAlgorithmWrapper;
    
    // Map of algorithm instances by ID
    std::unordered_map<std::string, std::shared_ptr<ExecutionAlgorithm>> algorithms_;
    
    // Callbacks
    std::function<void(const models::OrderPtr&)> order_callback_;
    std::function<void(const std::string&, bool, const std::string&)> completion_callback_;
    std::function<double(const std::string&, std::chrono::minutes)> volume_callback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
};

// Type aliases for convenience
using ExecutionAlgorithmPtr = std::shared_ptr<ExecutionAlgorithm>;
using AlgorithmParamsPtr = std::shared_ptr<AlgorithmParams>;

} // namespace execution
} // namespace trading