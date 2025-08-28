// In price_level.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <stdexcept>
#include <chrono>

namespace trading {
namespace data_ingestion {
 
class PriceLevel {
public:
    // Constructors
    PriceLevel() 
        : price_(0.0), quantity_(0.0), 
          timestamp_(std::chrono::system_clock::now()) {}
          
    PriceLevel(double price, double quantity) 
        : price_(price), quantity_(quantity), 
          timestamp_(std::chrono::system_clock::now()) {}
    
    PriceLevel(double price, double quantity, std::chrono::system_clock::time_point timestamp)
        : price_(price), quantity_(quantity), timestamp_(timestamp) {}
    
    // From string constructor if needed
    PriceLevel(const std::string& price, const std::string& quantity) {
        try {
            price_ = std::stod(price);
            quantity_ = std::stod(quantity);
            timestamp_ = std::chrono::system_clock::now();
        } catch (const std::exception& e) {
            throw std::invalid_argument("Invalid price or quantity format");
        }
    }
    
    // Basic getters and setters
    double getPrice() const { return price_; }
    double getQuantity() const { return quantity_; }
    std::chrono::system_clock::time_point getTimestamp() const { return timestamp_; }
    
    void setPrice(double price) { price_ = price; }
    void setQuantity(double quantity) { 
        quantity_ = quantity; 
        timestamp_ = std::chrono::system_clock::now();
    }
    void setTimestamp(std::chrono::system_clock::time_point timestamp) { timestamp_ = timestamp; }
    
    // Equality comparison
    bool operator==(const PriceLevel& other) const {
        return price_ == other.price_ && quantity_ == other.quantity_;
    }
    
    bool operator!=(const PriceLevel& other) const {
        return !(*this == other);
    }
    
private:
    double price_;
    double quantity_;
    std::chrono::system_clock::time_point timestamp_;
};

} // namespace data_ingestion
} // namespace trading