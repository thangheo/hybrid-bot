#include <gtest/gtest.h>
#include "risk/utils/risk_logger.hpp"
#include <fstream>
#include <string>
#include <filesystem>

class RiskLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test log file path
        testLogFile = "test_risk_log.txt";
        
        // Make sure the test file doesn't exist
        if (std::filesystem::exists(testLogFile)) {
            std::filesystem::remove(testLogFile);
        }
        
        // Create logger with test file
        logger = std::make_unique<RiskLogger>(testLogFile);
    }
    
    void TearDown() override {
        // Clean up test log file
        if (std::filesystem::exists(testLogFile)) {
            std::filesystem::remove(testLogFile);
        }
    }

    std::string testLogFile;
    std::unique_ptr<RiskLogger> logger;
};

TEST_F(RiskLoggerTest, LogInfo) {
    logger->logInfo("Test info message");
    
    // Check if file exists and contains the message
    std::ifstream logFile(testLogFile);
    ASSERT_TRUE(logFile.is_open());
    
    std::string line;
    bool foundMessage = false;
    while (std::getline(logFile, line)) {
        if (line.find("INFO") != std::string::npos && 
            line.find("Test info message") != std::string::npos) {
            foundMessage = true;
            break;
        }
    }
    
    EXPECT_TRUE(foundMessage);
}

TEST_F(RiskLoggerTest, LogWarning) {
    logger->logWarning("Test warning message");
    
    std::ifstream logFile(testLogFile);
    ASSERT_TRUE(logFile.is_open());
    
    std::string line;
    bool foundMessage = false;
    while (std::getline(logFile, line)) {
        if (line.find("WARNING") != std::string::npos && 
            line.find("Test warning message") != std::string::npos) {
            foundMessage = true;
            break;
        }
    }
    
    EXPECT_TRUE(foundMessage);
}

TEST_F(RiskLoggerTest, LogError) {
    logger->logError("Test error message");
    
    std::ifstream logFile(testLogFile);
    ASSERT_TRUE(logFile.is_open());
    
    std::string line;
    bool foundMessage = false;
    while (std::getline(logFile, line)) {
        if (line.find("ERROR") != std::string::npos && 
            line.find("Test error message") != std::string::npos) {
            foundMessage = true;
            break;
        }
    }
    
    EXPECT_TRUE(foundMessage);
}

TEST_F(RiskLoggerTest, LogRiskEvent) {
    RiskEvent event;
    event.type = RiskEventType::POSITION_LIMIT_BREACH;
    event.severity = RiskSeverity::HIGH;
    event.assetId = "BTC";
    event.message = "Position limit exceeded";
    
    logger->logRiskEvent(event);
    
    std::ifstream logFile(testLogFile);
    ASSERT_TRUE(logFile.is_open());
    
    std::string line;
    bool foundMessage = false;
    while (std::getline(logFile, line)) {
        if (line.find("RISK_EVENT") != std::string::npos && 
            line.find("POSITION_LIMIT_BREACH") != std::string::npos &&
            line.find("Position limit exceeded") != std::string::npos) {
            foundMessage = true;
            break;
        }
    }
    
    EXPECT_TRUE(foundMessage);
}