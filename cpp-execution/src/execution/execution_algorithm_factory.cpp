// execution_algorithm_factory.cpp
#include "execution/execution_algorithm_factory.hpp"
#include "execution/algorithms/twap_algorithm.hpp"
#include "execution/algorithms/twap_config.hpp"
#include "execution/algorithms/vwap_algorithm.hpp"
#include "execution/algorithms/iceberg_algorithm.hpp"
#include "execution/algorithms/pov_algorithm.hpp"
#include "execution/algorithms/pov_config.hpp"
#include <core/market_types.hpp>

#include <chrono>
#include <iostream>
#include <uuid/uuid.h>
#include <util/parse_safe.hpp>

namespace trading {
namespace execution {

    using namespace core;
// Generate a unique ID for algorithms
std::string generateUniqueId() {
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);
    return std::string(uuid_str);
}

// Wrapper classes for algorithm implementations

// TWAP Algorithm Wrapper
class ExecutionAlgorithmFactory::TwapAlgorithmWrapper : public ExecutionAlgorithm {
public:
    TwapAlgorithmWrapper(
        const std::string& id,
        const core::Symbol& tradingPair,
        models::OrderSide side,
        const algorithms::TwapConfig& config)
        : id_(id), algo_() {
        
        // Initialize algorithm
        algo_.initialize(tradingPair, side, config);
    }
    
    // Implement ExecutionAlgorithm interface
    ExecutionResponse execute(const models::Order& order, ExecutionService& execution_service) override {
        return algo_.execute(order, execution_service);
    }
    
    bool start() override {
        return algo_.start();
    }
    
    bool pause() override {
        return algo_.pause();
    }
    
    bool resume() override {
        return algo_.resume();
    }
    
    bool cancel() override {
        return algo_.cancel();
    }
    
    bool isRunning() const override {
        return algo_.isRunning();
    }
    
    bool isComplete() const override {
        return algo_.isComplete();
    }
    
    AlgorithmType getType() const override {
        return AlgorithmType::TWAP;
    }
    
    std::string getId() const override {
        return id_;
    }
    
    std::vector<models::OrderPtr> getChildOrders() const override {
        return algo_.getChildOrders();
    }
    
    void handleOrderUpdate(const models::OrderPtr& order) override {
        algo_.handleOrderUpdate(order);
    }
    
    void setOrderCallback(const std::function<void(const models::OrderPtr&)>& callback) {
        algo_.setOrderCallback(callback);
    }
    
    void setCompletionCallback(
        const std::function<void(bool, const std::string&)>& callback) {
        algo_.setCompletionCallback(callback);
    }
    
    void setVolumeCallback(
        const std::function<double(const std::string&, std::chrono::minutes)>& callback) {
        algo_.setVolumeCallback(callback);
    }
    
private:
    std::string id_;
    algorithms::TwapAlgorithm algo_;
};

// VWAP Algorithm Wrapper
class ExecutionAlgorithmFactory::VwapAlgorithmWrapper : public ExecutionAlgorithm {
public:
    VwapAlgorithmWrapper(
        const std::string& id,
        const core::Symbol& tradingPair,
        models::OrderSide side,
        const algorithms::VwapConfig& config)
        : id_(id), algo_() {
        
        // Initialize algorithm
        algo_.initialize(tradingPair, side, config);
    }
    
    // Implement ExecutionAlgorithm interface
    ExecutionResponse execute(const models::Order& order, ExecutionService& execution_service) override {
        return algo_.execute(order, execution_service);
    }
    
    bool start() override {
        return algo_.start();
    }
    
    bool pause() override {
        return algo_.pause();
    }
    
    bool resume() override {
        return algo_.resume();
    }
    
    bool cancel() override {
        return algo_.cancel();
    }
    
    bool isRunning() const override {
        return algo_.isRunning();
    }
    
    bool isComplete() const override {
        return algo_.isComplete();
    }
    
    AlgorithmType getType() const override {
        return AlgorithmType::VWAP;
    }
    
    std::string getId() const override {
        return id_;
    }
    
    std::vector<models::OrderPtr> getChildOrders() const override {
        return algo_.getChildOrders();
    }
    
    void handleOrderUpdate(const models::OrderPtr& order) override {
        algo_.handleOrderUpdate(order);
    }
    
    void setOrderCallback(const std::function<void(const models::OrderPtr&)>& callback) {
        algo_.setOrderCallback(callback);
    }
    
