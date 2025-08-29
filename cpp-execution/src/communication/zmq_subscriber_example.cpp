// C++ ZMQ Subscriber for Python Publisher
#include <zmq.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <map>

// Binary data structure (matching Python struct)
struct BinarySensorData {
    uint64_t timestamp_us;
    uint32_t sensor_id;
    float temperature;
    float humidity; 
    float pressure;
    float reserved[5];  // Reserved for future use
    char location[16];
} __attribute__((packed));

class CppSubscriber {
private:
    void* context;
    void* socket;
    std::vector<double> latencies;
    
public:
    CppSubscriber(const std::string& address = "tcp://localhost:5556") {
        context = zmq_ctx_new();
        socket = zmq_socket(context, ZMQ_SUB);
        
        // Basic socket options
        int linger = 0;
        zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
        
        // Set receive timeout
        int timeout = 1000; // 1 second
        zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        
        zmq_connect(socket, address.c_str());
        std::cout << "C++ subscriber connected to " << address << std::endl;
    }
    
    ~CppSubscriber() {
        zmq_close(socket);
        zmq_ctx_destroy(context);
    }
    
    void subscribe_to_topic(const std::string& topic) {
        zmq_setsockopt(socket, ZMQ_SUBSCRIBE, topic.c_str(), topic.length());
        std::cout << "✓ Subscribed to topic: " << topic << std::endl;
    }
    
    void subscribe_to_all() {
        zmq_setsockopt(socket, ZMQ_SUBSCRIBE, "", 0);
        std::cout << "✓ Subscribed to all topics" << std::endl;
    }
    
    void listen_csv_format(int max_messages = 20) {
        std::cout << "Listening for CSV format messages..." << std::endl;
        
        int received = 0;
        auto start_time = std::chrono::steady_clock::now();
        
        while (received < max_messages) {
            char buffer[1024];
            int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
            
            if (size == -1) {
                if (zmq_errno() == EAGAIN) {
                    std::cout << "Timeout waiting for messages" << std::endl;
                    break;
                }
                continue;
            }
            
            buffer[size] = '\0';
            std::string message(buffer);
            
            auto receive_time = std::chrono::high_resolution_clock::now();
            auto receive_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                receive_time.time_since_epoch()).count();
            
            // Parse CSV message: "sensor.X timestamp_us,sensor_id,temp,humidity,pressure,location"
            if (parse_csv_message(message, receive_time_us)) {
                received++;
            }
            
            // Check for timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed > std::chrono::seconds(30)) {
                std::cout << "Overall timeout reached" << std::endl;
                break;
            }
        }
        
        print_statistics("CSV");
    }
    
    void listen_json_format(int max_messages = 15) {
        std::cout << "Listening for JSON format messages..." << std::endl;
        
        int received = 0;
        
        while (received < max_messages) {
            char buffer[2048];  // Larger buffer for JSON
            int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
            
            if (size == -1) {
                if (zmq_errno() == EAGAIN) {
                    std::cout << "Timeout waiting for JSON messages" << std::endl;
                    break;
                }
                continue;
            }
            
            buffer[size] = '\0';
            std::string message(buffer);
            
            auto receive_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            
            if (parse_json_message(message, receive_time_us)) {
                received++;
            }
        }
        
        print_statistics("JSON");
    }
    
    void listen_binary_format(int max_messages = 25) {
        std::cout << "Listening for binary format messages..." << std::endl;
        
        int received = 0;
        
        while (received < max_messages) {
            char buffer[1024];
            int size = zmq_recv(socket, buffer, sizeof(buffer), 0);
            
            if (size == -1) {
                if (zmq_errno() == EAGAIN) {
                    std::cout << "Timeout waiting for binary messages" << std::endl;
                    break;
                }
                continue;
            }
            
            auto receive_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            
            if (parse_binary_message(buffer, size, receive_time_us)) {
                received++;
            }
        }
        
        print_statistics("BINARY");
    }
    
    void listen_high_frequency(int duration_seconds = 10) {
        std::cout << "Listening for high-frequency test messages for " 
                  << duration_seconds << " seconds..." << std::endl;
        
        int received = 0;
        auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_seconds)) {
            char buffer[256];
            int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, ZMQ_DONTWAIT);
            
            if (size == -1) {
                if (zmq_errno() == EAGAIN) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    continue;
                }
                continue;
            }
            
            buffer[size] = '\0';
            std::string message(buffer);
            
            auto receive_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            
            if (parse_test_message(message, receive_time_us)) {
                received++;
                
                if (received % 100 == 0) {
                    std::cout << "Received " << received << " test messages" << std::endl;
                }
            }
        }
        
        std::cout << "Total messages received: " << received << std::endl;
        print_statistics("HIGH_FREQUENCY");
        
        // Calculate throughput
        double throughput = static_cast<double>(received) / duration_seconds;
        std::cout << "Throughput: " << throughput << " messages/second" << std::endl;
    }
    
    void listen_market_data(int max_messages = 100) {
        std::cout << "Listening for market data updates..." << std::endl;
        
        int received = 0;
        std::map<std::string, double> latest_prices;
        
        while (received < max_messages) {
            char buffer[512];
            int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
            
            if (size == -1) {
                if (zmq_errno() == EAGAIN) {
                    std::cout << "Timeout waiting for market data" << std::endl;
                    break;
                }
                continue;
            }
            
            buffer[size] = '\0';
            std::string message(buffer);
            
            auto receive_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            
            if (parse_market_message(message, receive_time_us, latest_prices)) {
                received++;
            }
        }
        
        // Print final market summary
        std::cout << "\n=== FINAL MARKET PRICES ===" << std::endl;
        for (const auto& [symbol, price] : latest_prices) {
            std::cout << symbol << ": $" << std::fixed << std::setprecision(2) << price << std::endl;
        }
        
        print_statistics("MARKET_DATA");
    }

