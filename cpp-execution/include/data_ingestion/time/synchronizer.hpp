/**
 * @file synchronizer.hpp
 * @brief Timestamp synchronization across exchanges
 *
 * @details
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * Provides NTP-based clock synchronization with drift correction and exchange
 * timestamp normalization for accurate order book reconstruction.
 *
 * Created on: April 30, 2025
 */
#pragma once

#include <chrono>
#include <string>
#include <map>
#include <atomic>
#include <mutex>
#include <thread>
#include <memory>
#include <optional>
#include <functional>

namespace trading {
namespace data_ingestion {
namespace time {

using SystemClock = std::chrono::system_clock;
using TimePoint = SystemClock::time_point;
using Duration = SystemClock::duration;
using Nanoseconds = std::chrono::nanoseconds;

/**
 * @enum SyncStatus
 * @brief Status of time synchronization
 */
enum class SyncStatus {
    UNSYNCED,
    SYNCING, 
    SYNCED,
    DEGRADED,
    ERROR
};

/**
 * @enum SyncQuality
 * @brief Quality assessment of time synchronization
 */
enum class SyncQuality {
    POOR,      // >10ms drift
    FAIR,      // 1-10ms drift
    GOOD,      // 100μs-1ms drift
    EXCELLENT  // <100μs drift
};

/**
 * @struct SyncStats
 * @brief Statistics about synchronization performance
 */
struct SyncStats {
    SyncStatus status = SyncStatus::UNSYNCED;
    SyncQuality quality = SyncQuality::POOR;
    Nanoseconds currentOffset{0};
    Nanoseconds roundTripTime{0};
    Nanoseconds jitter{0};
    double driftRate = 0.0;  // Parts per million
    TimePoint lastSyncTime;
    int syncAttempts = 0;
    int successfulSyncs = 0;
    std::string ntpServer = "";
    
    double getAccuracyMicroseconds() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::abs(currentOffset)).count();
    }
};

/**
 * @struct ExchangeTimeProfile
 * @brief Time profile for a specific exchange
 */
struct ExchangeTimeProfile {
    std::string exchangeId;
    Nanoseconds averageOffset{0};
    Nanoseconds offsetVariance{0};
    double clockDriftRate = 0.0;
    TimePoint lastCalibration;
    bool isCalibrated = false;
    size_t sampleCount = 0;
    
    // Circular buffer for offset samples
    static constexpr size_t MAX_SAMPLES = 100;
    std::array<Nanoseconds, MAX_SAMPLES> offsetSamples{};
    size_t sampleIndex = 0;
};

/**
 * @class TimeSynchronizer
 * @brief High-precision time synchronization for trading systems
 * 
 * Provides NTP-based system clock synchronization with exchange-specific
 * drift correction and timestamp normalization capabilities.
 */
class TimeSynchronizer {
public:
    /**
     * @brief Configuration for the time synchronizer
     */
    struct Config {
        std::vector<std::string> ntpServers = {
            "time.cloudflare.com",
            "time.google.com", 
            "pool.ntp.org"
        };
        std::chrono::seconds syncInterval{300};        // 5 minutes
        std::chrono::seconds timeoutDuration{5};       // 5 seconds
        int maxRetries = 3;
        Nanoseconds maxAllowedOffset{std::chrono::milliseconds(100)};
        bool enableDriftCorrection = true;
        bool enableExchangeCalibration = true;
        std::chrono::minutes calibrationInterval{60}; // 1 hour
        size_t minSamplesForCalibration = 10;
    };

    /**
     * @brief Constructor
     * @param config Configuration parameters
     */
    explicit TimeSynchronizer(const Config&);
    
    /**
     * @brief Destructor
     */
    ~TimeSynchronizer();
    
    // Non-copyable, non-movable
    TimeSynchronizer(const TimeSynchronizer&) = delete;
    TimeSynchronizer& operator=(const TimeSynchronizer&) = delete;
    TimeSynchronizer(TimeSynchronizer&&) = delete;
    TimeSynchronizer& operator=(TimeSynchronizer&&) = delete;

    /**
     * @brief Start the synchronization service
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop the synchronization service
     */
    void stop();
    
    /**
     * @brief Check if synchronizer is running
     */
    bool isRunning() const { return isRunning_.load(); }

    /**
     * @brief Get current synchronized time
     * @return Current time adjusted for NTP offset and drift
     */
    TimePoint now() const;
    
    /**
     * @brief Get raw system time (without sync correction)
     * @return Raw system clock time
     */
    TimePoint systemNow() const { return SystemClock::now(); }

    /**
     * @brief Normalize exchange timestamp to synchronized time
     * @param exchangeId Exchange identifier
     * @param exchangeTimestamp Timestamp from exchange
     * @param receivedTime Local time when data was received
     * @return Normalized timestamp
     */
    TimePoint normalizeExchangeTime(
        const std::string& exchangeId,
        uint64_t exchangeTimestamp,
        const TimePoint& receivedTime);
    
