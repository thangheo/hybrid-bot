#include "communication/zmq_server_client.hpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <cmath>
#include <iomanip>

using namespace trading::communication;
class ZmqStressTest {
public:
    struct TestConfig {
        std::string transport = "inproc";  // inproc, ipc, tcp
        int message_rate = 4000;           // messages per second
        int duration_seconds = 5;         // test duration
        int num_topics = 5;              // number of different topics
        int payload_size = 300;          // bytes
        bool use_metadata = true;
        bool mixed_priorities = true;
        double target_delivery_rate = 0.97;  // 97%
        
        // Transport-specific settings
        std::string tcp_host = "127.0.0.1";
        int tcp_pub_port = 5555;
        int tcp_rep_port = 5556;
        std::string ipc_path = "/tmp/zmq_test";
        std::string inproc_address = "test";
        std::shared_ptr<zmq::context_t> shared_ctx = nullptr;
    };

    explicit ZmqStressTest(const TestConfig& config) : config_(config) {}

    bool runTest() {
        std::cout << "Starting ZMQ Stress Test\n";
        std::cout << "========================\n";
        std::cout << "Transport: " << config_.transport << "\n";
        std::cout << "Message rate: " << config_.message_rate << " msg/s\n";
        std::cout << "Duration: " << config_.duration_seconds << " seconds\n";
        std::cout << "Target delivery rate: " << (config_.target_delivery_rate * 100) << "%\n\n";

        // Setup endpoints based on transport
        // If using inproc transport we must ensure server and client share the
        // same zmq::context_t instance. Create and inject a shared context if
        // the test config didn't already provide one.
        if (config_.transport == "inproc" && !config_.shared_ctx) {
            config_.shared_ctx = std::make_shared<zmq::context_t>(1);
        }

        auto [server_config, client_config] = createConfigs();

        // Create and start server
        GenericZmqServer server(server_config, config_.shared_ctx);
        if (!server.start()) {
            std::cerr << "Failed to start server: " << server.getLastError() << std::endl;
            return false;
        }

        // Brief delay to ensure server is ready
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Create and connect client
        GenericZmqClient client(client_config, config_.shared_ctx);
        if (!client.connect()) {
            std::cerr << "Failed to connect client: " << client.getLastError() << std::endl;
            server.stop();
            return false;
        }

        // Setup message tracking
        setupMessageTracking(client);

        // Brief delay to ensure subscriptions are active
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Run the stress test
        bool success = executeStressTest(server);

        // Wait for final messages to arrive
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Calculate and display results
        bool test_passed = analyzeResults();

        // Cleanup
        client.disconnect();
        server.stop();

        return success && test_passed;
    }

private:
    TestConfig config_;
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> messages_lost_{0};
    std::vector<std::string> topics_;
    std::unordered_map<std::string, std::atomic<uint64_t>> topic_counters_;
    std::mutex stats_mutex_;

    std::pair<ZmqServerConfig, ZmqClientConfig> createConfigs() {
        ZmqServerConfig server_config;
        ZmqClientConfig client_config;

        // Optimize for high throughput
        server_config.sndhwm = 20000;
        server_config.rcvhwm = 20000;
        server_config.sndtimeo = 100;
        server_config.rcvtimeo = 100;
        server_config.linger = 0;
        server_config.immediate = true;
        
        client_config.rcvhwm = 20000;
        client_config.sndhwm = 20000;
        client_config.rcvtimeo = 100;
        client_config.sndtimeo = 100;
        client_config.linger = 0;

        if (config_.transport == "tcp") {
            server_config.pub_endpoint = "tcp://*:" + std::to_string(config_.tcp_pub_port);
            server_config.rep_endpoint = "tcp://*:" + std::to_string(config_.tcp_rep_port);
            client_config.pub_endpoints = {"tcp://" + config_.tcp_host + ":" + std::to_string(config_.tcp_pub_port)};
            client_config.req_endpoint = "tcp://" + config_.tcp_host + ":" + std::to_string(config_.tcp_rep_port);
        } else if (config_.transport == "ipc") {
            server_config.pub_endpoint = "ipc://" + config_.ipc_path + "_pub";
            server_config.rep_endpoint = "ipc://" + config_.ipc_path + "_rep";
            client_config.pub_endpoints = {"ipc://" + config_.ipc_path + "_pub"};
            client_config.req_endpoint = "ipc://" + config_.ipc_path + "_rep";
        } else { // inproc
            server_config.pub_endpoint = "inproc://" + config_.inproc_address + "_pub";
            server_config.rep_endpoint = "inproc://" + config_.inproc_address + "_rep";
            client_config.pub_endpoints = {"inproc://" + config_.inproc_address + "_pub"};
            client_config.req_endpoint = "inproc://" + config_.inproc_address + "_rep";
        }

        return {server_config, client_config};
    }

