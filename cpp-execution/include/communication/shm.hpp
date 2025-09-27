#pragma once

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <atomic>
#include <cstring>
#include <string>
#include <memory>
#include <stdexcept>
#include <chrono>
#include <iostream>

// High-performance lock-free ring buffer for protobuf messages
struct MessageHeader {
    uint32_t size;          // Message size
    uint32_t topic_len;     // Topic string length
    uint64_t timestamp_ns;  // Nanosecond timestamp
    uint32_t checksum;      // Simple checksum for integrity
    char topic_data[64];    // Topic string (fixed size for performance)
    // Message data follows immediately after this header
};

struct SharedRingBuffer {
    static constexpr size_t BUFFER_SIZE = 128 * 1024 * 1024; // 128MB ring buffer
    static constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024;  // 1MB max message
    static constexpr size_t HEADER_SIZE = sizeof(MessageHeader);
    
    // Control structure
    std::atomic<uint64_t> write_pos{0};
    std::atomic<uint64_t> read_pos{0};
    std::atomic<uint64_t> total_written{0};
    std::atomic<uint64_t> total_read{0};
    std::atomic<bool> producer_active{true};
    
    // Statistics
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_received{0};
    
    // Ring buffer data
    char data[BUFFER_SIZE];
    
    bool canWrite(size_t message_size) const {
        uint64_t current_write = write_pos.load(std::memory_order_acquire);
        uint64_t current_read = read_pos.load(std::memory_order_acquire);
        
        // Check if we have enough space
        size_t used_space = (current_write >= current_read) 
            ? (current_write - current_read)
            : (BUFFER_SIZE - current_read + current_write);
            
        return (used_space + message_size + HEADER_SIZE) < (BUFFER_SIZE - 1024); // Leave 1KB safety margin
    }
    
    size_t getUsedSpace() const {
        uint64_t current_write = write_pos.load(std::memory_order_acquire);
        uint64_t current_read = read_pos.load(std::memory_order_acquire);
        
        return (current_write >= current_read) 
            ? (current_write - current_read)
            : (BUFFER_SIZE - current_read + current_write);
    }
};

class SharedMemoryPublisher {
private:
    int fd_;
    SharedRingBuffer* buffer_;
    std::string shm_name_;
    bool is_creator_;
    
