#include <gtest/gtest.h>
#include <atomic>
#include <vector>
#include <thread>
#include <chrono>

#include "common/thread_pool.hpp"

using namespace trading::common;

TEST(SimpleThreadPoolTest, ConcurrentSubmitAndDrain) {
    // Use small soft limit to force fallback path
    SimpleThreadPool pool(4, 64);

    std::atomic<int> counter{0};
    const int num_threads = 8;
    const int tasks_per_thread = 1000;

    std::vector<std::thread> submitters;
    for (int i = 0; i < num_threads; ++i) {
        submitters.emplace_back([&] {
            for (int j = 0; j < tasks_per_thread; ++j) {
                pool.submit([&counter] {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    for (auto &t : submitters) t.join();

    // Give the pool some time to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Stop the pool (destructor will also stop) and check
    pool.stop();

    EXPECT_EQ(counter.load(), num_threads * tasks_per_thread);
}
