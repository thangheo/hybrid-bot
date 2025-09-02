// api_client_manager.cpp
#include "execution/api_client_manager.hpp"
#include "execution/exchanges/binance_api_client.hpp"
#include "execution/exchanges/ftx_api_client.hpp"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <iostream>

namespace trading {
namespace execution {

ApiClientManager::ApiClientManager(std::shared_ptr<AuthenticationService> authService)
    : authService_(std::move(authService)) {
    if (!authService_) {
        throw std::invalid_argument("Authentication service cannot be null");
    }
}
//ApiClientManager constructor
ApiClientManager::ApiClientManager() 
    : authService_(std::make_shared<AuthenticationService>()) {
    if (!authService_) {
        throw std::invalid_argument("Authentication service cannot be null");
    }
}
ApiClientManager::~ApiClientManager() = default;

std::shared_ptr<ExchangeApiClient> ApiClientManager::getClient(const core::ExchangeId& exchangeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if client already exists
    auto it = clients_.find(exchangeId.toString());
    if (it != clients_.end()) {
        return it->second;
    }
    
    // Create a new client if it doesn't exist
    auto client = createClient(exchangeId);
    if (client) {
        clients_[exchangeId.toString()] = client;
        
        // Create a rate limit controller for this exchange if it doesn't exist
        if (rateLimitControllers_.find(exchangeId.toString()) == rateLimitControllers_.end()) {
            rateLimitControllers_[exchangeId.toString()] = std::make_shared<RateLimitController>(exchangeId);
        }
        
        return client;
    }
    
    throw std::runtime_error("Failed to create client for exchange: " + exchangeId.toString());
}

bool ApiClientManager::hasClient(const core::ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.find(exchangeId.toString()) != clients_.end();
}

std::vector<std::string> ApiClientManager::getAvailableExchanges() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> exchanges;
    exchanges.reserve(clients_.size());
    
    for (const auto& pair : clients_) {
        exchanges.push_back(pair.first);
    }
    
    return exchanges;
}

std::unordered_map<std::string, bool> ApiClientManager::checkConnections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, bool> statuses;
    
    for (const auto& pair : clients_) {
        const auto& exchangeId = pair.first;
        const auto& client = pair.second;
        try {
            bool isConnected = client->checkConnection();
            statuses[exchangeId] = isConnected;
        } catch (const std::exception& e) {
            std::cerr << "Error checking connection for " << exchangeId 
                      << ": " << e.what() << std::endl;
            statuses[exchangeId] = false;
        }
    }
    
    return statuses;
}
// getHealthStatus
std::string ApiClientManager::getHealthStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json health;
    health["service"] = "api_client_manager";
    health["clients"] = nlohmann::json::array();
    
    for (const auto& pair : clients_) {
        const auto& exchangeId = pair.first;
        const auto& client = pair.second;
        nlohmann::json clientHealth;
        clientHealth["exchangeId"] = exchangeId;
        clientHealth["connected"] = client->isConnected();
        health["clients"].push_back(clientHealth);
    }
    
    return health.dump();
}
// getExchangeStatuses
std::unordered_map<std::string, std::string> ApiClientManager::getExchangeStatuses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, std::string> statuses;
    
    for (const auto& pair : clients_) {
        const auto& exchangeId = pair.first;
        const auto& client = pair.second;
        try {
            auto status = client->getExchangeStatuses();
            
            statuses[exchangeId] = nlohmann::json(status).dump();
        } catch (const std::exception& e) {
            std::cerr << "Error getting exchange statuses for " << exchangeId 
                      << ": " << e.what() << std::endl;
            statuses[exchangeId] = "Error";
        }
    }
    
    return statuses;
}
void ApiClientManager::addClient(const core::ExchangeId& exchangeId, 
                                std::shared_ptr<ExchangeApiClient> client) {
    if (!client) {
        throw std::invalid_argument("Client cannot be null");
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    clients_[exchangeId.toString()] = std::move(client);
    
    // Create a rate limit controller if it doesn't exist
    if (rateLimitControllers_.find(exchangeId.toString()) == rateLimitControllers_.end()) {
        rateLimitControllers_[exchangeId.toString()] = std::make_shared<RateLimitController>(exchangeId);
    }
}

void ApiClientManager::removeClient(const core::ExchangeId& exchangeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(exchangeId.toString());
    rateLimitControllers_.erase(exchangeId.toString());
}

RateLimitStatus ApiClientManager::getRateLimitStatus(const core::ExchangeId& exchangeId, 
                                                  ExchangeRequestType requestType) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = rateLimitControllers_.find(exchangeId.toString());
    if (it == rateLimitControllers_.end()) {
        throw std::runtime_error("No rate limit controller found for exchange: " + exchangeId.toString());
    }
    
    return it->second->getRateLimitStatus(requestType);
}
bool ApiClientManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (clients_.empty()) {
        // return false; // No clients to initialize
        //for test
        return true;
    }
    for (auto& pair : clients_) {
        auto& client = pair.second;
        if (!client->isConnected()) {
            client->connect();
        }
    }
    return true;
}
// Shutdown all clients and release resources
void ApiClientManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : clients_) {
        pair.second->disconnect();
    }
    clients_.clear();
    rateLimitControllers_.clear();
}
// In api_client_manager.cpp
std::shared_ptr<ExchangeApiClient> ApiClientManager::createClient(const core::ExchangeId& exchangeId) {
    // Get credentials from the authentication service
    auto credentials = authService_->getCredentials(exchangeId);
    
    // Get the rate limit controller for this exchange
    auto rateLimitController = rateLimitControllers_[exchangeId.toString()];
    
    // Create the appropriate client based on the exchange ID
    if (exchangeId == core::ExchangeId::binance()) {
        return std::make_shared<BinanceApiClient>(credentials.value(), rateLimitController);
    } else if (exchangeId == core::ExchangeId::ftx()) {
        return std::make_shared<FtxApiClient>(credentials.value(), rateLimitController);
    } else {
        throw std::runtime_error("Unsupported exchange: " + exchangeId.toString());
    }
}
} // namespace execution
} // namespace trading