    uint32_t calculateChecksum(const char* data, size_t len) {
        uint32_t checksum = 0;
        for (size_t i = 0; i < len; ++i) {
            checksum = checksum * 31 + static_cast<unsigned char>(data[i]);
        }
        return checksum;
    }
    
public:
    SharedMemoryPublisher(const std::string& name, bool create = true) 
        : shm_name_(name), is_creator_(create) {
        
        if (create) {
            // Create shared memory
            fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
            if (fd_ == -1) {
                throw std::runtime_error("Failed to create shared memory: " + shm_name_);
            }
            
            if (ftruncate(fd_, sizeof(SharedRingBuffer)) == -1) {
                close(fd_);
                shm_unlink(shm_name_.c_str());
                throw std::runtime_error("Failed to set shared memory size");
            }
        } else {
            // Open existing
            fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
            if (fd_ == -1) {
                throw std::runtime_error("Failed to open shared memory: " + shm_name_);
            }
        }
        
        // Map memory
        buffer_ = static_cast<SharedRingBuffer*>(
            mmap(nullptr, sizeof(SharedRingBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0)
        );
        
        if (buffer_ == MAP_FAILED) {
            close(fd_);
            if (create) shm_unlink(shm_name_.c_str());
            throw std::runtime_error("Failed to map shared memory");
        }
        
        if (create) {
            // Initialize the buffer
            new (buffer_) SharedRingBuffer();
        }
    }
    
    ~SharedMemoryPublisher() {
        if (buffer_) {
            if (is_creator_) {
                buffer_->producer_active.store(false, std::memory_order_release);
            }
            munmap(buffer_, sizeof(SharedRingBuffer));
        }
        if (fd_ != -1) {
            close(fd_);
        }
        if (is_creator_) {
            shm_unlink(shm_name_.c_str());
        }
    }
    bool shutdown() {
        if (is_creator_ && buffer_) {
            buffer_->producer_active.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }
    
    bool publish(const std::string& topic, const std::string& message) {
        if (!buffer_ || message.size() > SharedRingBuffer::MAX_MESSAGE_SIZE) {
            return false;
        }
        
        size_t total_size = SharedRingBuffer::HEADER_SIZE + message.size();
        
        // Check if we have space (with retry and a mild backoff). If consumer lags too far
        // we may forcibly advance read_pos to free space (dropping oldest messages).
        auto start_time = std::chrono::steady_clock::now();
        const auto publish_timeout = std::chrono::milliseconds(5);
        const size_t SAFETY_MARGIN = 1024; // 1KB safety margin
        while (!buffer_->canWrite(total_size)) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed > publish_timeout) {
                // Try to free up space by advancing read_pos (drop oldest messages) as a last resort.
                try {
                    size_t used = buffer_->getUsedSpace();
                    // Compute how many bytes we need to free to fit the new message (include header)
                    size_t needed_total = total_size + SAFETY_MARGIN;
                    if (used + needed_total > SharedRingBuffer::BUFFER_SIZE) {
                        size_t bytes_to_free = (used + needed_total) - SharedRingBuffer::BUFFER_SIZE;
                        uint64_t current_read = buffer_->read_pos.load(std::memory_order_acquire);
                        uint64_t new_read = current_read + bytes_to_free;
                        buffer_->read_pos.store(new_read, std::memory_order_release);
                        buffer_->messages_dropped.fetch_add(1, std::memory_order_relaxed);
                        std::cerr << "SharedMemoryPublisher::publish: forced advance read_pos by " << bytes_to_free << " bytes to free space" << std::endl;
                        // After forcing read_pos, check again quickly
                        if (!buffer_->canWrite(total_size)) {
                            // Still can't write — fail
                            buffer_->messages_dropped.fetch_add(1, std::memory_order_relaxed);
                            return false;
                        }
                        break;
                    } else {
                        // Somehow there should be space soon — give one more short wait
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                } catch (...) {
                    buffer_->messages_dropped.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        
        // Get write position
        uint64_t write_pos = buffer_->write_pos.load(std::memory_order_acquire);
        size_t buffer_pos = write_pos % SharedRingBuffer::BUFFER_SIZE;
        
        // Handle wrap-around
        if (buffer_pos + total_size > SharedRingBuffer::BUFFER_SIZE) {
            // Write to beginning of buffer
            buffer_pos = 0;
            write_pos = (write_pos / SharedRingBuffer::BUFFER_SIZE + 1) * SharedRingBuffer::BUFFER_SIZE;
        }
        
        // Prepare header
        MessageHeader header;
        header.size = static_cast<uint32_t>(message.size());
        header.topic_len = static_cast<uint32_t>(std::min(topic.size(), sizeof(header.topic_data) - 1));
        header.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        header.checksum = calculateChecksum(message.data(), message.size());
        
        // Copy topic (with null termination)
        std::memset(header.topic_data, 0, sizeof(header.topic_data));
        std::memcpy(header.topic_data, topic.data(), header.topic_len);
        
        // Write header and message atomically
        std::memcpy(buffer_->data + buffer_pos, &header, SharedRingBuffer::HEADER_SIZE);
        std::memcpy(buffer_->data + buffer_pos + SharedRingBuffer::HEADER_SIZE, 
                   message.data(), message.size());
        
        // Update write position (release semantics to ensure visibility)
        buffer_->write_pos.store(write_pos + total_size, std::memory_order_release);
        buffer_->messages_sent.fetch_add(1, std::memory_order_relaxed);
        
        return true;
    }
    
    // Get statistics
    struct Stats {
        uint64_t messages_sent;
        uint64_t messages_dropped;
        size_t buffer_used_bytes;
        double buffer_utilization;
    };
    
    Stats getStats() const {
        if (!buffer_) return {};
        
        Stats stats;
        stats.messages_sent = buffer_->messages_sent.load(std::memory_order_relaxed);
        stats.messages_dropped = buffer_->messages_dropped.load(std::memory_order_relaxed);
        stats.buffer_used_bytes = buffer_->getUsedSpace();
        stats.buffer_utilization = static_cast<double>(stats.buffer_used_bytes) / SharedRingBuffer::BUFFER_SIZE;
        
        return stats;
    }
};

class SharedMemorySubscriber {
private:
    int fd_;
    SharedRingBuffer* buffer_;
    std::string shm_name_;
    uint64_t last_read_pos_;
    
    uint32_t calculateChecksum(const char* data, size_t len) {
        uint32_t checksum = 0;
        for (size_t i = 0; i < len; ++i) {
            checksum = checksum * 31 + static_cast<unsigned char>(data[i]);
        }
        return checksum;
    }
    
public:
    struct Message {
        std::string topic;
        std::string data;
        uint64_t timestamp_ns;
    };
    
    SharedMemorySubscriber(const std::string& name) : shm_name_(name), last_read_pos_(0) {
        // Open existing shared memory (need read-write so subscriber can update stats counters)
        fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open shared memory: " + shm_name_);
        }
        
        // Map memory (read-write) - subscriber updates shared counters (messages_received etc.)
        buffer_ = static_cast<SharedRingBuffer*>(
            mmap(nullptr, sizeof(SharedRingBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0)
        );
        
        if (buffer_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to map shared memory");
        }
        
        // Start from current write position to avoid reading old data
        last_read_pos_ = buffer_->write_pos.load(std::memory_order_acquire);
    }
    
    ~SharedMemorySubscriber() {
        if (buffer_) {
            munmap(buffer_, sizeof(SharedRingBuffer));
        }
        if (fd_ != -1) {
            close(fd_);
        }
    }
    bool shutdown() {
        if (buffer_) {
            munmap(buffer_, sizeof(SharedRingBuffer));
            buffer_ = nullptr;
        }
        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }
        return true;
    }

    bool readMessage(Message& msg) {
        if (!buffer_) {
            std::cerr << "SharedMemorySubscriber::readMessage: buffer_ == nullptr" << std::endl;
            return false;
        }

        uint64_t current_write = 0;
        try {
            current_write = buffer_->write_pos.load(std::memory_order_acquire);
        } catch (...) {
            std::cerr << "SharedMemorySubscriber::readMessage: failed to read write_pos" << std::endl;
            return false;
        }

        // Check if there's new data
        if (last_read_pos_ >= current_write) {
            return false; // No new messages
        }

        // Sanity: if the gap is larger than buffer size, assume we missed too many messages and resync
        uint64_t gap = current_write - last_read_pos_;
        if (gap > SharedRingBuffer::BUFFER_SIZE) {
            std::cerr << "SharedMemorySubscriber::readMessage: detected gap > BUFFER_SIZE (gap=" << gap << ") - resyncing to write_pos" << std::endl;
            last_read_pos_ = current_write;
            return false;
        }
        
        size_t buffer_pos = last_read_pos_ % SharedRingBuffer::BUFFER_SIZE;
        
        // Handle potential wrap-around for header read
        MessageHeader header;
        if (buffer_pos + SharedRingBuffer::HEADER_SIZE > SharedRingBuffer::BUFFER_SIZE) {
            // Header wraps around, read in two parts (rare case)
            size_t first_part = SharedRingBuffer::BUFFER_SIZE - buffer_pos;
            std::memcpy(&header, buffer_->data + buffer_pos, first_part);
            std::memcpy(reinterpret_cast<char*>(&header) + first_part, buffer_->data, 
                       SharedRingBuffer::HEADER_SIZE - first_part);
            buffer_pos = SharedRingBuffer::HEADER_SIZE - first_part;
        } else {
            std::memcpy(&header, buffer_->data + buffer_pos, SharedRingBuffer::HEADER_SIZE);
            buffer_pos += SharedRingBuffer::HEADER_SIZE;
        }
        
        // Validate message size and topic length
        if (header.size > SharedRingBuffer::MAX_MESSAGE_SIZE) {
            std::cerr << "SharedMemorySubscriber::readMessage: header.size too large (" << header.size << ") - skipping" << std::endl;
            // Corrupted header, skip to next message boundary
            last_read_pos_ = current_write;
            return false;
        }
        if (header.topic_len > sizeof(header.topic_data)) {
            std::cerr << "SharedMemorySubscriber::readMessage: header.topic_len too large (" << header.topic_len << ") - skipping" << std::endl;
            last_read_pos_ = current_write;
            return false;
        }
        
        // Read message data
        std::string message_data(header.size, '\0');
        if (buffer_pos + header.size > SharedRingBuffer::BUFFER_SIZE) {
            // Message data wraps around
            size_t first_part = SharedRingBuffer::BUFFER_SIZE - buffer_pos;
            std::memcpy(&message_data[0], buffer_->data + buffer_pos, first_part);
            std::memcpy(&message_data[first_part], buffer_->data, header.size - first_part);
        } else {
            std::memcpy(&message_data[0], buffer_->data + buffer_pos, header.size);
        }
        
        // Verify checksum
        uint32_t computed_checksum = calculateChecksum(message_data.data(), message_data.size());
        if (computed_checksum != header.checksum) {
            std::cerr << "SharedMemorySubscriber::readMessage: checksum mismatch (computed=" << computed_checksum << ", header=" << header.checksum << ") - skipping" << std::endl;
            // Corrupted message, skip
            last_read_pos_ += SharedRingBuffer::HEADER_SIZE + header.size;
            return false;
        }
        
        // Success - populate message
        msg.topic = std::string(header.topic_data, header.topic_len);
        msg.data = std::move(message_data);
        msg.timestamp_ns = header.timestamp_ns;
        
        // Update read position
        last_read_pos_ += SharedRingBuffer::HEADER_SIZE + header.size;
        buffer_->messages_received.fetch_add(1, std::memory_order_relaxed);
        
        return true;
    }
    
    // Non-blocking read with timeout
    bool readMessageWithTimeout(Message& msg, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        auto start = std::chrono::steady_clock::now();

        if (!buffer_) return false;

        while (std::chrono::steady_clock::now() - start < timeout) {
            if (readMessage(msg)) {
                return true;
            }

            // Check if producer is still active (guard buffer_ access)
            try {
                if (!buffer_->producer_active.load(std::memory_order_acquire)) {
                    return false;
                }
            } catch (...) {
                // If we cannot read producer_active, bail out
                return false;
            }

            // Small sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }

        return false;
    }
    
    bool isProducerActive() const {
        return buffer_ && buffer_->producer_active.load(std::memory_order_acquire);
    }
    
    struct Stats {
        uint64_t messages_received;
        size_t buffer_used_bytes;
        double buffer_utilization;
    };
    
    Stats getStats() const {
        if (!buffer_) return {};
        
        Stats stats;
        stats.messages_received = buffer_->messages_received.load(std::memory_order_relaxed);
        stats.buffer_used_bytes = buffer_->getUsedSpace();
        stats.buffer_utilization = static_cast<double>(stats.buffer_used_bytes) / SharedRingBuffer::BUFFER_SIZE;
        
        return stats;
    }
};
