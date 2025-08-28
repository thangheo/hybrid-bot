#include <zmq.hpp>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <jsoncpp/json/json.h>
// #include <json/json-forwards.h>
#include <queue>
#include <mutex>

class ZMQMathServer {
private:
    zmq::context_t context;
    zmq::socket_t socket;
    std::queue<std::pair<double, double>> computation_queue;
    std::mutex queue_mutex;
    bool running;

public:
    ZMQMathServer() : context(1), socket(context, ZMQ_REP), running(true) {
        socket.bind("tcp://*:5555");
        std::cout << "ZMQ Server started on port 5555" << std::endl;
    }
    
    void start() {
        while (running) {
            zmq::message_t request;
            
            // Wait for next request from client
            auto result = socket.recv(request, zmq::recv_flags::dontwait);
            
            if (result) {
                std::string request_str = std::string(static_cast<char*>(request.data()), request.size());
                std::string response = process_request(request_str);
                
                // Send reply back to client
                zmq::message_t reply(response.size());
                memcpy(reply.data(), response.c_str(), response.size());
                socket.send(reply, zmq::send_flags::dontwait);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    void stop() {
        running = false;
    }

private:
    std::string process_request(const std::string& request) {
        Json::Value root;
        Json::Reader reader;
        Json::Value response;
        
        try {
            if (reader.parse(request, root)) {
                std::string operation = root["operation"].asString();
                
                if (operation == "add") {
                    double a = root["a"].asDouble();
                    double b = root["b"].asDouble();
                    response["result"] = a + b;
                    response["status"] = "success";
                    response["operation"] = operation;
                }
                else if (operation == "multiply") {
                    double a = root["a"].asDouble();
                    double b = root["b"].asDouble();
                    response["result"] = a * b;
                    response["status"] = "success";
                    response["operation"] = operation;
                }
                else if (operation == "matrix_multiply") {
                    // Simulate heavy computation
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    
                    auto matrix_a = root["matrix_a"];
                    auto matrix_b = root["matrix_b"];
                    
                    // Simple 2x2 matrix multiplication
                    Json::Value result_matrix(Json::arrayValue);
                    for (int i = 0; i < 2; i++) {
                        Json::Value row(Json::arrayValue);
                        for (int j = 0; j < 2; j++) {
                            double sum = 0;
                            for (int k = 0; k < 2; k++) {
                                sum += matrix_a[i][k].asDouble() * matrix_b[k][j].asDouble();
                            }
                            row.append(sum);
                        }
                        result_matrix.append(row);
                    }
                    
                    response["result"] = result_matrix;
                    response["status"] = "success";
                    response["operation"] = operation;
                }
                else if (operation == "fibonacci") {
                    int n = root["n"].asInt();
                    response["result"] = static_cast<Json::Int64>(fibonacci(n));
                    response["status"] = "success";
                    response["operation"] = operation;
                }
                else if (operation == "prime_check") {
                    int num = root["number"].asInt();
                    response["result"] = is_prime(num);
                    response["status"] = "success";
                    response["operation"] = operation;
                }
                else {
                    response["error"] = "Unknown operation: " + operation;
                    response["status"] = "error";
                }
            } else {
                response["error"] = "Invalid JSON format";
                response["status"] = "error";
            }
        } catch (const std::exception& e) {
            response["error"] = std::string("Exception: ") + e.what();
            response["status"] = "error";
        }
        
        Json::StreamWriterBuilder builder;
        return Json::writeString(builder, response);
    }
    
    long long fibonacci(int n) {
        if (n <= 1) return n;
        long long a = 0, b = 1;
        for (int i = 2; i <= n; i++) {
            long long temp = a + b;
            a = b;
            b = temp;
        }
        return b;
    }
    
    bool is_prime(int n) {
        if (n < 2) return false;
        for (int i = 2; i * i <= n; i++) {
            if (n % i == 0) return false;
        }
        return true;
    }
};

int main() {
    ZMQMathServer server;
    server.start();
    return 0;
}