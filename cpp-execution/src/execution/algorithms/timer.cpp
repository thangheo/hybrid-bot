/**
 * @file timer.cpp
 * @brief Implementation of Timer utility class
 * 
 * Created on: May 9, 2025
 */

 #include "execution/algorithms/timer.hpp"

 namespace trading {
 namespace execution {
 namespace algorithms {
 
 Timer::Timer(std::function<void(void)> callback, std::chrono::milliseconds interval)
     : callback_(callback), interval_(interval), running_(false) {
 }
 
 Timer::Timer()
     : callback_(nullptr), interval_(std::chrono::milliseconds(0)), running_(false) {
 }
 
 Timer::~Timer() {
     stop();
 }
 
 void Timer::start() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (running_ || !callback_ || interval_.count() == 0) {
         return;  // Already running or invalid configuration
     }
     
     running_ = true;
     
     // Start the timer thread
     thread_ = std::thread([this]() {
         while (running_) {
             std::this_thread::sleep_for(interval_);
             if (running_ && callback_) {
                 callback_();
             }
         }
     });
 }
 
 bool Timer::start(std::function<void(void)> callback, std::chrono::milliseconds interval) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (running_) {
         return false;  // Already running
     }
     
     if (!callback || interval.count() == 0) {
         return false;  // Invalid configuration
     }
     
     // Update callback and interval
     callback_ = callback;
     interval_ = interval;
     
     running_ = true;
     
     // Start the timer thread
     thread_ = std::thread([this]() {
         while (running_) {
             std::this_thread::sleep_for(interval_);
             if (running_ && callback_) {
                 callback_();
             }
         }
     });
     
     return true;
 }
 
 void Timer::stop() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (!running_) {
         return;  // Not running
     }
     
     running_ = false;
     
     // Join the thread if joinable
     if (thread_.joinable()) {
         thread_.join();
     }
 }
 
 } // namespace algorithms
 } // namespace execution
 } // namespace trading