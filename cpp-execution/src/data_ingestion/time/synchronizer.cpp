/**
 * @file synchronizer.cpp
 * @brief Implementation of timestamp synchronization across exchanges
 *
 * @details
 * Provides NTP-based clock synchronization with drift correction and
 * exchange timestamp normalization for the crypto trading system.
 *
 * Created on: April 30, 2025
 */

#include "time/synchronizer.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>

namespace trading {
namespace data_ingestion {
namespace time {

/**
 * @struct NtpPacket
 * @brief NTP protocol packet structure
 */
struct TimeSynchronizer::NtpPacket {
    uint8_t flags = 0x1B;           // NTP version 3, client mode
    uint8_t stratum = 0;
    uint8_t poll = 0;
    uint8_t precision = 0;
    uint32_t rootDelay = 0;
    uint32_t rootDispersion = 0;
    uint32_t referenceId = 0;
    uint64_t referenceTimestamp = 0;
    uint64_t originateTimestamp = 0;
    uint64_t receiveTimestamp = 0;
    uint64_t transmitTimestamp = 0;
    
    // Convert to network byte order
    void toNetworkOrder();
    void fromNetworkOrder();
};

// Constants
constexpr uint64_t NTP_EPOCH_OFFSET = 2208988800ULL; // Seconds between 1900 and 1970
constexpr double NTP_SCALE = 4294967296.0; // 2^32 for fractional seconds

TimeSynchronizer::TimeSynchronizer(const Config& config) 
    : config_(config) {
    syncStats_.lastSyncTime = SystemClock::now();
}

TimeSynchronizer::~TimeSynchronizer() {
    stop();
}

bool TimeSynchronizer::start() {
    if (isRunning_.load()) {
        return true;
    }
    
    shouldStop_ = false;
    syncThread_ = std::make_unique<std::thread>(&TimeSynchronizer::syncWorker, this);
    isRunning_ = true;
    
    // Perform initial sync
    return forceSync();
}

void TimeSynchronizer::stop() {
    if (!isRunning_.load()) {
        return;
    }
    
    shouldStop_ = true;
    
    if (syncThread_ && syncThread_->joinable()) {
        syncThread_->join();
    }
    
    isRunning_ = false;
}

TimePoint TimeSynchronizer::now() const {
    std::lock_guard<std::mutex> lock(syncMutex_);
    
    auto systemTime = SystemClock::now();
    
    // Apply NTP offset
    auto correctedTime = systemTime + systemClockOffset_;
    
    // Apply drift correction if enabled
    if (config_.enableDriftCorrection && syncStats_.status == SyncStatus::SYNCED) {
        auto timeSinceLastSync = systemTime - lastDriftUpdate_;
        auto driftCorrection = Nanoseconds(static_cast<long long>(
            std::chrono::duration_cast<Nanoseconds>(timeSinceLastSync).count() * 
            clockDriftRate_ / 1e9));
        correctedTime += driftCorrection;
    }
    
    return correctedTime;
}

TimePoint TimeSynchronizer::normalizeExchangeTime(
    const std::string& exchangeId,
    uint64_t exchangeTimestamp,
    const TimePoint& receivedTime) {
    
    // Convert exchange timestamp (assuming milliseconds) to TimePoint
    auto exchangeTime = TimePoint(std::chrono::milliseconds(exchangeTimestamp));
    
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    
    auto it = exchangeProfiles_.find(exchangeId);
    if (it != exchangeProfiles_.end() && it->second.isCalibrated) {
        // Apply exchange-specific offset
        exchangeTime += it->second.averageOffset;
    } else {
        // Fallback: use received time with estimated network delay
        auto networkDelay = std::chrono::milliseconds(50); // Conservative estimate
        return receivedTime - networkDelay;
    }
    
    return exchangeTime;
}

TimePoint TimeSynchronizer::normalizeExchangeTimeMs(
    const std::string& exchangeId,
    uint64_t exchangeTimestampMs,
    const TimePoint& receivedTime) {
    
    return normalizeExchangeTime(exchangeId, exchangeTimestampMs, receivedTime);
}

void TimeSynchronizer::addExchangeTimeSample(
    const std::string& exchangeId,
    uint64_t exchangeTime,
    const TimePoint& localTime) {
    
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    
    auto& profile = exchangeProfiles_[exchangeId];
    profile.exchangeId = exchangeId;
    
    // Calculate offset between exchange and local time
    auto exchangeTimePoint = TimePoint(std::chrono::milliseconds(exchangeTime));
    auto offset = localTime - exchangeTimePoint;
    
    // Add to circular buffer
    profile.offsetSamples[profile.sampleIndex] = 
        std::chrono::duration_cast<Nanoseconds>(offset);
    profile.sampleIndex = (profile.sampleIndex + 1) % ExchangeTimeProfile::MAX_SAMPLES;
    profile.sampleCount = std::min(profile.sampleCount + 1, 
                                  ExchangeTimeProfile::MAX_SAMPLES);
    
    // Recalibrate if we have enough samples
    if (profile.sampleCount >= config_.minSamplesForCalibration) {
        calibrateExchange(exchangeId);
    }
}

bool TimeSynchronizer::forceSync() {
    return performNtpSync();
}

SyncStats TimeSynchronizer::getSyncStats() const {
    std::lock_guard<std::mutex> lock(syncMutex_);
    return syncStats_;
}

std::optional<ExchangeTimeProfile> TimeSynchronizer::getExchangeProfile(
    const std::string& exchangeId) const {
    
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    
    auto it = exchangeProfiles_.find(exchangeId);
    if (it != exchangeProfiles_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void TimeSynchronizer::onSyncStatusChange(std::function<void(SyncStatus)> callback) {
    statusCallbacks_.push_back(std::move(callback));
}

std::string TimeSynchronizer::getStatusString() const {
    std::lock_guard<std::mutex> lock(syncMutex_);
    
    std::ostringstream oss;
    oss << "Status: " << syncStatusToString(syncStats_.status)
        << ", Quality: " << syncQualityToString(syncStats_.quality)
        << ", Offset: " << syncStats_.getAccuracyMicroseconds() << "μs"
        << ", Server: " << syncStats_.ntpServer;
    
    return oss.str();
}

bool TimeSynchronizer::isSynchronized() const {
    std::lock_guard<std::mutex> lock(syncMutex_);
    return syncStats_.status == SyncStatus::SYNCED && 
           (syncStats_.quality == SyncQuality::GOOD || 
            syncStats_.quality == SyncQuality::EXCELLENT);
}

std::chrono::seconds TimeSynchronizer::getTimeUntilNextSync() const {
    std::lock_guard<std::mutex> lock(syncMutex_);
    
    auto timeSinceLastSync = SystemClock::now() - syncStats_.lastSyncTime;
    auto remaining = config_.syncInterval - 
                    std::chrono::duration_cast<std::chrono::seconds>(timeSinceLastSync);
    
    return std::max(remaining, std::chrono::seconds(0));
}
bool TimeSynchronizer::configure(const Config& config) {
    std::lock_guard<std::mutex> lock(syncMutex_);
    
    // Check if running and if changes require restart
    bool needsRestart = isRunning_.load() && 
                       (config.ntpServers != config_.ntpServers ||
                        config.syncInterval != config_.syncInterval);
    
    if (needsRestart) {
        // Temporarily stop for reconfiguration
        stop();
    }
    
    // Apply new configuration
    config_ = config;
    
    // Restart if was running
    if (needsRestart) {
        return start();
    }
    
    return true;
}
// Private methods implementation

void TimeSynchronizer::syncWorker() {
    while (!shouldStop_.load()) {
        performNtpSync();
        
        // Sleep for sync interval or until stop requested
        auto sleepDuration = config_.syncInterval;
        auto start = SystemClock::now();
        
        while (!shouldStop_.load() && 
               (SystemClock::now() - start) < sleepDuration) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool TimeSynchronizer::performNtpSync() {
    std::lock_guard<std::mutex> lock(syncMutex_);
    
    syncStats_.syncAttempts++;
    
    for (const auto& server : config_.ntpServers) {
        if (syncWithServer(server)) {
            syncStats_.successfulSyncs++;
            syncStats_.lastSyncTime = SystemClock::now();
            syncStats_.ntpServer = server;
            
            updateDriftCorrection();
            updateSyncQuality();
            
            auto oldStatus = syncStats_.status;
            syncStats_.status = SyncStatus::SYNCED;
            
            if (oldStatus != SyncStatus::SYNCED) {
                notifyStatusChange(SyncStatus::SYNCED);
            }
            
            return true;
        }
    }
    
    // All servers failed
    auto oldStatus = syncStats_.status;
    syncStats_.status = SyncStatus::ERROR;
    
    if (oldStatus != SyncStatus::ERROR) {
        notifyStatusChange(SyncStatus::ERROR);
    }
    
    return false;
}

bool TimeSynchronizer::syncWithServer(const std::string& server) {
    NtpPacket packet;
    auto sendTime = SystemClock::now();
    
    if (!sendNtpRequest(server, packet)) {
        return false;
    }
    
    auto receiveTime = SystemClock::now();
    
    // Calculate offset and round-trip time
    auto offset = calculateOffset(packet, sendTime, receiveTime);
    auto roundTrip = std::chrono::duration_cast<Nanoseconds>(receiveTime - sendTime);
    
    // Validate the response
    if (std::abs(offset.count()) > config_.maxAllowedOffset.count()) {
        return false; // Offset too large, possibly bad server
    }
    
    // Update sync stats
    systemClockOffset_ = offset;
    syncStats_.currentOffset = offset;
    syncStats_.roundTripTime = roundTrip;
    
    // Update jitter (simplified)
    static Nanoseconds lastOffset{0};
    syncStats_.jitter = Nanoseconds(std::abs(offset.count() - lastOffset.count()));
    lastOffset = offset;
    
    return true;
}

void TimeSynchronizer::updateDriftCorrection() {
    // Simplified drift calculation
    // In production, this would use more sophisticated algorithms
    static TimePoint lastUpdate = SystemClock::now();
    static Nanoseconds lastOffset = systemClockOffset_;
    
    auto now = SystemClock::now();
    auto deltaTime = std::chrono::duration_cast<Nanoseconds>(now - lastUpdate);
    auto deltaOffset = systemClockOffset_ - lastOffset;
    
    if (deltaTime.count() > 0) {
        clockDriftRate_ = static_cast<double>(deltaOffset.count()) / deltaTime.count() * 1e9;
    }
    
    lastUpdate = now;
    lastOffset = systemClockOffset_;
    lastDriftUpdate_ = now;
    syncStats_.driftRate = clockDriftRate_;
}

void TimeSynchronizer::calibrateExchange(const std::string& exchangeId) {
    auto& profile = exchangeProfiles_[exchangeId];
    
    if (profile.sampleCount < config_.minSamplesForCalibration) {
        return;
    }
    
    // Calculate average offset
    auto sum = std::accumulate(
        profile.offsetSamples.begin(),
        profile.offsetSamples.begin() + std::min(profile.sampleCount, 
                                                ExchangeTimeProfile::MAX_SAMPLES),
        Nanoseconds{0});
    
    profile.averageOffset = Nanoseconds(sum.count() / profile.sampleCount);
    
    // Calculate variance
    double variance = 0.0;
    auto sampleEnd = profile.offsetSamples.begin() + 
                    std::min(profile.sampleCount, ExchangeTimeProfile::MAX_SAMPLES);
    
    for (auto it = profile.offsetSamples.begin(); it != sampleEnd; ++it) {
        auto diff = *it - profile.averageOffset;
        variance += diff.count() * diff.count();
    }
    variance /= profile.sampleCount;
    
    profile.offsetVariance = Nanoseconds(static_cast<long long>(std::sqrt(variance)));
    profile.isCalibrated = true;
    profile.lastCalibration = SystemClock::now();
}

void TimeSynchronizer::updateSyncQuality() {
    auto offsetMicros = std::chrono::duration_cast<std::chrono::microseconds>(
        syncStats_.currentOffset.count() < 0 ? -syncStats_.currentOffset : syncStats_.currentOffset).count();
    
    if (offsetMicros < 100) {
        syncStats_.quality = SyncQuality::EXCELLENT;
    } else if (offsetMicros < 1000) {
        syncStats_.quality = SyncQuality::GOOD;
    } else if (offsetMicros < 10000) {
        syncStats_.quality = SyncQuality::FAIR;
    } else {
        syncStats_.quality = SyncQuality::POOR;
    }
}

void TimeSynchronizer::notifyStatusChange(SyncStatus newStatus) {
    for (auto& callback : statusCallbacks_) {
        try {
            callback(newStatus);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

bool TimeSynchronizer::sendNtpRequest(const std::string& server, NtpPacket& response) {
    // Create socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return false;
    }
    
    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = config_.timeoutDuration.count();
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Resolve server address
    struct hostent* host = gethostbyname(server.c_str());
    if (!host) {
        close(sockfd);
        return false;
    }
    
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(123); // NTP port
    memcpy(&serverAddr.sin_addr.s_addr, host->h_addr, host->h_length);
    
    // Prepare NTP packet
    NtpPacket packet;
    packet.originateTimestamp = timePointToNtpTimestamp(SystemClock::now());
    packet.toNetworkOrder();
    
    // Send request
    if (sendto(sockfd, &packet, sizeof(packet), 0, 
               reinterpret_cast<struct sockaddr*>(&serverAddr), 
               sizeof(serverAddr)) < 0) {
        close(sockfd);
        return false;
    }
    
    // Receive response
    socklen_t addrLen = sizeof(serverAddr);
    if (recvfrom(sockfd, &response, sizeof(response), 0,
                 reinterpret_cast<struct sockaddr*>(&serverAddr), 
                 &addrLen) < 0) {
        close(sockfd);
        return false;
    }
    
    response.fromNetworkOrder();
    close(sockfd);
    return true;
}

Nanoseconds TimeSynchronizer::calculateOffset(
    const NtpPacket& packet,
    const TimePoint& sendTime,
    const TimePoint& receiveTime) {
    
    // NTP offset calculation
    // offset = ((T2 - T1) + (T3 - T4)) / 2
    // where:
    // T1 = client send time (originateTimestamp)
    // T2 = server receive time (receiveTimestamp) 
    // T3 = server transmit time (transmitTimestamp)
    // T4 = client receive time
    
    auto t1 = timePointToNtpTimestamp(sendTime);
    auto t2 = packet.receiveTimestamp;
    auto t3 = packet.transmitTimestamp;
    auto t4 = timePointToNtpTimestamp(receiveTime);
    
    // Convert to nanoseconds for high precision
    auto t1_ns = static_cast<int64_t>((t1 >> 32) * 1000000000ULL + 
                                     ((t1 & 0xFFFFFFFF) * 1000000000ULL >> 32));
    auto t2_ns = static_cast<int64_t>((t2 >> 32) * 1000000000ULL + 
                                     ((t2 & 0xFFFFFFFF) * 1000000000ULL >> 32));
    auto t3_ns = static_cast<int64_t>((t3 >> 32) * 1000000000ULL + 
                                     ((t3 & 0xFFFFFFFF) * 1000000000ULL >> 32));
    auto t4_ns = static_cast<int64_t>((t4 >> 32) * 1000000000ULL + 
                                     ((t4 & 0xFFFFFFFF) * 1000000000ULL >> 32));
    
    auto offset_ns = ((t2_ns - t1_ns) + (t3_ns - t4_ns)) / 2;
    
    return Nanoseconds(offset_ns);
}

uint64_t TimeSynchronizer::timePointToNtpTimestamp(const TimePoint& tp) {
    auto duration = tp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto fractional = duration - seconds;
    
    // Add NTP epoch offset
    uint64_t ntpSeconds = seconds.count() + NTP_EPOCH_OFFSET;
    uint64_t ntpFractional = static_cast<uint64_t>(
        std::chrono::duration_cast<Nanoseconds>(fractional).count() * 
        NTP_SCALE / 1000000000.0);
    
    return (ntpSeconds << 32) | (ntpFractional & 0xFFFFFFFF);
}

TimePoint TimeSynchronizer::ntpTimestampToTimePoint(uint64_t ntpTime) {
    uint64_t seconds = (ntpTime >> 32) - NTP_EPOCH_OFFSET;
    uint64_t fractional = ntpTime & 0xFFFFFFFF;
    
    auto nanoseconds = static_cast<uint64_t>(fractional * 1000000000.0 / NTP_SCALE);
    
    return TimePoint(std::chrono::seconds(seconds) + std::chrono::nanoseconds(nanoseconds));
}

std::string TimeSynchronizer::syncStatusToString(SyncStatus status) {
    switch (status) {
        case SyncStatus::UNSYNCED: return "UNSYNCED";
        case SyncStatus::SYNCING:  return "SYNCING";
        case SyncStatus::SYNCED:   return "SYNCED";
        case SyncStatus::DEGRADED: return "DEGRADED";
        case SyncStatus::ERROR:    return "ERROR";
        default:                   return "UNKNOWN";
    }
}

std::string TimeSynchronizer::syncQualityToString(SyncQuality quality) {
    switch (quality) {
        case SyncQuality::POOR:      return "POOR";
        case SyncQuality::FAIR:      return "FAIR";
        case SyncQuality::GOOD:      return "GOOD";
        case SyncQuality::EXCELLENT: return "EXCELLENT";
        default:                     return "UNKNOWN";
    }
}

// NTP packet byte order conversion methods
void TimeSynchronizer::NtpPacket::toNetworkOrder() {
    rootDelay = htonl(rootDelay);
    rootDispersion = htonl(rootDispersion);
    referenceId = htonl(referenceId);
    
    // Convert 64-bit timestamps
    referenceTimestamp = htobe64(referenceTimestamp);
    originateTimestamp = htobe64(originateTimestamp);
    receiveTimestamp = htobe64(receiveTimestamp);
    transmitTimestamp = htobe64(transmitTimestamp);
}

void TimeSynchronizer::NtpPacket::fromNetworkOrder() {
    rootDelay = ntohl(rootDelay);
    rootDispersion = ntohl(rootDispersion);
    referenceId = ntohl(referenceId);
    
    // Convert 64-bit timestamps
    referenceTimestamp = be64toh(referenceTimestamp);
    originateTimestamp = be64toh(originateTimestamp);
    receiveTimestamp = be64toh(receiveTimestamp);
    transmitTimestamp = be64toh(transmitTimestamp);
}

// SynchronizedClock implementation

SynchronizedClock::SynchronizedClock(std::shared_ptr<TimeSynchronizer> synchronizer)
    : synchronizer_(std::move(synchronizer)) {}

TimePoint SynchronizedClock::now() const {
    if (synchronizer_ && synchronizer_->isSynchronized()) {
        return synchronizer_->now();
    }
    return SystemClock::now();
}

bool SynchronizedClock::isSynchronized() const {
    return synchronizer_ && synchronizer_->isSynchronized();
}

double SynchronizedClock::getAccuracyMicroseconds() const {
    if (synchronizer_) {
        return synchronizer_->getSyncStats().getAccuracyMicroseconds();
    }
    return -1.0; // Unknown accuracy
}

// Global synchronizer management

namespace {
    std::unique_ptr<TimeSynchronizer> g_globalSynchronizer;
    std::mutex g_globalMutex;
}

std::shared_ptr<TimeSynchronizer> createTimeSynchronizer(const TimeSynchronizer::Config& config) {
    return std::make_shared<TimeSynchronizer>(config);
}

TimeSynchronizer& getGlobalSynchronizer() {
    std::lock_guard<std::mutex> lock(g_globalMutex);
    
    if (!g_globalSynchronizer) {        
        // Fix - provide a Config argument
        g_globalSynchronizer = std::make_unique<TimeSynchronizer>(TimeSynchronizer::Config{});
        g_globalSynchronizer->start();
    }
    
    return *g_globalSynchronizer;
}

bool initializeGlobalSynchronizer(const TimeSynchronizer::Config& config) {
    std::lock_guard<std::mutex> lock(g_globalMutex);
    
    if (g_globalSynchronizer) {
        g_globalSynchronizer->stop();
    }
    
    g_globalSynchronizer = std::make_unique<TimeSynchronizer>(config);
    return g_globalSynchronizer->start();
}

} // namespace time
} // namespace data_ingestion
} // namespace trading