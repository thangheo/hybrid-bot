/**
 * @file timer.hpp
 * @brief Timer utility class for scheduling periodic tasks
 * 
 * @details 
 * Provides a simple timer implementation using std::thread and 
 * std::chrono for scheduling periodic tasks.
 * 
 * Created on: May 9, 2025
 */

 #pragma once

 #include <functional>
 #include <chrono>
 #include <thread>
 #include <atomic>
 #include <mutex>
 #include <condition_variable>
 
 namespace trading {
 namespace execution {
 namespace algorithms {
 
 /**
  * @brief Simple timer class for periodic execution of tasks
  */
 class Timer {
 public:
     /**
      * @brief Constructor with callback and interval
      * 
      * @param callback The function to call at each interval
      * @param interval The interval between executions
      */
     Timer(std::function<void(void)> callback = nullptr, 
           std::chrono::milliseconds interval = std::chrono::milliseconds(0));
     
     /**
      * @brief Default constructor
      */
     Timer();
     
     /**
      * @brief Destructor
      */
     ~Timer();
     
     /**
      * @brief Start the timer with the current callback and interval
      */
     void start();
     
     /**
      * @brief Start the timer with a new callback and interval
      * 
      * @param callback The function to call at each interval
      * @param interval The interval between executions
      * @return true if started successfully, false otherwise
      */
     bool start(std::function<void(void)> callback, std::chrono::milliseconds interval);
     
     /**
      * @brief Start the timer with the given interval and current callback
      * 
      * @param interval The interval between executions
      * @return true if started successfully, false otherwise
      */
     template<typename Duration>
     bool start(Duration interval) {
         return start(callback_, std::chrono::duration_cast<std::chrono::milliseconds>(interval));
     }
     
     /**
      * @brief Start the timer with the given callback and current interval
      * 
      * @param callback The function to call at each interval
      * @return true if started successfully, false otherwise
      */
     bool start(std::function<void(void)> callback) {
         return start(callback, interval_);
     }
     
     /**
      * @brief Stop the timer
      */
     void stop();
     
     /**
      * @brief Check if the timer is running
      * 
      * @return true if running, false otherwise
      */
     bool isRunning() const {
         return running_;
     }
     
     /**
      * @brief Set the callback function
      * 
      * @param callback The function to call at each interval
      */
     void setCallback(std::function<void(void)> callback) {
         callback_ = callback;
     }
     
     /**
      * @brief Set the interval between executions
      * 
      * @param interval The interval between executions
      */
     void setInterval(std::chrono::milliseconds interval) {
         interval_ = interval;
     }
     
 private:
     std::function<void(void)> callback_;   // Callback function
     std::chrono::milliseconds interval_;   // Timer interval
     std::thread thread_;                   // Timer thread
     std::atomic<bool> running_;            // Flag indicating if timer is running
     mutable std::mutex mutex_;             // Mutex for thread safety
 };
 
 } // namespace algorithms
 } // namespace execution
 } // namespace trading