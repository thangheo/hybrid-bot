#include <zmq.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <iomanip>
#include <sstream>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>

// Simple message structure instead of protobuf
struct TestMessage {
    uint64_t sequence_id;
    std::string topic;
    std::string payload;
    uint64_t timestamp_ms;
    std::vector<std::string> tags;
    
    std::string serialize() const {
        std::ostringstream oss;
        oss << sequence_id << "|" << topic << "|" << payload << "|" << timestamp_ms << "|";
        for (size_t i = 0; i < tags.size(); ++i) {
            if (i > 0) oss << ",";
            oss << tags[i];
        }
        return oss.str();
    }
    
    static TestMessage deserialize(const std::string& data) {
        TestMessage msg;
        // Simple parsing - in production you'd use a proper serializer
        return msg;
    }
};

// ZMQ Configuration
struct ZmqConfig {
    std::string pub_endpoint = "inproc://stress_pub";
    std::string router_endpoint = "inproc://stress_router";
    int32_t pub_sndhwm = 100000;
    int32_t sub_rcvhwm = 100000;
    int32_t pub_sndtimeo = 2000;
    int32_t pub_linger = 0;
};

// Simple ZMQ Server for stress testing
class SimpleZmqServer {
public:
    explicit SimpleZmqServer(const ZmqConfig& config) 
        : config_(config), context_(1), running_(false) {}
    
    ~SimpleZmqServer() { stop(); }
    
    bool start() {
        if (running_.load()) return true;
        
        try {
            pub_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PUB);
            pub_socket_->set(zmq::sockopt::sndhwm, config_.pub_sndhwm);
            pub_socket_->set(zmq::sockopt::sndtimeo, config_.pub_sndtimeo);
            pub_socket_->set(zmq::sockopt::linger, config_.pub_linger);
            pub_socket_->bind(config_.pub_endpoint);
            
            running_ = true;
            return true;
        } catch (const zmq::error_t& e) {
            std::cerr << "Server start error: " << e.what() << std::endl;
            return false;
        }
    }
    
    void stop() {
        running_ = false;
        if (pub_socket_) {
            pub_socket_->close();
            pub_socket_.reset();
        }
    }
    
    bool publish(const std::string& topic, const TestMessage& message) {
        if (!running_.load() || !pub_socket_) return false;
        
        try {
            std::string payload_data = message.serialize();
            
            // Frame 1: Topic
            zmq::message_t topic_frame(topic.size());
            memcpy(topic_frame.data(), topic.c_str(), topic.size());
            
            // Frame 2: Payload
            zmq::message_t payload_frame(payload_data.size());
            memcpy(payload_frame.data(), payload_data.c_str(), payload_data.size());
            
            auto result1 = pub_socket_->send(topic_frame, zmq::send_flags::sndmore);
            auto result2 = pub_socket_->send(payload_frame, zmq::send_flags::none);
            
            if (result1 && result2) {
                messages_published_++;
                return true;
            } else {
                publish_failures_++;
                return false;
            }
        } catch (const zmq::error_t& e) {
            publish_failures_++;
            return false;
        }
    }
    
    uint64_t get_published_count() const { return messages_published_.load(); }
    uint64_t get_failure_count() const { return publish_failures_.load(); }

private:
    ZmqConfig config_;
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> pub_socket_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> messages_published_{0};
    std::atomic<uint64_t> publish_failures_{0};
};

// Simple ZMQ Client for stress testing
class SimpleZmqClient {
public:
    explicit SimpleZmqClient(const ZmqConfig& config) 
        : config_(config), context_(1), running_(false) {}
    
    ~SimpleZmqClient() { disconnect(); }
    
    bool connect() {
        if (running_.load()) return true;
        
        try {
            sub_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_SUB);
            sub_socket_->set(zmq::sockopt::rcvhwm, config_.sub_rcvhwm);
            sub_socket_->set(zmq::sockopt::rcvtimeo, 1000);
            sub_socket_->connect(config_.pub_endpoint);
            
            running_ = true;
            
            // Start subscriber thread
            subscriber_thread_ = std::thread([this]() { subscriber_worker(); });
            
            // Warm-up delay
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return true;
        } catch (const zmq::error_t& e) {
            std::cerr << "Client connect error: " << e.what() << std::endl;
            return false;
        }
    }
    
    void disconnect() {
        running_ = false;
        if (subscriber_thread_.joinable()) {
            subscriber_thread_.join();
        }
        if (sub_socket_) {
            sub_socket_->close();
            sub_socket_.reset();
        }
    }
    
    bool subscribe(const std::string& topic) {
        if (!sub_socket_) return false;
        try {
            sub_socket_->set(zmq::sockopt::subscribe, topic);
            return true;
        } catch (const zmq::error_t& e) {
            return false;
        }
    }
    
    uint64_t get_received_count() const { return messages_received_.load(); }
    uint64_t get_receive_failures() const { return receive_failures_.load(); }

