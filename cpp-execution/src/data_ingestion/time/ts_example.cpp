/**
 * @file time_synchronizer_example.cpp
 * @brief Example usage of the TimeSynchronizer class
 * 
 * Demonstrates how to use the time synchronization system in your
 * crypto trading application.
 */

#include "time/synchronizer.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace trading::data_ingestion::time;

int main() {
    std::cout << "=== Time Synchronizer Example ===" << std::endl;
    
    // 1. Create and configure synchronizer
    TimeSynchronizer::Config config;
    config.ntpServers = {"time.cloudflare.com", "time.google.com"};
    config.syncInterval = std::chrono::seconds(60); // Sync every minute
    config.enableDriftCorrection = true;
    config.enableExchangeCalibration = true;
    
    auto synchronizer = createTimeSynchronizer(config);
    
    // 2. Set up status change callback
    synchronizer->onSyncStatusChange([](SyncStatus status) {
        std::cout << "Sync status changed to: " << 
                     (status == SyncStatus::SYNCED ? "SYNCED" :
                      status == SyncStatus::ERROR ? "ERROR" :
                      status == SyncStatus::SYNCING ? "SYNCING" : "OTHER")
                  << std::endl;
    });
    
    // 3. Start synchronization
    if (!synchronizer->start()) {
        std::cerr << "Failed to start time synchronizer!" << std::endl;
        return 1;
    }
    
    std::cout << "Time synchronizer started..." << std::endl;
    
    // 4. Wait for initial sync
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 5. Display sync status
    auto stats = synchronizer->getSyncStats();
    std::cout << "\nSync Statistics:" << std::endl;
    std::cout << "  Status: " << synchronizer->getStatusString() << std::endl;
    std::cout << "  Accuracy: " << stats.getAccuracyMicroseconds() << " μs" << std::endl;
    std::cout << "  Drift Rate: " << stats.driftRate << " ppm" << std::endl;
    
    // 6. Demonstrate time access
    std::cout << "\nTime Comparison:" << std::endl;
    auto systemTime = SystemClock::now();
    auto syncedTime = synchronizer->now();
    
    auto offsetNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        syncedTime - systemTime).count();
    
    std::cout << "  System time:  " << std::chrono::duration_cast<std::chrono::milliseconds>(
        systemTime.time_since_epoch()).count() << " ms" << std::endl;
    std::cout << "  Synced time:  " << std::chrono::duration_cast<std::chrono::milliseconds>(
        syncedTime.time_since_epoch()).count() << " ms" << std::endl;
    std::cout << "  Offset:       " << offsetNs << " ns" << std::endl;
    
    // 7. Simulate exchange timestamp normalization
    std::cout << "\nExchange Timestamp Normalization:" << std::endl;
    
    // Simulate Binance timestamp (current time in milliseconds)
    auto binanceTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        SystemClock::now().time_since_epoch()).count();
    
    // Add some exchange samples for calibration
    for (int i = 0; i < 15; ++i) {
        auto localReceiveTime = SystemClock::now();
        auto simulatedExchangeTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            localReceiveTime.time_since_epoch()).count() - 50; // Simulate 50ms delay
        
        synchronizer->addExchangeTimeSample("binance", simulatedExchangeTime, localReceiveTime);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Normalize a timestamp
    auto receivedTime = SystemClock::now();
    auto normalizedTime = synchronizer->normalizeExchangeTimeMs(
        "binance", binanceTime, receivedTime);
    
    std::cout << "  Original exchange time: " << binanceTime << " ms" << std::endl;
    std::cout << "  Normalized time: " << 
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     normalizedTime.time_since_epoch()).count() << " ms" << std::endl;
    
    // 8. Check exchange calibration
    auto exchangeProfile = synchronizer->getExchangeProfile("binance");
    if (exchangeProfile) {
        std::cout << "  Exchange calibrated: " << exchangeProfile->isCalibrated << std::endl;
        std::cout << "  Average offset: " << 
                     std::chrono::duration_cast<std::chrono::microseconds>(
                         exchangeProfile->averageOffset).count() << " μs" << std::endl;
    }
    
    // 9. Demonstrate SynchronizedClock wrapper
    std::cout << "\nSynchronizedClock Example:" << std::endl;
    SynchronizedClock clock(synchronizer);
    
    std::cout << "  Is synchronized: " << clock.isSynchronized() << std::endl;
    std::cout << "  Accuracy: " << clock.getAccuracyMicroseconds() << " μs" << std::endl;
    
    auto clockTime = clock.now();
    std::cout << "  Clock time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
        clockTime.time_since_epoch()).count() << " ms" << std::endl;
    
    // 10. Performance test
    std::cout << "\nPerformance Test (1000 calls):" << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        volatile auto time = synchronizer->now();
        (void)time; // Suppress unused variable warning
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto avgLatency = std::chrono::duration_cast<std::chrono::nanoseconds>(
        endTime - startTime).count() / 1000;
    
    std::cout << "  Average call latency: " << avgLatency << " ns" << std::endl;
    
    // 11. Test global synchronizer
    std::cout << "\nGlobal Synchronizer Test:" << std::endl;
    
    if (initializeGlobalSynchronizer(config)) {
        auto& globalSync = getGlobalSynchronizer();
        std::cout << "  Global synchronizer initialized" << std::endl;
        std::cout << "  Status: " << globalSync.getStatusString() << std::endl;
    }
    
    // 12. Cleanup
    std::cout << "\nStopping synchronizer..." << std::endl;
    synchronizer->stop();
    
    std::cout << "Example completed successfully!" << std::endl;
    return 0;
}