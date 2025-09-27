#include "zmq_protobuf_extensions.hpp"
#include "messages.pb.h"  // Generated from messages.proto
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "ZMQ Protobuf Example\n";
    std::cout << "===================\n\n";

    // Configure for inproc transport (fastest, same-process only)
    ZmqServerConfig server_config;
    server_config.pub_endpoint = "inproc://news_server";
    server_config.rep_endpoint = "inproc://echo_server";
    server_config.enable_monitoring = true;

    ZmqClientConfig client_config;
    client_config.pub_endpoints = {"inproc://news_server"};
    client_config.req_endpoint = "inproc://echo_server";

    // Configure protobuf options for optimal performance
    ProtobufOptions pb_options;
    pb_options.use_delimited_format = true;  // Better for streaming
    pb_options.validate_on_parse = true;     // Safety

    // Create protobuf-optimized server and client
    auto server = createProtobufServer(server_config, pb_options);
    auto client = createProtobufClient(client_config, pb_options);

    // Start server
    if (!server->start()) {
        std::cerr << "Failed to start server: " << server->getLastError() << std::endl;
        return 1;
    }

    std::cout << "Server started with inproc transport\n";

    // Register protobuf echo handler
    server->registerProtobufHandler<zmq_example::EchoRequest, zmq_example::EchoResponse>(
        "echo",
        [](const zmq_example::EchoRequest& request, 
           const std::unordered_map<std::string, std::string>& metadata) -> zmq_example::EchoResponse {
            
            std::cout << "Received echo request: " << request.message() 
                      << " (seq: " << request.sequence_number() << ")" << std::endl;
            
            zmq_example::EchoResponse response;
            response.set_echoed_message("Echo: " + request.message());
            response.set_sequence_number(request.sequence_number());
            response.set_server_timestamp(std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()));
            response.set_success(true);
            
            return response;
        });

    // Register status handler
    server->registerProtobufHandler<zmq_example::StatusRequest, zmq_example::StatusResponse>(
        "status",
        [&server](const zmq_example::StatusRequest& request, 
                  const std::unordered_map<std::string, std::string>& metadata) -> zmq_example::StatusResponse {
            
            zmq_example::StatusResponse response;
            response.set_component(request.component());
            response.set_status("HEALTHY");
            response.set_version("1.0.0");
            response.set_uptime_seconds(60);
            
            // Add some metrics
            auto metrics = response.mutable_metrics();
            (*metrics)["messages_sent"] = std::to_string(server->getMessagesSent());
            (*metrics)["queue_size"] = std::to_string(server->getQueueSize());
            
            return response;
        });

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connect client
    if (!client->connect()) {
        std::cerr << "Failed to connect client: " << client->getLastError() << std::endl;
        server->stop();
        return 1;
    }

    std::cout << "Client connected\n";

    // Subscribe to news messages with protobuf handler
    client->subscribeProtobuf<zmq_example::NewsMessage>(
        "news.*",
        [](const std::string& topic, const zmq_example::NewsMessage& news,
           const std::unordered_map<std::string, std::string>& metadata) {
            std::cout << "\n[NEWS] " << topic << "\n";
            std::cout << "  Headline: " << news.headline() << "\n";
            std::cout << "  Content: " << news.content() << "\n";
            std::cout << "  Author: " << news.author() << "\n";
            std::cout << "  Category: " << news.category() << "\n";
            std::cout << "  Priority: " << news.priority() << "\n";
        });

    // Subscribe to alerts with protobuf handler
    client->subscribeProtobuf<zmq_example::AlertMessage>(
        "alerts.*",
        [](const std::string& topic, const zmq_example::AlertMessage& alert,
           const std::unordered_map<std::string, std::string>& metadata) {
            std::cout << "\n[ALERT] " << topic << " - " << alert.severity() << "\n";
            std::cout << "  Message: " << alert.message() << "\n";
            std::cout << "  Source: " << alert.source() << "\n";
            
            if (!alert.context().empty()) {
                std::cout << "  Context: ";
                for (const auto& [key, value] : alert.context()) {
                    std::cout << key << "=" << value << " ";
                }
                std::cout << "\n";
            }
        });

    // Give subscriptions time to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "\nPublishing protobuf messages...\n";

    // Publish news messages with different priorities
    {
        zmq_example::NewsMessage tech_news;
        tech_news.set_headline("Revolutionary AI Breakthrough");
        tech_news.set_content("Scientists develop new quantum AI algorithm with 1000x speedup");
        tech_news.set_author("Dr. Jane Smith");
        tech_news.set_category("Technology");
        tech_news.set_priority(3);  // HIGH
        tech_news.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

        server->publishProtobuf("news.tech", tech_news, MessagePriority::HIGH, "tech-001");
    }

    {
        zmq_example::NewsMessage sports_news;
        sports_news.set_headline("Local Team Wins Championship");
        sports_news.set_content("After a thrilling overtime victory, the home team claims the title");
        sports_news.set_author("Sports Reporter");
        sports_news.set_category("Sports");
        sports_news.set_priority(1);  // NORMAL
        sports_news.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

        server->publishProtobuf("news.sports", sports_news, MessagePriority::NORMAL, "sport-001");
    }

    // Publish alert message
    {
        zmq_example::AlertMessage alert;
        alert.set_message("System maintenance window starting in 10 minutes");
        alert.set_severity("WARNING");
        alert.set_source("maintenance_scheduler");
        alert.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        
        auto context = alert.mutable_context();
        (*context)["duration"] = "30 minutes";
        (*context)["services_affected"] = "user_api, reporting";

        server->publishProtobuf("alerts.maintenance", alert, MessagePriority::CRITICAL);
    }

    // Send protobuf requests
    std::cout << "\nSending protobuf requests...\n";

    try {
        // Echo request
        zmq_example::EchoRequest echo_req;
        echo_req.set_message("Hello from protobuf client!");
        echo_req.set_sequence_number(1);
        auto req_metadata = echo_req.mutable_metadata();
        (*req_metadata)["client_id"] = "example_client";

        auto echo_response = client->sendProtobufRequest<zmq_example::EchoRequest, zmq_example::EchoResponse>(
            "echo", echo_req);

        std::cout << "Echo Response: " << echo_response.echoed_message() << "\n";
        std::cout << "  Sequence: " << echo_response.sequence_number() << "\n";
        std::cout << "  Server Time: " << echo_response.server_timestamp() << "\n";
        std::cout << "  Success: " << (echo_response.success() ? "true" : "false") << "\n";

        // Status request
        zmq_example::StatusRequest status_req;
        status_req.set_component("zmq_server");

        auto status_response = client->sendProtobufRequest<zmq_example::StatusRequest, zmq_example::StatusResponse>(
            "status", status_req);

        std::cout << "\nStatus Response for " << status_response.component() << ":\n";
        std::cout << "  Status: " << status_response.status() << "\n";
        std::cout << "  Version: " << status_response.version() << "\n";
        std::cout << "  Uptime: " << status_response.uptime_seconds() << " seconds\n";
        std::cout << "  Metrics:\n";
        for (const auto& [key, value] : status_response.metrics()) {
            std::cout << "    " << key << ": " << value << "\n";
        }

        // Async request example
        std::cout << "\nSending async request...\n";
        auto future_response = client->sendProtobufRequestAsync<zmq_example::EchoRequest, zmq_example::EchoResponse>(
            "echo", echo_req);

        // Do other work...
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto async_response = future_response.get();
        std::cout << "Async Echo Response: " << async_response.echoed_message() << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Request failed: " << e.what() << std::endl;
    }

    // Publish a few more messages to demonstrate streaming
    std::cout << "\nStreaming more news updates...\n";
    for (int i = 0; i < 3; ++i) {
        zmq_example::NewsMessage update;
        update.set_headline("Breaking News Update #" + std::to_string(i + 1));
        update.set_content("This is update number " + std::to_string(i + 1) + " from the newsroom");
        update.set_author("News Bot");
        update.set_category("Breaking");
        update.set_priority(2);  // HIGH
        update.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

        server->publishProtobuf("news.breaking", update, MessagePriority::HIGH);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Let final messages process
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Display statistics
    std::cout << "\nPerformance Statistics:\n";
    std::cout << "======================\n";
    std::cout << "Server - Messages sent: " << server->getMessagesSent() 
              << ", Messages dropped: " << server->getMessagesDropped() 
              << ", Queue size: " << server->getQueueSize() << std::endl;
    std::cout << "Client - Messages received: " << client->getMessagesReceived() << std::endl;

    // Cleanup
    std::cout << "\nShutting down...\n";
    client->disconnect();
    server->stop();

    std::cout << "Protobuf example completed successfully!\n";
    std::cout << "\nKey advantages of protobuf API:\n";
    std::cout << "- Type safety at compile time\n";
    std::cout << "- Automatic serialization/deserialization\n";
    std::cout << "- Schema evolution support\n";
    std::cout << "- Better performance with delimited format\n";
    std::cout << "- Automatic message type metadata\n";

    return 0;
}