private:
    void subscriber_worker() {
        while (running_.load()) {
            try {
                zmq::message_t topic_frame;
                zmq::message_t payload_frame;
                
                auto result1 = sub_socket_->recv(topic_frame, zmq::recv_flags::dontwait);
                if (!result1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                
                auto result2 = sub_socket_->recv(payload_frame, zmq::recv_flags::none);
                if (result2) {
                    messages_received_++;
                }
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {
                    receive_failures_++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    
    ZmqConfig config_;
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> sub_socket_;
    std::atomic<bool> running_;
    std::thread subscriber_thread_;
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> receive_failures_{0};
};
class StressTest
{
    
public:
    struct Config {
        int target_rate_per_second = 400;
        int duration_seconds = 5;
        int num_topics = 3;
        int payload_size_bytes = 1024;
        bool verbose = false;
        bool use_metadata = true;
    };
    
    StressTest(const Config& config) : config_(config) {}
    
    bool run() {
        std::cout << "\n=== ZeroMQ Protobuf Stress Test ===" << std::endl;
        std::cout << "Target rate: " << config_.target_rate_per_second << " msg/s" << std::endl;
        std::cout << "Duration: " << config_.duration_seconds << " seconds" << std::endl;
        std::cout << "Topics: " << config_.num_topics << std::endl;
        std::cout << "Payload size: " << config_.payload_size_bytes << " bytes" << std::endl;
        std::cout << "Using inproc:// transport (same-process)" << std::endl;
        std::cout << std::endl;
        
        // Setup ZMQ configuration for stress testing
        ZmqConfig zmq_config;
        zmq_config.set_stress_test_mode();
        
        // Create shared context for inproc communication
        auto context = std::make_shared<zmq::context_t>(1);
        
        // Create server and client
        ZmqServer server(zmq_config);
        ZmqClient client(zmq_config);
        
        // Setup statistics tracking
        std::atomic<uint64_t> messages_received{0};
        std::vector<std::string> topics;
        for (int i = 0; i < config_.num_topics; ++i) {
            topics.push_back("topic_" + std::to_string(i));
        }
        
        // Start server
        if (!server.start()) {
            std::cerr << "Failed to start server" << std::endl;
            return false;
        }
        
        // Start client and setup subscriptions
        if (!client.connect()) {
            std::cerr << "Failed to connect client" << std::endl;
            return false;
        }
        
        // Subscribe to all topics
        for (const auto& topic : topics) {
            client.subscribe(topic, [&messages_received, this](
                const std::string& recv_topic,
                const google::protobuf::Message& message,
                const nlohmann::json& metadata) {
                
                messages_received++;
                
                if (config_.verbose) {
                    std::cout << "Received: " << recv_topic 
                              << " (seq: " << messages_received.load() << ")" << std::endl;
                }
            });
        }
        
        // Give subscriptions time to warm up
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Calculate timing
        auto interval = std::chrono::microseconds(1000000 / config_.target_rate_per_second);
        auto total_messages = config_.target_rate_per_second * config_.duration_seconds;
        
        std::cout << "Starting message publishing..." << std::endl;
        std::cout << "Expected total messages: " << total_messages << std::endl;
        std::cout << "Interval between messages: " << interval.count() << " μs" << std::endl;
        std::cout << std::endl;
        
        // Start publishing
        auto start_time = std::chrono::steady_clock::now();
        uint64_t messages_sent = 0;
        
        for (int i = 0; i < total_messages && server.is_running(); ++i) {
            auto message_start = std::chrono::steady_clock::now();
            
            // Create test message
            zmq_test::TestMessage test_msg;
            test_msg.set_sequence_id(i);
            test_msg.set_topic(topics[i % topics.size()]);
            test_msg.set_timestamp_ms(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            
            // Create payload of specified size
            std::string payload(config_.payload_size_bytes, 'A' + (i % 26));
            test_msg.set_payload(payload);
            test_msg.add_tags("stress_test");
            test_msg.add_tags("sequence_" + std::to_string(i));
            
            // Optional metadata
            nlohmann::json metadata;
            if (config_.use_metadata) {
                metadata["sequence"] = i;
                metadata["timestamp"] = test_msg.timestamp_ms();
                metadata["test_run"] = "stress_test_1";
            }
            
            // Publish message
            bool success = server.publish(test_msg.topic(), test_msg, metadata);
            if (success) {
                messages_sent++;
            }
            
            // Rate limiting
            auto elapsed = std::chrono::steady_clock::now() - message_start;
            if (elapsed < interval) {
                std::this_thread::sleep_for(interval - elapsed);
            }
            
            // Progress reporting
            if (i > 0 && i % (config_.target_rate_per_second / 2) == 0) {
                auto current_time = std::chrono::steady_clock::now();
                auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                    current_time - start_time).count();
                
                std::cout << "Progress: " << i << "/" << total_messages 
                          << " (" << std::fixed << std::setprecision(1) 
                          << (100.0 * i / total_messages) << "%) "
                          << "Sent: " << messages_sent 
                          << " Received: " << messages_received.load()
                          << " Time: " << elapsed_seconds << "s" << std::endl;
            }
        }
        
        auto end_time = std::chrono::steady_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        std::cout << "\nPublishing completed. Waiting for remaining messages..." << std::endl;
        
        // Wait a bit for remaining messages to be received
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Final statistics
        auto server_stats = server.get_stats();
        auto client_stats = client.get_stats();
        
        std::cout << "\n=== Final Results ===" << std::endl;
        std::cout << "Test duration: " << total_duration.count() << " ms" << std::endl;
        std::cout << "Messages sent (API): " << messages_sent << std::endl;
        std::cout << "Messages published (server): " << server_stats.messages_published.load() << std::endl;
        std::cout << "Messages received (client): " << messages_received.load() << std::endl;
        std::cout << "Publish failures: " << server_stats.publish_failures.load() << std::endl;
        std::cout << "Messages dropped: " << server_stats.messages_dropped.load() << std::endl;
        std::cout << "Receive failures: " << client_stats.receive_failures.load() << std::endl;
        
        // Calculate delivery rate
        double delivery_rate = 0.0;
        if (server_stats.messages_published.load() > 0) {
            delivery_rate = (100.0 * messages_received.load()) / server_stats.messages_published.load();
        }
        
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Delivery rate: " << delivery_rate << "%" << std::endl;
        
        // Calculate actual throughput
        double actual_rate = (1000.0 * server_stats.messages_published.load()) / total_duration.count();
        std::cout << "Actual publish rate: " << std::setprecision(1) << actual_rate << " msg/s" << std::endl;
        
        double receive_rate = (1000.0 * messages_received.load()) / total_duration.count();
        std::cout << "Actual receive rate: " << receive_rate << " msg/s" << std::endl;
        
        // Test criteria
        bool success = delivery_rate >= 97.0 && actual_rate >= (config_.target_rate_per_second * 0.9);
        
        std::cout << "\n=== Test Result ===" << std::endl;
        std::cout << "Target: ≥97% delivery, ≥" << (config_.target_rate_per_second * 0.9) << " msg/s" << std::endl;
        std::cout << "Result: " << (success ? "PASS" : "FAIL") << std::endl;
        
        if (!success) {
            std::cout << "Issues:" << std::endl;
            if (delivery_rate < 97.0) {
                std::cout << "  - Delivery rate too low: " << delivery_rate << "% < 97%" << std::endl;
            }
            if (actual_rate < (config_.target_rate_per_second * 0.9)) {
                std::cout << "  - Publish rate too low: " << actual_rate 
                          << " < " << (config_.target_rate_per_second * 0.9) << std::endl;
            }
        }
        
        // Cleanup
        std::cout << "\nCleaning up..." << std::endl;
        client.disconnect();
        server.stop();
        
        return success;
    }

private:
    Config config_;
};

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --rate <rate>       Target rate (msg/s, default: 400)" << std::endl;
    std::cout << "  --duration <sec>    Duration in seconds (default: 5)" << std::endl;
    std::cout << "  --topics <count>    Number of topics (default: 3)" << std::endl;
    std::cout << "  --payload <bytes>   Payload size in bytes (default: 1024)" << std::endl;
    std::cout << "  --verbose           Enable verbose output" << std::endl;
    std::cout << "  --no-metadata       Disable metadata frames" << std::endl;
    std::cout << "  --help              Show this help" << std::endl;
}

int main(int argc, char* argv[]) {
    StressTest::Config config;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--rate" && i + 1 < argc) {
            config.target_rate_per_second = std::stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            config.duration_seconds = std::stoi(argv[++i]);
        } else if (arg == "--topics" && i + 1 < argc) {
            config.num_topics = std::stoi(argv[++i]);
        } else if (arg == "--payload" && i + 1 < argc) {
            config.payload_size_bytes = std::stoi(argv[++i]);
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--no-metadata") {
            config.use_metadata = false;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Validate configuration
    if (config.target_rate_per_second <= 0 || config.duration_seconds <= 0) {
        std::cerr << "Rate and duration must be positive" << std::endl;
        return 1;
    }
    
    if (config.num_topics <= 0) {
        std::cerr << "Number of topics must be positive" << std::endl;
        return 1;
    }
    
    try {
        StressTest test(config);
        bool success = test.run();
        return success ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}