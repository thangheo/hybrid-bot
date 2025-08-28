#include "common/zeromq_client.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace trading::common;

int main() {
    // Configure ZeroMQ client
    ZeroMQConfig config;
    config.subscriberEndpoint = "tcp://localhost:5556";
    config.publisherEndpoint = "tcp://localhost:5557";
    config.reconnectInterval = std::chrono::milliseconds(500);
    config.maxReconnectAttempts = 5;
    
    // Create client instance
    ZeroMQClient client(config);
    
    // Set message handler
    client.setMessageHandler([](const std::string& topic, const std::string& message) {
        std::cout << "Received: Topic=" << topic << ", Message=" << message << std::endl;
    });
    
    // Set error handler
    client.setErrorHandler([](const std::string& context, const std::string& error) {
        std::cerr << "Error in " << context << ": " << error << std::endl;
    });
    
    // Start client
    if (!client.start()) {
        std::cerr << "Failed to start ZeroMQ client" << std::endl;
        return 1;
    }
    
    // Subscribe to topic
    client.subscribe("market.data");
    client.subscribe("orders");
    
    // Publish some messages in a loop
    for (int i = 0; i < 10; i++) {
        std::string message = "Test message " + std::to_string(i);
        
        if (client.publish("market.data", message)) {
            std::cout << "Published: " << message << std::endl;
        } else {
            std::cerr << "Failed to publish message" << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Print statistics
    ZeroMQStats stats = client.getStats();
    std::cout << "Messages sent: " << stats.messagesSent << std::endl;
    std::cout << "Messages received: " << stats.messagesReceived << std::endl;
    std::cout << "Bytes sent: " << stats.bytesSent << std::endl;
    std::cout << "Bytes received: " << stats.bytesReceived << std::endl;
    
    // Stop client
    client.stop();
    
    return 0;
}