    void setupMessageTracking(GenericZmqClient& client) {
        // Generate topic names
        for (int i = 0; i < config_.num_topics; ++i) {
            std::string topic = "test.topic." + std::to_string(i);
            topics_.push_back(topic);
            topic_counters_[topic] = 0;
        }

        // Subscribe to all topics with message handler
        for (const auto& topic : topics_) {
            client.subscribe(topic, [this, topic](const std::string& recv_topic, 
                                                 const std::string& data, 
                                                 const std::unordered_map<std::string, std::string>& metadata) {
                messages_received_.fetch_add(1);
                topic_counters_[topic].fetch_add(1);
                
                // Validate message content
                if (data.size() != config_.payload_size) {
                    messages_lost_.fetch_add(1);
                }
            });
        }
    }

    bool executeStressTest(GenericZmqServer& server) {
        auto start_time = std::chrono::steady_clock::now();
        auto end_time = start_time + std::chrono::seconds(config_.duration_seconds);
        
        int messages_per_interval = config_.message_rate / 10; // 100ms intervals
        auto interval_duration = std::chrono::milliseconds(100);

        uint64_t total_messages_to_send = config_.message_rate * config_.duration_seconds;
        uint64_t messages_sent_local = 0;

        std::cout << "Starting message transmission...\n";

        while (std::chrono::steady_clock::now() < end_time) {
            auto interval_start = std::chrono::steady_clock::now();

            // Send messages for this interval
            for (int i = 0; i < messages_per_interval && messages_sent_local < total_messages_to_send; ++i) {
                std::string topic = topics_[messages_sent_local % topics_.size()];
                std::string payload = generatePayload(config_.payload_size);
                
                MessagePriority priority = MessagePriority::NORMAL;
                if (config_.mixed_priorities) {
                    int priority_val = messages_sent_local % 4;
                    priority = static_cast<MessagePriority>(priority_val);
                }

                std::unordered_map<std::string, std::string> metadata;
                if (config_.use_metadata) {
                    metadata["message_id"] = std::to_string(messages_sent_local);
                    metadata["test_run"] = "stress_test";
                }

                bool sent = false;
                if (config_.use_metadata) {
                    sent = server.publishWithMetadata(topic, payload, metadata, priority);
                } else {
                    sent = server.publishAsync(topic, payload, priority, std::to_string(messages_sent_local));
                }

                if (sent) {
                    messages_sent_.fetch_add(1);
                    messages_sent_local++;
                } else {
                    messages_lost_.fetch_add(1);
                    std::cerr << "Failed to send message " << messages_sent_local << ": " << server.getLastError() << std::endl;
                }
            }

            // Wait for the rest of the interval
            auto interval_end = interval_start + interval_duration;
            auto now = std::chrono::steady_clock::now();
            if (now < interval_end) {
                std::this_thread::sleep_until(interval_end);
            }

            // Progress indicator
            if (messages_sent_local % (config_.message_rate / 2) == 0) {
                double progress = static_cast<double>(messages_sent_local) / total_messages_to_send * 100.0;
                std::cout << "Progress: " << std::fixed << std::setprecision(1) << progress << "% ("
                          << messages_sent_local << "/" << total_messages_to_send << ")\n";
            }
        }

        std::cout << "Message transmission completed. Messages sent: " << messages_sent_.load() << "\n";
        return true;
    }

