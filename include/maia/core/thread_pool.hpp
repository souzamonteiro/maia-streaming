#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <cstddef>

namespace maia::core {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads = 4, std::size_t max_queue_size = 1024);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool enqueue(std::function<void()> task);
    void stop();

    [[nodiscard]] std::size_t pending_tasks() const;
    [[nodiscard]] std::size_t busy_workers() const;
    [[nodiscard]] std::size_t total_workers() const noexcept { return workers_.size(); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cv_not_full_;
    std::atomic_bool stop_{false};
    std::atomic_size_t busy_count_{0};
    std::size_t max_queue_size_{1024};
};

} // namespace maia::core