    void setCompletionCallback(
        const std::function<void(bool, const std::string&)>& callback) {
        algo_.setCompletionCallback(callback);
    }
    
    void setVolumeCallback(
        const std::function<double(const std::string&, std::chrono::minutes)>& callback) {
        algo_.setVolumeCallback(callback);
    }
    
private:
    std::string id_;
    algorithms::VwapAlgorithm algo_;
};

// Iceberg Algorithm Wrapper
class ExecutionAlgorithmFactory::IcebergAlgorithmWrapper : public ExecutionAlgorithm {
public:
    IcebergAlgorithmWrapper(
        const std::string& id,
        const core::Symbol& tradingPair,
        models::OrderSide side,
        const algorithms::IcebergConfig& config)
        : id_(id), algo_() {
        
        // Initialize algorithm
        algo_.initialize(tradingPair, side, config);
    }
    
    // Implement ExecutionAlgorithm interface
    ExecutionResponse execute(const models::Order& order, ExecutionService& execution_service) override {
        return algo_.execute(order, execution_service);
    }
    
    bool start() override {
        return algo_.start();
    }
    
    bool pause() override {
        return algo_.pause();
    }
    
    bool resume() override {
        return algo_.resume();
    }
    
    bool cancel() override {
        return algo_.cancel();
    }
    
    bool isRunning() const override {
        return algo_.isRunning();
    }
    
    bool isComplete() const override {
        return algo_.isComplete();
    }
    
    AlgorithmType getType() const override {
        return AlgorithmType::ICEBERG;
    }
    
    std::string getId() const override {
        return id_;
    }
    
    std::vector<models::OrderPtr> getChildOrders() const override {
        return algo_.getChildOrders();
    }
    
    void handleOrderUpdate(const models::OrderPtr& order) override {
        algo_.handleOrderUpdate(order);
    }
    
    void setOrderCallback(const std::function<void(const models::OrderPtr&)>& callback) {
        algo_.setOrderCallback(callback);
    }
    
    void setCompletionCallback(
        const std::function<void(bool, const std::string&)>& callback) {
        algo_.setCompletionCallback(callback);
    }
    
private:
    std::string id_;
    algorithms::IcebergAlgorithm algo_;
};

// POV Algorithm Wrapper
class ExecutionAlgorithmFactory::PovAlgorithmWrapper : public ExecutionAlgorithm {
public:
    PovAlgorithmWrapper(
        const std::string& id,
        const core::Symbol& tradingPair,
        models::OrderSide side,
        const algorithms::PovConfig& config)
        : id_(id), algo_() {
        
        // Initialize algorithm
        algo_.initialize(tradingPair, side, config);
    }
    
    // Implement ExecutionAlgorithm interface
    ExecutionResponse execute(const models::Order& order, ExecutionService& execution_service) override {
        return algo_.execute(order, execution_service);
    }
    
    bool start() override {
        return algo_.start();
    }
    
    bool pause() override {
        return algo_.pause();
    }
    
    bool resume() override {
        return algo_.resume();
    }
    
    bool cancel() override {
        return algo_.cancel();
    }
    
    bool isRunning() const override {
        return algo_.isRunning();
    }
    
    bool isComplete() const override {
        return algo_.isComplete();
    }
    
    AlgorithmType getType() const override {
        return AlgorithmType::POV;
    }
    
    std::string getId() const override {
        return id_;
    }
    
    std::vector<models::OrderPtr> getChildOrders() const override {
        return algo_.getChildOrders();
    }
    
    void handleOrderUpdate(const models::OrderPtr& order) override {
        algo_.handleOrderUpdate(order);
    }
    
    void setOrderCallback(const std::function<void(const models::OrderPtr&)>& callback) {
        algo_.setOrderCallback(callback);
    }
    
    void setCompletionCallback(
        const std::function<void(bool, const std::string&)>& callback) {
        algo_.setCompletionCallback(callback);
    }
    
