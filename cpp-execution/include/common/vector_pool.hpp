#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <deque>

namespace trading {
namespace common {

/**
 * @brief Memory pool for efficient vector allocations in correlation calculations
 * 
 * This pool pre-allocates chunks of memory to avoid frequent heap allocations
 * during high-frequency correlation calculations.
 */
template<typename T>
class VectorPool {
public:
    explicit VectorPool(size_t max_vector_size = 5000, size_t pool_size = 10)
        : max_vector_size_(max_vector_size), pool_size_(pool_size) {
        
        // Pre-allocate vectors
        for (size_t i = 0; i < pool_size; ++i) {
            auto vec = std::make_unique<std::vector<T>>();
            vec->reserve(max_vector_size_);
            available_vectors_.push_back(std::move(vec));
        }
    }

    /**
     * @brief Get a vector from the pool
     * @return Unique pointer to a pre-allocated vector
     */
    std::unique_ptr<std::vector<T>> getVector() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!available_vectors_.empty()) {
            auto vec = std::move(available_vectors_.back());
            available_vectors_.pop_back();
            vec->clear(); // Clear but keep capacity
            return vec;
        }
        
        // Pool exhausted, create new vector
        auto vec = std::make_unique<std::vector<T>>();
        vec->reserve(max_vector_size_);
        return vec;
    }

    /**
     * @brief Return a vector to the pool
     * @param vec Vector to return (will be moved)
     */
    void returnVector(std::unique_ptr<std::vector<T>> vec) {
        if (!vec) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Only return to pool if we have space and vector is reasonable size
        if (available_vectors_.size() < pool_size_ && 
            vec->capacity() <= max_vector_size_ * 2) {
            vec->clear(); // Keep capacity, clear contents
            available_vectors_.push_back(std::move(vec));
        }
        // Otherwise let it be destroyed (RAII)
    }

    /**
     * @brief Get pool statistics
     */
    struct PoolStats {
        size_t available_count;
        size_t max_vector_size;
        size_t pool_capacity;
    };

    PoolStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {available_vectors_.size(), max_vector_size_, pool_size_};
    }

private:
    const size_t max_vector_size_;
    const size_t pool_size_;
    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<std::vector<T>>> available_vectors_;
};

/**
 * @brief RAII wrapper for vector pool usage
 */
template<typename T>
class PooledVector {
public:
    explicit PooledVector(VectorPool<T>& pool) 
        : pool_(pool), vector_(pool.getVector()) {}
    
    ~PooledVector() {
        if (vector_) {
            pool_.returnVector(std::move(vector_));
        }
    }

    // Move-only semantics
    PooledVector(const PooledVector&) = delete;
    PooledVector& operator=(const PooledVector&) = delete;
    
    PooledVector(PooledVector&& other) noexcept
        : pool_(other.pool_), vector_(std::move(other.vector_)) {}
    
    PooledVector& operator=(PooledVector&& other) noexcept {
        if (this != &other) {
            if (vector_) {
                pool_.returnVector(std::move(vector_));
            }
            vector_ = std::move(other.vector_);
        }
        return *this;
    }

    std::vector<T>& operator*() { return *vector_; }
    const std::vector<T>& operator*() const { return *vector_; }
    
    std::vector<T>* operator->() { return vector_.get(); }
    const std::vector<T>* operator->() const { return vector_.get(); }
    
    std::vector<T>* get() { return vector_.get(); }
    const std::vector<T>* get() const { return vector_.get(); }

private:
    VectorPool<T>& pool_;
    std::unique_ptr<std::vector<T>> vector_;
};

} // namespace common
} // namespace trading

// Usage example in correlation calculation:
/*
class MomentumCorrelation {
private:
    mutable trading::common::VectorPool<double> double_vector_pool_{5000, 20};
    
public:
    void calculateCorrelations() {
        // Use pooled vectors instead of local vectors
        auto priceMovements = trading::common::PooledVector<double>(double_vector_pool_);
        auto timeWeights = trading::common::PooledVector<double>(double_vector_pool_);
        
        priceMovements->reserve(n);
        timeWeights->reserve(n);
        
        // Use priceMovements and timeWeights normally
        for (const auto& dp : snapshotCopy) {
            priceMovements->push_back(dp.price_movement.percent_change);
            // ... etc
        }
        
        // Vectors automatically returned to pool when going out of scope
    }
};
*/
