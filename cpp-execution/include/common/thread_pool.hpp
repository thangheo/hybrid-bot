#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <type_traits>
#include <chrono>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency()) 
        : stop_(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] { workerThread(); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // Submit task và trả về future
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (stop_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        
        condition_.notify_one();
        return res;
    }

    size_t pendingTasks() const {
        std::unique_lock<std::mutex> lock(queueMutex_);
        return tasks_.size();
    }

private:
    void workerThread() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                condition_.wait(lock, [this] { 
                    return stop_ || !tasks_.empty(); 
                });
                
                if (stop_ && tasks_.empty()) {
                    return;
                }
                
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
};



// SimpleThreadPool with spinlock and soft queue limit for backpressure
namespace trading {
namespace common {

class SimpleThreadPool {
public:
    // Giữ nguyên interface: constructor(size_t), destructor, stop(), submit(F)
    explicit SimpleThreadPool(size_t threads = std::max(1u, std::thread::hardware_concurrency()),
                              size_t softQueueLimit = 1024)
        : stop_(false), soft_queue_limit_(softQueueLimit)
    {
        workers_.reserve(threads);
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~SimpleThreadPool() {
        stop();
    }

    void stop() {
        bool expected = false;
        if (!stop_.compare_exchange_strong(expected, true)) return;
        // Wake up any sleeping workers
        cv_.notify_all();
        for (auto &t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    // submit(F) -> std::future<decltype(f())>
    template<typename F>
    auto submit(F f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto taskPtr = std::make_shared<std::packaged_task<R()>>(std::move(f));
        std::future<R> fut = taskPtr->get_future();

        // Fast path: attempt lock-free-ish push via spin on atomic_flag
        if (try_acquire_spinlock()) {
            tasks_.emplace([taskPtr]() { (*taskPtr)(); });
            release_spinlock();
            cv_.notify_one();
            return fut;
        }

        // If fast spin failed, check soft queue limit and fallback to mutex-protected push
        {
            std::unique_lock<std::mutex> lk(fallback_mutex_);
            // Optional soft backpressure: if queue too large, yield briefly to avoid unbounded growth.
            // This does not change API but reduces risk of memory blow-up under spike.
            size_t tries = 0;
            while (!stop_.load() && tasks_.size() > soft_queue_limit_ && tries < 50) {
                ++tries;
                lk.unlock();
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                lk.lock();
            }
            tasks_.emplace([taskPtr]() { (*taskPtr)(); });
        }
        cv_.notify_one();
        return fut;
    }

private:
    // Worker loop: spin a bit for low latency, then block on cv when no work
    void workerLoop() {
        const int SPIN_CYCLES = 200;                    // tune: small number to optimize for short/intermittent tasks
        const std::chrono::microseconds SLEEP_WHEN_IDLE(50);

        while (!stop_.load()) {
            // Try fast path: acquire spinlock and pop
            if (try_acquire_spinlock()) {
                if (!tasks_.empty()) {
                    auto task = std::move(tasks_.front());
                    tasks_.pop();
                    release_spinlock();
                    // Execute outside locks: exceptions will be captured by packaged_task
                    task();
                    continue;
                }
                release_spinlock();
            }

            // Spin-wait loop (busy) for short period to catch bursts with low latency
            bool did_work = false;
            for (int i = 0; i < SPIN_CYCLES && !stop_.load(); ++i) {
                if (try_acquire_spinlock()) {
                    if (!tasks_.empty()) {
                        auto task = std::move(tasks_.front());
                        tasks_.pop();
                        release_spinlock();
                        task();
                        did_work = true;
                        break;
                    }
                    release_spinlock();
                }
                // short pause to avoid starving CPU
                std::this_thread::yield();
            }
            if (did_work) continue;

            // Fallback to blocking wait with mutex + condition variable
            std::unique_lock<std::mutex> lk(fallback_mutex_);
            cv_.wait_for(lk, SLEEP_WHEN_IDLE, [this] { return stop_.load() || !tasks_.empty(); });
            // loop repeats and will try fast path again
        }

        // Drain remaining tasks (optional): execute remaining tasks so packaged_task futures complete.
        // If you prefer to discard queued tasks on stop, remove this drain.
        while (true) {
            if (try_acquire_spinlock()) {
                if (tasks_.empty()) {
                    release_spinlock();
                    break;
                }
                auto task = std::move(tasks_.front());
                tasks_.pop();
                release_spinlock();
                try { task(); } catch (...) { /* packaged_task will propagate exceptions */ }
            } else {
                // If cannot get spinlock, fallback to mutex
                std::unique_lock<std::mutex> lk(fallback_mutex_);
                if (tasks_.empty()) break;
                auto task = std::move(tasks_.front());
                tasks_.pop();
                lk.unlock();
                try { task(); } catch (...) { }
            }
        }
    }

    // Simple spinlock helpers (fast-path). Use atomic_flag which is very cheap.
    bool try_acquire_spinlock() {
        // try to acquire quickly with a few spins to reduce contention
        const int SPIN_ATTEMPTS = 40;
        for (int i = 0; i < SPIN_ATTEMPTS; ++i) {
            if (!spin_.test_and_set(std::memory_order_acquire)) return true;
            // short CPU pause
            std::this_thread::yield();
        }
        return false;
    }
    void release_spinlock() {
        spin_.clear(std::memory_order_release);
    }

    // Members
    std::vector<std::thread> workers_;

    // Single shared queue protected primarily by spinlock; fallback_mutex_ used for blocking wait and safe fallback path
    std::queue<std::function<void()>> tasks_;
    std::atomic_flag spin_ = ATOMIC_FLAG_INIT;
    std::mutex fallback_mutex_;
    std::condition_variable cv_;

    std::atomic<bool> stop_;
    const size_t soft_queue_limit_;
};
inline
SimpleThreadPool &getDetectorThreadPool() {
    static SimpleThreadPool pool(std::max(1u, std::thread::hardware_concurrency()));
    return pool;
}
inline
SimpleThreadPool &getTrainingThreadPool() {
    static SimpleThreadPool pool(3);  // 3 threads for training to avoid overloading
    return pool;
}

} // namespace common
} // namespace trading