    void setMarketVolumeCallback(
        const std::function<double(const std::string&, std::chrono::minutes)>& callback) {
        algo_.setMarketVolumeCallback(callback);
    }
    
private:
    std::string id_;
    algorithms::PovAlgorithm algo_;
};

// Factory implementation methods follow...

ExecutionAlgorithmFactory::ExecutionAlgorithmFactory() = default;

std::shared_ptr<ExecutionAlgorithm> ExecutionAlgorithmFactory::createAlgorithm(
    const std::shared_ptr<DefaultAlgorithmParams>& params) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!params) {
        std::cerr << "Algorithm parameters cannot be null" << std::endl;
        return nullptr;
    }
    
    // Generate a unique ID
    std::string algorithm_id = generateUniqueId();
    
    // Create algorithm based on type
    std::shared_ptr<ExecutionAlgorithm> algorithm;
    
    switch (params->getType()) {
        case AlgorithmType::TWAP: {
            // Create TWAP config from default params
            algorithms::TwapConfig twap_config;
            twap_config.total_quantity = params->quantity;
            twap_config.duration = std::chrono::seconds(static_cast<int>(params->duration_seconds));
            twap_config.slice_interval = std::chrono::seconds(static_cast<int>(params->duration_seconds / 10)); // Default to 10 slices
            twap_config.price_limit = params->price;
            
            auto wrapper = std::make_shared<TwapAlgorithmWrapper>(
                algorithm_id, params->tradingPair, params->side, twap_config);
            
            if (order_callback_) {
                wrapper->setOrderCallback(order_callback_);
            }
            
            if (completion_callback_) {
                wrapper->setCompletionCallback(
                    [this, algorithm_id](bool success, const std::string& message) {
                        completion_callback_(algorithm_id, success, message);
                    });
            }
            
            algorithm = wrapper;
            break;
        }
        
        case AlgorithmType::VWAP: {
            // Create VWAP config from default params
            algorithms::VwapConfig vwap_config;
            vwap_config.total_quantity = params->quantity;
            vwap_config.duration = std::chrono::seconds(static_cast<int>(params->duration_seconds));
            vwap_config.price_limit = params->price;
            
            // Default volume profile (equal distribution)
            std::vector<double> volume_profile(24, 1.0/24.0);
            vwap_config.volume_profile = volume_profile;
            
            auto wrapper = std::make_shared<VwapAlgorithmWrapper>(
                algorithm_id, params->tradingPair, params->side, vwap_config);
            
            if (order_callback_) {
                wrapper->setOrderCallback(order_callback_);
            }
            
            if (completion_callback_) {
                wrapper->setCompletionCallback(
                    [this, algorithm_id](bool success, const std::string& message) {
                        completion_callback_(algorithm_id, success, message);
                    });
            }
            
            if (volume_callback_) {
                wrapper->setVolumeCallback(volume_callback_);
            }
            
            algorithm = wrapper;
            break;
        }
        
        case AlgorithmType::ICEBERG: {
            // Create Iceberg config from default params
            algorithms::IcebergConfig iceberg_config;
            iceberg_config.total_quantity = params->quantity;
            iceberg_config.visible_quantity = params->visible_quantity > 0 ? 
                params->visible_quantity : params->quantity / 10.0; // Default to 1/10th
            iceberg_config.price = params->price;
            
            auto wrapper = std::make_shared<IcebergAlgorithmWrapper>(
                algorithm_id, params->tradingPair, params->side, iceberg_config);
            
            if (order_callback_) {
                wrapper->setOrderCallback(order_callback_);
            }
            
            if (completion_callback_) {
                wrapper->setCompletionCallback(
                    [this, algorithm_id](bool success, const std::string& message) {
                        completion_callback_(algorithm_id, success, message);
                    });
            }
            
            algorithm = wrapper;
            break;
        }
        
        case AlgorithmType::POV: {
            // Create POV config from default params
            algorithms::PovConfig pov_config;
            pov_config.total_quantity = params->quantity;
            pov_config.duration = std::chrono::seconds(static_cast<int>(params->duration_seconds));
            pov_config.price_limit = params->price;
            
            // Default to 5% participation rate if not specified
            double participation_rate = 0.05;
            if (params->extra_params.find("participation_rate") != params->extra_params.end()) {
                participation_rate = std::stod(params->extra_params.at("participation_rate"));
            }
            pov_config.participation_rate = participation_rate;
            
            // Set optional parameters if specified
            if (params->extra_params.find("min_order_size") != params->extra_params.end()) {
                pov_config.min_order_size = std::stod(params->extra_params.at("min_order_size"));
            }
            
            if (params->extra_params.find("max_order_size") != params->extra_params.end()) {
                pov_config.max_order_size = std::stod(params->extra_params.at("max_order_size"));
            }
            
            if (params->extra_params.find("check_interval_seconds") != params->extra_params.end()) {
                int ci = ::util::parseIntOr(params->extra_params.at("check_interval_seconds"), 60);
                pov_config.check_interval = std::chrono::seconds(ci);
            }
            
            // Use exchange ID if available
            if (!params->exchangeId.toString().empty()) {
                pov_config.exchangeId = params->exchangeId;
            }
            
            auto wrapper = std::make_shared<PovAlgorithmWrapper>(
                algorithm_id, params->tradingPair, params->side, pov_config);
            
            if (order_callback_) {
                wrapper->setOrderCallback(order_callback_);
            }
            
            if (completion_callback_) {
                wrapper->setCompletionCallback(
                    [this, algorithm_id](bool success, const std::string& message) {
                        completion_callback_(algorithm_id, success, message);
                    });
            }
            
            if (volume_callback_) {
                wrapper->setMarketVolumeCallback(volume_callback_);
            }
            
            algorithm = wrapper;
            break;
        }
        
        default:
            std::cerr << "Unsupported algorithm type" << std::endl;
            return nullptr;
    }
    
    // Store algorithm in the factory's map
    algorithms_[algorithm_id] = algorithm;
    
    return algorithm;
}

