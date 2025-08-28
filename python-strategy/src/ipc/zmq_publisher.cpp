// ==== BASIC ZMQ PUBLISHER (works with any ZMQ version) ====
#include <zmq.h>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <sstream>

class BasicPublisher {
private:
    void* context;
    void* socket;
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_real_distribution<> dis;
    
public:
    BasicPublisher() : gen(rd()), dis(0.0, 100.0) {
        context = zmq_ctx_new();
        socket = zmq_socket(context, ZMQ_PUB);
        
        // Only basic options that work everywhere
        int linger = 0;
        zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
        
        zmq_bind(socket, "tcp://*:5556");
        std::cout << "Basic publisher started on port 5556" << std::endl;
        std::cout << "Using basic ZMQ options for maximum compatibility" << std::endl;
        
        // Short warmup
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    ~BasicPublisher() {
        zmq_close(socket);
        zmq_ctx_destroy(context);
    }
    
    void publish_csv_data(int count = 50) {
        std::cout << "Publishing " << count << " CSV messages..." << std::endl;
        
        for (int i = 0; i < count; i++) {
            int sensor_id = i % 5;
            
            // Get timestamp immediately before send
            auto now = std::chrono::high_resolution_clock::now();
            auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
            
            // Create simple CSV message (no std::setprecision needed)
            std::ostringstream oss;
            oss << "sensor." << sensor_id << " "
                << timestamp_us << ","
                << sensor_id << ","
                << (int)(dis(gen) * 100) / 100.0 << ","  // temperature (simple formatting)
                << (int)(dis(gen) * 100) / 100.0 << ","  // humidity
                << (int)((1000 + dis(gen) * 0.1) * 10) / 10.0;  // pressure
            
            std::string message = oss.str();
            
            // Send message
            int result = zmq_send(socket, message.c_str(), message.length(), 0);
            
            if (sensor_id == 0) {
                if (result >= 0) {
                    std::cout << "✓ Published sensor.0 at " << timestamp_us << " µs" << std::endl;
                } else {
                    std::cout << "✗ Failed to publish sensor.0" << std::endl;
                }
            }
            
            // Delay between messages
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        std::cout << "Finished publishing " << count << " messages" << std::endl;
    }
    
    void run_benchmark(int count = 500) {
        std::cout << "Running basic benchmark with " << count << " messages..." << std::endl;
        
        for (int i = 0; i < count; i++) {
            auto send_time = std::chrono::high_resolution_clock::now();
            auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                send_time.time_since_epoch()).count();
            
            // Simple message format
            std::string message = "bench " + std::to_string(timestamp_us);
            
            zmq_send(socket, message.c_str(), message.length(), 0);
            
            if (i % 50 == 0) {
                std::cout << "Sent benchmark message " << i << std::endl;
            }
            
            // 2ms delay for testing
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        
        std::cout << "Benchmark complete" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    std::cout << "Basic ZMQ Publisher (Compatible with old ZMQ versions)" << std::endl;
    
    BasicPublisher publisher;
    
    if (argc > 1 && std::string(argv[1]) == "bench") {
        publisher.run_benchmark(500);
    } else {
        publisher.publish_csv_data(30);
    }
    
    return 0;
}