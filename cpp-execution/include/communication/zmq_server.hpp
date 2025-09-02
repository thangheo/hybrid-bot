#ifndef ZMQ_SERVER_HPP
#define ZMQ_SERVER_HPP

#include <zmq.hpp>
#include <thread>
#include <chrono>
#include <iostream>
#include <string>
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <random>
#include <cstring>
#include <functional>  // Added for std::function

// Include generated protobuf headers
#include "../proto/generated/cpp/trading_messages.pb.h"

class TradingCommunicationServer {
private:
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> intelligence_publisher_;
    std::unique_ptr<zmq::socket_t> intent_receiver_;
    
    std::atomic<bool> running_;
    std::thread intelligence_thread_;
    std::thread intent_handler_thread_;
    
    // Market intelligence generation
    std::mutex intel_queue_mutex_;
    std::queue<trading::MarketIntelligence> intel_queue_;
    std::condition_variable intel_cv_;
    
    // Execution acknowledgment callback
    std::function<trading::ExecutionAcknowledgment(const trading::TradingIntent&)> execution_callback_;

public:
    TradingCommunicationServer();
    TradingCommunicationServer(const TradingCommunicationServer&) = delete;
    TradingCommunicationServer& operator=(const TradingCommunicationServer&) = delete;
    
    ~TradingCommunicationServer();

    void InitializeSockets();
    void SetExecutionCallback(std::function<trading::ExecutionAcknowledgment(const trading::TradingIntent&)> callback);
    void Start();
    void Stop();

    void PublishMarketIntelligence(const trading::MarketIntelligence& intel);
    void PublishMarketIntelligenceSync(const trading::MarketIntelligence& intel);

private:
    void IntelligencePublisherLoop();
    void IntentHandlerLoop();
    
    // Helper methods
    trading::ExecutionAcknowledgment CreateDefaultAcknowledgment(const trading::TradingIntent& intent);
    void SendErrorAcknowledgment(const std::string& error_reason);
};

#endif // ZMQ_SERVER_HPP