private:
    bool parse_csv_message(const std::string& message, uint64_t receive_time_us) {
        try {
            // Parse: "sensor.X timestamp_us,sensor_id,temp,humidity,pressure,location"
            size_t space_pos = message.find(' ');
            if (space_pos == std::string::npos) return false;
            
            std::string topic = message.substr(0, space_pos);
            std::string csv_data = message.substr(space_pos + 1);
            
            // Parse CSV fields
            std::vector<std::string> fields;
            std::stringstream ss(csv_data);
            std::string field;
            
            while (std::getline(ss, field, ',')) {
                fields.push_back(field);
            }
            
            if (fields.size() >= 6) {
                uint64_t sent_time_us = std::stoull(fields[0]);
                int sensor_id = std::stoi(fields[1]);
                double temperature = std::stod(fields[2]);
                double humidity = std::stod(fields[3]);
                double pressure = std::stod(fields[4]);
                std::string location = fields[5];
                
                double latency_ms = (receive_time_us - sent_time_us) / 1000.0;
                latencies.push_back(latency_ms);
                
                std::cout << "Sensor " << sensor_id << " (" << location << "): "
                          << std::fixed << std::setprecision(2) << temperature << "°C, "
                          << humidity << "% RH, " << pressure << " hPa "
                          << "→ latency " << std::setprecision(3) << latency_ms << " ms" << std::endl;
                
                return true;
            }
        } catch (const std::exception& e) {
            std::cout << "Error parsing CSV message: " << e.what() << std::endl;
        }
        
        return false;
    }
    
    bool parse_json_message(const std::string& message, uint64_t receive_time_us) {
        try {
            // Simple JSON parsing for demonstration
            // In production, use proper JSON library like nlohmann/json
            size_t space_pos = message.find(' ');
            if (space_pos == std::string::npos) return false;
            
            std::string topic = message.substr(0, space_pos);
            std::string json_data = message.substr(space_pos + 1);
            
            // Extract timestamp (simple approach - find "timestamp_us":value)
            size_t ts_pos = json_data.find("\"timestamp_us\":");
            if (ts_pos != std::string::npos) {
                size_t start = ts_pos + 15;
                size_t end = json_data.find_first_of(",}", start);
                
                if (end != std::string::npos) {
                    uint64_t sent_time_us = std::stoull(json_data.substr(start, end - start));
                    double latency_ms = (receive_time_us - sent_time_us) / 1000.0;
                    latencies.push_back(latency_ms);
                    
                    std::cout << "JSON message from " << topic 
                              << " → latency " << std::fixed << std::setprecision(3) 
                              << latency_ms << " ms" << std::endl;
                    
                    return true;
                }
            }
        } catch (const std::exception& e) {
            std::cout << "Error parsing JSON message: " << e.what() << std::endl;
        }
        
        return false;
    }
    
    bool parse_binary_message(const char* buffer, int size, uint64_t receive_time_us) {
        try {
            // Find topic separator
            const char* space_ptr = static_cast<const char*>(memchr(buffer, ' ', size));
            if (!space_ptr) return false;
            
            int topic_len = space_ptr - buffer;
            std::string topic(buffer, topic_len);
            
            // Extract binary data
            const char* binary_data = space_ptr + 1;
            int binary_size = size - topic_len - 1;
            
            if (binary_size >= sizeof(BinarySensorData)) {
                const BinarySensorData* data = reinterpret_cast<const BinarySensorData*>(binary_data);
                
                double latency_ms = (receive_time_us - data->timestamp_us) / 1000.0;
                latencies.push_back(latency_ms);
                
                // Extract location string (null-terminated)
                std::string location(data->location, strnlen(data->location, 16));
                
                std::cout << "Binary sensor " << data->sensor_id << " (" << location << "): "
                          << std::fixed << std::setprecision(2) << data->temperature << "°C, "
                          << data->humidity << "% RH, " << data->pressure << " hPa "
                          << "→ latency " << std::setprecision(3) << latency_ms << " ms" << std::endl;
                
                return true;
            }
        } catch (const std::exception& e) {
            std::cout << "Error parsing binary message: " << e.what() << std::endl;
        }
        
        return false;
    }
    
    bool parse_test_message(const std::string& message, uint64_t receive_time_us) {
        try {
            // Parse: "test timestamp_us"
            if (message.substr(0, 5) == "test ") {
                uint64_t sent_time_us = std::stoull(message.substr(5));
                double latency_ms = (receive_time_us - sent_time_us) / 1000.0;
                latencies.push_back(latency_ms);
                return true;
            }
        } catch (const std::exception& e) {
            // Silently ignore parsing errors for high-frequency messages
        }
        
        return false;
    }
    
    bool parse_market_message(const std::string& message, uint64_t receive_time_us, 
                             std::map<std::string, double>& latest_prices) {
        try {
            // Parse: "market symbol,timestamp_us,price,volume,bid,ask"
            if (message.substr(0, 7) == "market ") {
                std::string csv_data = message.substr(7);
                
                std::vector<std::string> fields;
                std::stringstream ss(csv_data);
                std::string field;
                
                while (std::getline(ss, field, ',')) {
                    fields.push_back(field);
                }
                
                if (fields.size() >= 6) {
                    std::string symbol = fields[0];
                    uint64_t sent_time_us = std::stoull(fields[1]);
                    double price = std::stod(fields[2]);
                    int volume = std::stoi(fields[3]);
                    double bid = std::stod(fields[4]);
                    double ask = std::stod(fields[5]);
                    
                    double latency_ms = (receive_time_us - sent_time_us) / 1000.0;
                    latencies.push_back(latency_ms);
                    
                    latest_prices[symbol] = price;
                    
                    std::cout << symbol << ": $" << std::fixed << std::setprecision(2) << price
                              << " (Vol: " << volume << ", Spread: " << (ask - bid) << ")"
                              << " → latency " << std::setprecision(3) << latency_ms << " ms" << std::endl;
                    
                    return true;
                }
            }
        } catch (const std::exception& e) {
            std::cout << "Error parsing market message: " << e.what() << std::endl;
        }
        
        return false;
    }
    
    void print_statistics(const std::string& format) {
        if (latencies.empty()) {
            std::cout << "No latency data collected for " << format << std::endl;
            return;
        }
        
        std::sort(latencies.begin(), latencies.end());
        
        double sum = 0;
        for (double latency : latencies) {
            sum += latency;
        }
        
        double avg = sum / latencies.size();
        double min_lat = latencies.front();
        double max_lat = latencies.back();
        
        std::cout << "\n=== " << format << " LATENCY STATISTICS ===" << std::endl;
        std::cout << "Messages:     " << latencies.size() << std::endl;
        std::cout << "Average:      " << std::fixed << std::setprecision(3) << avg << " ms" << std::endl;
        std::cout << "Min:          " << min_lat << " ms" << std::endl;
        std::cout << "Max:          " << max_lat << " ms" << std::endl;
        
        if (latencies.size() >= 10) {
            size_t p90_idx = static_cast<size_t>(0.90 * latencies.size());
            size_t p95_idx = static_cast<size_t>(0.95 * latencies.size());
            
            std::cout << "P90:          " << latencies[p90_idx] << " ms" << std::endl;
            std::cout << "P95:          " << latencies[p95_idx] << " ms" << std::endl;
        }
        
        // Performance assessment
        if (avg < 1.0) {
            std::cout << "✅ Excellent latency!" << std::endl;
        } else if (avg < 5.0) {
            std::cout << "✅ Good latency" << std::endl;
        } else {
            std::cout << "⚠ High latency detected" << std::endl;
        }
        
        // Clear latencies for next test
        latencies.clear();
    }
};

int main(int argc, char* argv[]) {
    std::cout << "=== C++ ZMQ Subscriber ===" << std::endl;
    
    CppSubscriber subscriber;
    
    if (argc > 1) {
        std::string mode = argv[1];
        
        if (mode == "csv") {
            subscriber.subscribe_to_topic("sensor.");
            subscriber.listen_csv_format(25);
        } else if (mode == "json") {
            subscriber.subscribe_to_topic("json_sensor.");
            subscriber.listen_json_format(15);
        } else if (mode == "binary") {
            subscriber.subscribe_to_topic("binary.");
            subscriber.listen_binary_format(20);
        }
        // else if (mode == "test") {
        //     subscriber.subscribe_to_topic("test");
        //     subscriber.listen_test_format(10);
        // }
    } else {
        // Default: subscribe to all and run all tests
        subscriber.subscribe_to_all();
        subscriber.listen_csv_format(10);
        subscriber.listen_json_format(10);
        subscriber.listen_binary_format(10);
        subscriber.listen_high_frequency(5);
        subscriber.listen_market_data(50);

    }
}
