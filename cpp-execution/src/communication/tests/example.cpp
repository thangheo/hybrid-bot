#include "communication/zmq_server_client.hpp"
#include <iostream>
#include <thread>
#include <chrono>
using namespace trading::communication;

int main(int argc, char** argv) {
    std::cout << "ZMQ Server/Client Example\n";
    std::cout << "========================\n\n";
    // Choose transport: default is TCP. Pass "inproc" as first arg to use inproc
    // transport (for in-process tests) — when using inproc both sockets must
    // share the same zmq::context_t instance (we inject a shared context).
    bool use_inproc = false;
    if (argc > 1 && std::string(argv[1]) == "inproc") {
        use_inproc = true;
    }

    // Configure server and client
    ZmqServerConfig server_config;
    ZmqClientConfig client_config;
    server_config.enable_monitoring = true;
    

    // Create and start server. If using inproc we create a shared context and
    // inject it into both server and client so inproc transport works.
    std::shared_ptr<zmq::context_t> shared_ctx = nullptr;
    if (use_inproc) {
        shared_ctx = std::make_shared<zmq::context_t>(1);
        server_config.pub_endpoint = "inproc://example_pub";
        server_config.rep_endpoint = "inproc://example_rep";
    } else {
        server_config.pub_endpoint = "tcp://127.0.0.1:5555";
        server_config.rep_endpoint = "tcp://127.0.0.1:5556";
    }

    GenericZmqServer server(server_config, shared_ctx);
    if (!server.start()) {
        std::cerr << "Failed to start server: " << server.getLastError() << std::endl;
        return 1;
    }

    std::cout << "Server started on PUB: " << server_config.pub_endpoint 
              << ", REP: " << server_config.rep_endpoint << std::endl;

    // Register a request handler
    server.registerRequestHandler("echo", [](const std::string& data, 
                                            const std::unordered_map<std::string, std::string>& metadata) -> std::string {
        std::cout << "Received echo request: " << data << std::endl;
        return "Echo: " + data;
    });

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create and connect client
    if (use_inproc) {
        client_config.pub_endpoints = {"inproc://example_pub"};
        client_config.req_endpoint = "inproc://example_rep";
    } else {
        client_config.pub_endpoints = {"tcp://127.0.0.1:5555"};
        client_config.req_endpoint = "tcp://127.0.0.1:5556";
    }

    GenericZmqClient client(client_config, shared_ctx);
    if (!client.connect()) {
        std::cerr << "Failed to connect client: " << client.getLastError() << std::endl;
        server.stop();
        return 1;
    }

    std::cout << "Client connected\n";

    // Subscribe to topics
    client.subscribe("news.*", [](const std::string& topic, const std::string& data, 
                                 const std::unordered_map<std::string, std::string>& metadata) {
        std::cout << "Received on topic '" << topic << "': " << data << std::endl;
        
        // Print metadata if present
        if (!metadata.empty()) {
            std::cout << "  Metadata: ";
            for (const auto& [key, value] : metadata) {
                std::cout << key << "=" << value << " ";
            }
            std::cout << std::endl;
        }
    });

    client.subscribe("alerts.*", [](const std::string& topic, const std::string& data, 
                                  const std::unordered_map<std::string, std::string>& metadata) {
        std::cout << "[ALERT] " << topic << ": " << data << std::endl;
    });

    // Give subscriptions time to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "\nPublishing messages...\n";

    // Publish some messages
    std::unordered_map<std::string, std::string> metadata;
    metadata["source"] = "example";
    metadata["priority"] = "normal";

    // Publish with different priorities
    server.publishWithMetadata("news.tech", "New AI breakthrough announced!", metadata, MessagePriority::HIGH);
    server.publishAsync("news.sports", "Team wins championship!", MessagePriority::NORMAL, "sport-001");
    server.publishSync("alerts.system", "System maintenance scheduled", MessagePriority::CRITICAL);

    // Send some requests
    std::cout << "\nSending requests...\n";
    
    std::string response1 = client.sendRequest("echo", "Hello, ZMQ!");
    std::cout << "Response 1: " << response1 << std::endl;

    // Async request
    auto future_response = client.sendRequestAsync("echo", "Async message", {{"async", "true"}});
    std::string response2 = future_response.get();
    std::cout << "Response 2: " << response2 << std::endl;

    // Publish a few more messages
    for (int i = 0; i < 5; ++i) {
        std::string message = "News update #" + std::to_string(i + 1);
        server.publishAsync("news.general", message, MessagePriority::NORMAL);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Let messages process
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Display statistics
    std::cout << "\nStatistics:\n";
    std::cout << "Server - Messages sent: " << server.getMessagesSent() 
              << ", Messages dropped: " << server.getMessagesDropped() 
              << ", Queue size: " << server.getQueueSize() << std::endl;
    std::cout << "Client - Messages received: " << client.getMessagesReceived() << std::endl;

    // Cleanup
    std::cout << "\nShutting down...\n";
    client.disconnect();
    server.stop();

    std::cout << "Example completed successfully!\n";
    return 0;
}
