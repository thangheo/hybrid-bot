// include/data_ingestion/monitoring/pipeline_metrics.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include <limits>
#include <vector>
#include "../../core/market_types.hpp"

namespace trading {
namespace data_ingestion {

// Metrics tracking for the normalization pipeline
class PipelineMetrics {
public:
    PipelineMetrics() = default;
    
    // Count metrics
    void increment_input_count(const core::ExchangeId& exchange, const std::string& symbol) {
        std::string key = exchange.toString() + ":" + symbol;
        
        std::lock_guard<std::mutex> lock(mutex_);
        ++input_counts_[key];
        ++total_input_count_;
    }
    
    void increment_output_count(const core::ExchangeId& exchange, const std::string& symbol) {
        std::string key = exchange.toString() + ":" + symbol;
        
        std::lock_guard<std::mutex> lock(mutex_);
        ++output_counts_[key];
        ++total_output_count_;
    }
    
    void increment_error_count(const std::string& stage) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++error_counts_[stage];
        ++total_error_count_;
    }
    
    void increment_backpressure_count(const std::string& stage) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++backpressure_counts_[stage];
    }
    
    // Set metrics
    void set_queue_size(const std::string& stage, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_sizes_[stage] = size;
    }
    
    // Timing metrics
    void record_processing_time(const std::string& stage, double milliseconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Update running average
        auto& avg = avg_processing_times_[stage];
        avg.count++;
        avg.sum += milliseconds;
        
        // Update min/max
        if (milliseconds < avg.min || avg.count == 1) avg.min = milliseconds;
        if (milliseconds > avg.max) avg.max = milliseconds;
    }
    
    // Get metrics
    size_t get_total_input_count() const {
        return total_input_count_.load();
    }
    
    size_t get_total_output_count() const {
        return total_output_count_.load();
    }
    
    size_t get_total_error_count() const {
        return total_error_count_.load();
    }
    
    size_t get_input_count(const core::ExchangeId& exchange, const std::string& symbol) const {
        std::string key = exchange.toString() + ":" + symbol;
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = input_counts_.find(key);
        return (it != input_counts_.end()) ? it->second : 0;
    }
    
    size_t get_output_count(const core::ExchangeId& exchange, const std::string& symbol) const {
        std::string key = exchange.toString() + ":" + symbol;
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = output_counts_.find(key);
        return (it != output_counts_.end()) ? it->second : 0;
    }
    
    size_t get_error_count(const std::string& stage) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = error_counts_.find(stage);
        return (it != error_counts_.end()) ? it->second : 0;
    }
    
    size_t get_backpressure_count(const std::string& stage) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backpressure_counts_.find(stage);
        return (it != backpressure_counts_.end()) ? it->second : 0;
    }
    
    size_t get_queue_size(const std::string& stage) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = queue_sizes_.find(stage);
        return (it != queue_sizes_.end()) ? it->second : 0;
    }
    
    struct TimingStats {
        size_t count = 0;
        double sum = 0.0;
        double min = std::numeric_limits<double>::max();
        double max = 0.0;
        
        double average() const {
            return count > 0 ? sum / count : 0.0;
        }
    };
    
    TimingStats get_processing_time_stats(const std::string& stage) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = avg_processing_times_.find(stage);
        return (it != avg_processing_times_.end()) ? it->second : TimingStats{};
    }
    
    // Get all stage names
    std::vector<std::string> get_stage_names() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        
        for (const auto& pair : error_counts_) {
            result.push_back(pair.first);
        }
        
        return result;
    }
    
    // Get all exchange/symbol pairs
    std::vector<std::pair<std::string, std::string>> get_exchange_symbols() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<std::string, std::string>> result;
        
        for (const auto& pair : input_counts_) {
            size_t pos = pair.first.find(':');
            if (pos != std::string::npos) {
                std::string exchange = pair.first.substr(0, pos);
                std::string symbol = pair.first.substr(pos + 1);
                result.emplace_back(exchange, symbol);
            }
        }
        
        return result;
    }
    
private:
    // Overall counts
    std::atomic<size_t> total_input_count_{0};
    std::atomic<size_t> total_output_count_{0};
    std::atomic<size_t> total_error_count_{0};
    
    // Per-exchange/symbol counts
    mutable std::mutex mutex_;
    std::unordered_map<std::string, size_t> input_counts_;
    std::unordered_map<std::string, size_t> output_counts_;
    
    // Per-stage metrics
    std::unordered_map<std::string, size_t> error_counts_;
    std::unordered_map<std::string, size_t> backpressure_counts_;
    std::unordered_map<std::string, size_t> queue_sizes_;
    std::unordered_map<std::string, TimingStats> avg_processing_times_;
};

} // namespace data_ingestion
} // namespace trading