    bool analyzeResults() {
        std::cout << "\nTest Results\n";
        std::cout << "============\n";

        uint64_t sent = messages_sent_.load();
        uint64_t received = messages_received_.load();
        uint64_t lost = messages_lost_.load();

        double delivery_rate = sent > 0 ? static_cast<double>(received) / sent : 0.0;
        double loss_rate = sent > 0 ? static_cast<double>(lost) / sent : 0.0;

        std::cout << "Messages sent: " << sent << "\n";
        std::cout << "Messages received: " << received << "\n";
        std::cout << "Messages lost: " << lost << "\n";
        std::cout << "Delivery rate: " << std::fixed << std::setprecision(2) << (delivery_rate * 100) << "%\n";
        std::cout << "Loss rate: " << std::fixed << std::setprecision(2) << (loss_rate * 100) << "%\n";

        // Per-topic statistics
        std::cout << "\nPer-topic statistics:\n";
        for (const auto& topic : topics_) {
            uint64_t topic_received = topic_counters_[topic].load();
            std::cout << "  " << topic << ": " << topic_received << " messages\n";
        }

        // Calculate actual message rate
        double actual_rate = static_cast<double>(sent) / config_.duration_seconds;
        std::cout << "\nActual message rate: " << std::fixed << std::setprecision(1) << actual_rate << " msg/s\n";
        std::cout << "Target message rate: " << config_.message_rate << " msg/s\n";

        // Determine if test passed
        bool rate_achieved = actual_rate >= (config_.message_rate * 0.95); // Within 5% of target
        bool delivery_target_met = delivery_rate >= config_.target_delivery_rate;

        std::cout << "\nTest Results:\n";
        std::cout << "  Rate target achieved: " << (rate_achieved ? "PASS" : "FAIL") << "\n";
        std::cout << "  Delivery target achieved: " << (delivery_target_met ? "PASS" : "FAIL") << "\n";
        std::cout << "  Overall result: " << ((rate_achieved && delivery_target_met) ? "PASS" : "FAIL") << "\n";

        return rate_achieved && delivery_target_met;
    }

    std::string generatePayload(int size) {
        static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        std::string payload;
        payload.reserve(size);

        for (int i = 0; i < size; ++i) {
            payload += chars[i % chars.length()];
        }

        return payload;
    }
};

int main(int argc, char* argv[]) {
    ZmqStressTest::TestConfig config;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--transport" && i + 1 < argc) {
            config.transport = argv[++i];
        } else if (arg == "--rate" && i + 1 < argc) {
            config.message_rate = std::stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            config.duration_seconds = std::stoi(argv[++i]);
        } else if (arg == "--topics" && i + 1 < argc) {
            config.num_topics = std::stoi(argv[++i]);
        } else if (arg == "--payload-size" && i + 1 < argc) {
            config.payload_size = std::stoi(argv[++i]);
        } else if (arg == "--target-rate" && i + 1 < argc) {
            config.target_delivery_rate = std::stod(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --transport <type>    Transport type: inproc, ipc, tcp (default: inproc)\n";
            std::cout << "  --rate <rate>         Message rate in msg/s (default: 400)\n";
            std::cout << "  --duration <sec>      Test duration in seconds (default: 5)\n";
            std::cout << "  --topics <count>      Number of topics (default: 5)\n";
            std::cout << "  --payload-size <size> Payload size in bytes (default: 100)\n";
            std::cout << "  --target-rate <rate>  Target delivery rate 0.0-1.0 (default: 0.97)\n";
            std::cout << "  --help                Show this help\n";
            return 0;
        }
    }

    // Validate transport type
    if (config.transport != "inproc" && config.transport != "ipc" && config.transport != "tcp") {
        std::cerr << "Invalid transport type. Must be: inproc, ipc, or tcp\n";
        return 1;
    }

    try {
        ZmqStressTest test(config);
        bool success = test.runTest();
        
        return success ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