    /**
     * @brief Normalize exchange timestamp (milliseconds)
     * @param exchangeId Exchange identifier  
     * @param exchangeTimestampMs Timestamp from exchange in milliseconds
     * @param receivedTime Local time when data was received
     * @return Normalized timestamp
     */
    TimePoint normalizeExchangeTimeMs(
        const std::string& exchangeId,
        uint64_t exchangeTimestampMs,
        const TimePoint& receivedTime);

    /**
     * @brief Add time sample for exchange calibration
     * @param exchangeId Exchange identifier
     * @param exchangeTime Exchange timestamp
     * @param localTime Local time when received
     */
    void addExchangeTimeSample(
        const std::string& exchangeId,
        uint64_t exchangeTime,
        const TimePoint& localTime);

    /**
     * @brief Force immediate synchronization
     * @return true if sync was successful
     */
    bool forceSync();

    /**
     * @brief Get synchronization statistics
     * @return Current sync stats
     */
    SyncStats getSyncStats() const;
    
    /**
     * @brief Get exchange time profile
     * @param exchangeId Exchange identifier
     * @return Exchange time profile if available
     */
    std::optional<ExchangeTimeProfile> getExchangeProfile(const std::string& exchangeId) const;

    /**
     * @brief Register callback for sync status changes
     * @param callback Function to call when sync status changes
     */
    void onSyncStatusChange(std::function<void(SyncStatus)> callback);

    /**
     * @brief Get human-readable sync status
     * @return Status string
     */
    std::string getStatusString() const;

    /**
     * @brief Check if system is properly synchronized
     * @return true if sync quality is good or excellent
     */
    bool isSynchronized() const;

    /**
     * @brief Get time until next sync attempt
     * @return Duration until next sync
     */
    std::chrono::seconds getTimeUntilNextSync() const;

    /**
     * @brief Runtime configuration update
     * @param config New configuration settings
     * @return true if configuration was applied successfully
     */
    bool configure(const Config& config);
        
private:
    Config config_;
    
    // Synchronization state
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> shouldStop_{false};
    std::unique_ptr<std::thread> syncThread_;
    
    // NTP synchronization data
    mutable std::mutex syncMutex_;
    SyncStats syncStats_;
    Nanoseconds systemClockOffset_{0};
    double clockDriftRate_ = 0.0;  // ppm
    TimePoint lastDriftUpdate_;
    
    // Exchange calibration data
    mutable std::mutex exchangeMutex_;
    std::map<std::string, ExchangeTimeProfile> exchangeProfiles_;
    
    // Callbacks
    std::vector<std::function<void(SyncStatus)>> statusCallbacks_;
    
    // Internal methods
    void syncWorker();
    bool performNtpSync();
    bool syncWithServer(const std::string& server);
    void updateDriftCorrection();
    void calibrateExchange(const std::string& exchangeId);
    void updateSyncQuality();
    void notifyStatusChange(SyncStatus newStatus);
    
            // Add method to apply config changes
    void applyConfiguration(const Config& newConfig);
    // NTP packet handling
    struct NtpPacket;
    bool sendNtpRequest(const std::string& server, NtpPacket& response);
    Nanoseconds calculateOffset(const NtpPacket& packet, 
                               const TimePoint& sendTime,
                               const TimePoint& receiveTime);
    
    // Utility methods
    static uint64_t timePointToNtpTimestamp(const TimePoint& tp);
    static TimePoint ntpTimestampToTimePoint(uint64_t ntpTime);
    static std::string syncStatusToString(SyncStatus status);
    static std::string syncQualityToString(SyncQuality quality);
};

/**
 * @class SynchronizedClock
 * @brief RAII wrapper for synchronized time access
 * 
 * Provides convenient access to synchronized time with automatic
 * fallback to system time if synchronization fails.
 */
class SynchronizedClock {
public:
    explicit SynchronizedClock(std::shared_ptr<TimeSynchronizer> synchronizer);
    
    /**
     * @brief Get current synchronized time
     * @return Current time (synchronized if available, system time otherwise)
     */
    TimePoint now() const;
    
    /**
     * @brief Check if clock is synchronized
     */
    bool isSynchronized() const;
    
    /**
     * @brief Get sync accuracy in microseconds
     */
    double getAccuracyMicroseconds() const;

private:
    std::shared_ptr<TimeSynchronizer> synchronizer_;
};

/**
 * @brief Create a shared time synchronizer instance
 * @param config Synchronizer configuration
 * @return Shared pointer to time synchronizer
 */
std::shared_ptr<TimeSynchronizer> createTimeSynchronizer(
    const TimeSynchronizer::Config& config = TimeSynchronizer::Config{});

/**
 * @brief Get global time synchronizer instance (singleton)
 * @return Reference to global synchronizer
 */
TimeSynchronizer& getGlobalSynchronizer();

/**
 * @brief Initialize global time synchronizer
 * @param config Configuration for the global synchronizer
 * @return true if initialized successfully
 */
bool initializeGlobalSynchronizer(const TimeSynchronizer::Config& config = TimeSynchronizer::Config{});

} // namespace time
} // namespace data_ingestion
} // namespace trading