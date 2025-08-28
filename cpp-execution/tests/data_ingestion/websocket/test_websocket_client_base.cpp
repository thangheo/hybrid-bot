// tests/data_ingestion/websocket/test_websocket_client_base.cpp
#include "data_ingestion/websocket/client_base.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>

using namespace trading::data_ingestion;
using namespace ::testing;

namespace {

// Mock WebSocket client for testing
class MockWebSocketClient : public WebSocketClientBase {
public:
    MockWebSocketClient(const std::string& host, const std::string& port)
        : WebSocketClientBase(host, port, false), // Non-SSL for easier testing
          open_called_(false),
          message_received_(false),
          error_received_(false),
          reconnect_called_(false),
          last_message_(""),
          last_error_("") {
    }

    // Getters for test checking
    bool wasOpenCalled() const { return open_called_; }
    bool wasMessageReceived() const { return message_received_; }
    bool wasErrorReceived() const { return error_received_; }
    bool wasReconnectCalled() const { return reconnect_called_; }
    std::string getLastMessage() const { return last_message_; }
    std::string getLastError() const { return last_error_; }
    int getCloseCode() const { return close_code_; }
    std::string getCloseReason() const { return close_reason_; }
    
    // Wait for message with timeout
    bool waitForMessage(int timeout_ms = 1000) {
        std::unique_lock<std::mutex> lock(mutex_);
        return message_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this]() -> bool { return this->message_received_.load(); });
    }
    
    // Wait for open with timeout
    bool waitForOpen(int timeout_ms = 1000) {
        std::unique_lock<std::mutex> lock(mutex_);
        return open_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this]() -> bool { return this->open_called_.load(); });
    }
    
    // Mock message injection
    void injectMessage(const std::string& message) {
        // Directly call onMessage as if received from WebSocket
        onMessage(message);
    }
    
    // Mock error injection
    void injectError(const std::string& error) {
        onError(error);
    }
    
    // Mock close injection
    void injectClose(int code, const std::string& reason) {
        onClose(code, reason);
    }
    
    // Override connect to control for testing
    bool connect() override {
        // In tests, we can make connect always succeed and call onOpen
        connected_ = true;
        onOpen();
        return true;
    }
    
protected:
    // Implementation of base class methods for testing
    void onOpen() override {
        std::unique_lock<std::mutex> lock(mutex_);
        open_called_ = true;
        open_cv_.notify_all();
    }
    
    void onMessage(const std::string& message) override {
        std::unique_lock<std::mutex> lock(mutex_);
        message_received_ = true;
        last_message_ = message;
        message_cv_.notify_all();
    }
    
    void onClose(int code, const std::string& reason) override {
        std::unique_lock<std::mutex> lock(mutex_);
        close_code_ = code;
        close_reason_ = reason;
    }
    
    void onError(const std::string& error) override {
        std::unique_lock<std::mutex> lock(mutex_);
        error_received_ = true;
        last_error_ = error;
    }
    
    void onReconnect() override {
        std::unique_lock<std::mutex> lock(mutex_);
        reconnect_called_ = true;
    }
    
private:
    std::atomic<bool> open_called_;
    std::atomic<bool> message_received_;
    std::atomic<bool> error_received_;
    std::atomic<bool> reconnect_called_;
    std::string last_message_;
    std::string last_error_;
    int close_code_ = 0;
    std::string close_reason_;
    
    // Synchronization for testing
    mutable std::mutex mutex_;
    std::condition_variable open_cv_;
    std::condition_variable message_cv_;
};

} // anonymous namespace
 
// Test fixture - keep this outside the anonymous namespace
class WebSocketClientBaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a client pointed at a dummy endpoint
        client_ = std::make_unique<MockWebSocketClient>("localhost", "8080");
        client_->setPingInterval(100);
    }
    
    void TearDown() override {
        if (client_ && client_->isConnected()) {
            client_->disconnect();
        }
    }
    
    std::unique_ptr<MockWebSocketClient> client_;
};

// Test cases - must be outside any namespace
TEST_F(WebSocketClientBaseTest, ConnectionOpenEvent) {
    ASSERT_TRUE(client_->connect());
    EXPECT_TRUE(client_->waitForOpen(500));
    EXPECT_TRUE(client_->wasOpenCalled());
    EXPECT_TRUE(client_->isConnected());
}
// Test message handling
TEST_F(WebSocketClientBaseTest, MessageHandling) {
    ASSERT_TRUE(client_->connect());
    
    // Inject a test message
    client_->injectMessage("test_message");
    
    EXPECT_TRUE(client_->waitForMessage(500));
    EXPECT_TRUE(client_->wasMessageReceived());
    EXPECT_EQ("test_message", client_->getLastMessage());
}

// Test error handling
TEST_F(WebSocketClientBaseTest, ErrorHandling) {
    ASSERT_TRUE(client_->connect());
    
    // Inject an error
    client_->injectError("test_error");
    
    EXPECT_TRUE(client_->wasErrorReceived());
    EXPECT_EQ("test_error", client_->getLastError());
}

// Test close handling
TEST_F(WebSocketClientBaseTest, CloseHandling) {
    ASSERT_TRUE(client_->connect());
    
    const int CLOSE_CODE = 1000;
    const std::string CLOSE_REASON = "Normal closure";
    
    // Inject a close event
    client_->injectClose(CLOSE_CODE, CLOSE_REASON);
    
    EXPECT_EQ(CLOSE_CODE, client_->getCloseCode());
    EXPECT_EQ(CLOSE_REASON, client_->getCloseReason());
}

// Test disconnection
TEST_F(WebSocketClientBaseTest, Disconnection) {
    ASSERT_TRUE(client_->connect());
    EXPECT_TRUE(client_->isConnected());
    
    client_->disconnect();
    EXPECT_FALSE(client_->isConnected());
}

// Test sending data
TEST_F(WebSocketClientBaseTest, SendData) {
    ASSERT_TRUE(client_->connect());
    
    // This just tests that send doesn't throw an exception
    // In a real test, we'd need to mock the underlying WebSocket
    EXPECT_NO_THROW(client_->send("test_data"));
}

// Main function required to run the tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
