#include "communication/zmq_server.hpp"

TradingCommunicationServer::TradingCommunicationServer() : context_(1), running_(false) {
    InitializeSockets();
}

TradingCommunicationServer::~TradingCommunicationServer() {
    Stop();
}

void TradingCommunicationServer::InitializeSockets() {
    try {
        // Market Intelligence Publisher (PUB socket)
        intelligence_publisher_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PUB);
        intelligence_publisher_->bind("tcp://*:5555");
        
        // Trading Intent Receiver (REP socket) 
        intent_receiver_ = std::make_unique<zmq::socket_t>(context_, ZMQ_REP);
        intent_receiver_->bind("tcp://*:5556");
        
        std::cout << "ZMQ Server initialized successfully" << std::endl;
        std::cout << "Market Intelligence Publisher: tcp://*:5555" << std::endl;
        std::cout << "Trading Intent Receiver: tcp://*:5556" << std::endl;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to initialize ZMQ sockets: " << e.what() << std::endl;
        throw;
    }
}

void TradingCommunicationServer::SetExecutionCallback(std::function<trading::ExecutionAcknowledgment(const trading::TradingIntent&)> callback) {
    execution_callback_ = callback;
}

void TradingCommunicationServer::Start() {
    running_ = true;
    
    // Start market intelligence publishing thread
    intelligence_thread_ = std::thread([this]() {
        this->IntelligencePublisherLoop();
    });
    
    // Start trading intent handler thread
    intent_handler_thread_ = std::thread([this]() {
        this->IntentHandlerLoop();
    });
    
    std::cout << "Trading communication server started" << std::endl;
}

void TradingCommunicationServer::Stop() {
    running_ = false;
    intel_cv_.notify_all();
    
    if (intelligence_thread_.joinable()) {
        intelligence_thread_.join();
    }
    if (intent_handler_thread_.joinable()) {
        intent_handler_thread_.join();
    }
    
    std::cout << "Trading communication server stopped" << std::endl;
}

void TradingCommunicationServer::PublishMarketIntelligence(const trading::MarketIntelligence& intel) {
    {
        std::lock_guard<std::mutex> lock(intel_queue_mutex_);
        intel_queue_.push(intel);
    }
    intel_cv_.notify_one();
}

void TradingCommunicationServer::PublishMarketIntelligenceSync(const trading::MarketIntelligence& intel) {
    try {
        std::string serialized = intel.SerializeAsString();
        
        // Send topic first, then data
        zmq::message_t topic_msg(strlen("MARKET_INTEL"));
        memcpy(topic_msg.data(), "MARKET_INTEL", strlen("MARKET_INTEL"));
        
        zmq::message_t data_msg(serialized.size());
        memcpy(data_msg.data(), serialized.c_str(), serialized.size());
        
        intelligence_publisher_->send(topic_msg, zmq::send_flags::sndmore);
        intelligence_publisher_->send(data_msg, zmq::send_flags::none);
        
    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to publish market intelligence: " << e.what() << std::endl;
    }
}

void TradingCommunicationServer::IntelligencePublisherLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(intel_queue_mutex_);
        
        // Wait for market intelligence to publish
        intel_cv_.wait(lock, [this]() { 
            return !intel_queue_.empty() || !running_; 
        });
        
        if (!running_) break;
        
        // Process all pending market intelligence
        while (!intel_queue_.empty()) {
            trading::MarketIntelligence intel = intel_queue_.front();
            intel_queue_.pop();
            lock.unlock();
            
            PublishMarketIntelligenceSync(intel);
            
            lock.lock();
        }
    }
}

void TradingCommunicationServer::IntentHandlerLoop() {
    while (running_) {
        try {
            // Set timeout for non-blocking receive
            int timeout_ms = 100;
            intent_receiver_->set(zmq::sockopt::rcvtimeo, timeout_ms);
            
            zmq::message_t request;
            auto result = intent_receiver_->recv(request, zmq::recv_flags::dontwait);
            
            if (result) {
                // Parse trading intent
                trading::TradingIntent intent;
                std::string data(static_cast<char*>(request.data()), request.size());
                
                if (intent.ParseFromString(data)) {
                    std::cout << "Received trading intent for " << intent.symbol() 
                             << " (ID: " << intent.intent_id() << ")" << std::endl;
                    
                    // Process intent and generate acknowledgment
                    trading::ExecutionAcknowledgment ack;
                    if (execution_callback_) {
                        ack = execution_callback_(intent);
                    } else {
                        // Default acknowledgment
                        ack = CreateDefaultAcknowledgment(intent);
                    }
                    
                    // Send acknowledgment back
                    std::string ack_data = ack.SerializeAsString();
                    zmq::message_t reply(ack_data.size());
                    memcpy(reply.data(), ack_data.c_str(), ack_data.size());
                    intent_receiver_->send(reply, zmq::send_flags::none);
                    
                } else {
                    std::cerr << "Failed to parse trading intent" << std::endl;
                    SendErrorAcknowledgment("Failed to parse message");
                }
            }
            
            // Small sleep to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {  // EAGAIN is expected for timeout
                std::cerr << "ZMQ error in intent handler: " << e.what() << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

trading::ExecutionAcknowledgment TradingCommunicationServer::CreateDefaultAcknowledgment(const trading::TradingIntent& intent) {
    trading::ExecutionAcknowledgment ack;
    ack.set_intent_id(intent.intent_id());
    ack.set_accepted(true);
    ack.set_execution_id("exec_" + intent.intent_id());
    ack.set_reason("Accepted for execution");
    ack.set_estimated_completion_time(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count() + intent.max_execution_time_ms()
    );
    
    // Set default execution parameters
    trading::ExecutionParameters* params = ack.mutable_execution_params();
    params->set_chunk_size_ratio(0.2);
    params->set_aggression_level(0.5);
    params->set_max_chunk_delay_ms(2000);
    params->set_price_improvement_threshold(0.001);
    params->set_use_hidden_orders(false);
    params->set_max_participation_rate(0.15);
    params->set_preferred_order_type(trading::ORDER_LIMIT);
    params->set_post_only(true);
    params->set_time_in_force_ms(30000);
    
    return ack;
}

void TradingCommunicationServer::SendErrorAcknowledgment(const std::string& error_reason) {
    trading::ExecutionAcknowledgment ack;
    ack.set_intent_id("unknown");
    ack.set_accepted(false);
    ack.set_reason(error_reason);
    
    std::string ack_data = ack.SerializeAsString();
    zmq::message_t reply(ack_data.size());
    memcpy(reply.data(), ack_data.c_str(), ack_data.size());
    intent_receiver_->send(reply, zmq::send_flags::none);
}