std::shared_ptr<ExecutionAlgorithm> ExecutionAlgorithmFactory::getAlgorithm(
    const std::string& id) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = algorithms_.find(id);
    if (it != algorithms_.end()) {
        return it->second;
    }
    
    return nullptr;
}

void ExecutionAlgorithmFactory::registerOrderCallback(
    const std::function<void(const models::OrderPtr&)>& callback) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    order_callback_ = callback;
    
    // Update callback for existing algorithms
    for (auto& pair : algorithms_) {
        if (pair.second->getType() == AlgorithmType::TWAP) {
            auto wrapper = std::dynamic_pointer_cast<TwapAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setOrderCallback(callback);
            }
        } else if (pair.second->getType() == AlgorithmType::VWAP) {
            auto wrapper = std::dynamic_pointer_cast<VwapAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setOrderCallback(callback);
            }
        } else if (pair.second->getType() == AlgorithmType::ICEBERG) {
            auto wrapper = std::dynamic_pointer_cast<IcebergAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setOrderCallback(callback);
            }
        } else if (pair.second->getType() == AlgorithmType::POV) {
            auto wrapper = std::dynamic_pointer_cast<PovAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setOrderCallback(callback);
            }
        }
    }
}

void ExecutionAlgorithmFactory::registerCompletionCallback(
    const std::function<void(const std::string&, bool, const std::string&)>& callback) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    completion_callback_ = callback;
    
    // Update callback for existing algorithms
    for (auto& pair : algorithms_) {
        if (pair.second->getType() == AlgorithmType::TWAP) {
            auto wrapper = std::dynamic_pointer_cast<TwapAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setCompletionCallback(
                    [this, id = pair.first](bool success, const std::string& message) {
                        completion_callback_(id, success, message);
                    });
            }
        } else if (pair.second->getType() == AlgorithmType::VWAP) {
            auto wrapper = std::dynamic_pointer_cast<VwapAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setCompletionCallback(
                    [this, id = pair.first](bool success, const std::string& message) {
                        completion_callback_(id, success, message);
                    });
            }
        } else if (pair.second->getType() == AlgorithmType::ICEBERG) {
            auto wrapper = std::dynamic_pointer_cast<IcebergAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setCompletionCallback(
                    [this, id = pair.first](bool success, const std::string& message) {
                        completion_callback_(id, success, message);
                    });
            }
        } else if (pair.second->getType() == AlgorithmType::POV) {
            auto wrapper = std::dynamic_pointer_cast<PovAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setCompletionCallback(
                    [this, id = pair.first](bool success, const std::string& message) {
                        completion_callback_(id, success, message);
                    });
            }
        }
    }
}

void ExecutionAlgorithmFactory::registerVolumeCallback(
    const std::function<double(const std::string&, std::chrono::minutes)>& callback) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    volume_callback_ = callback;
    
    // Update callback for existing algorithms
    for (auto& pair : algorithms_) {
        if (pair.second->getType() == AlgorithmType::VWAP) {
            auto wrapper = std::dynamic_pointer_cast<VwapAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setVolumeCallback(callback);
            }
        } else if (pair.second->getType() == AlgorithmType::TWAP) {
            auto wrapper = std::dynamic_pointer_cast<TwapAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setVolumeCallback(callback);
            }
        } else if (pair.second->getType() == AlgorithmType::POV) {
            auto wrapper = std::dynamic_pointer_cast<PovAlgorithmWrapper>(pair.second);
            if (wrapper) {
                wrapper->setMarketVolumeCallback(callback);
            }
        }
    }
}

} // namespace execution
} // namespace trading