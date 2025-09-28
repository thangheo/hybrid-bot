#pragma once
#include <vector>
#include <mutex>
#include <optional>

namespace trading {
    namespace common {
        // A simple thread-safe circular buffer
template <typename T>
class CircularBuffer {
public:
    explicit CircularBuffer(size_t capacity)
        : capacity_(capacity), buffer_(capacity) {}

    // Push element (overwrite oldest if full)
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_[head_] = item;
        head_ = (head_ + 1) % capacity_;
        if (size_ < capacity_) {
            ++size_;
        } else {
            tail_ = (tail_ + 1) % capacity_; // overwrite oldest
        }
    }

    void push(T&& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_[head_] = std::move(item);
        head_ = (head_ + 1) % capacity_;
        if (size_ < capacity_) {
            ++size_;
        } else {
            tail_ = (tail_ + 1) % capacity_;
        }
    }

    // Copy out a snapshot (safe under lock)
    std::vector<T> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<T> result;
        result.reserve(size_);
        for (size_t i = 0; i < size_; ++i) {
            size_t idx = (tail_ + i) % capacity_;
            result.push_back(buffer_[idx]);
        }
        return result;
    }

    // Snapshot of raw pointers (cheaper than copy)
    std::vector<const T*> snapshotPtrs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<const T*> result;
        result.reserve(size_);
        for (size_t i = 0; i < size_; ++i) {
            size_t idx = (tail_ + i) % capacity_;
            result.push_back(&buffer_[idx]);
        }
        return result;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    size_t capacity() const {
        return capacity_;
    }

    bool empty() const {
        return size() == 0;
    }

private:
    const size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<T> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;
};

    } // namespace common
} // namespace trading