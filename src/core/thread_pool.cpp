#include "maia/core/thread_pool.hpp"

namespace maia::core {

ThreadPool::ThreadPool(std::size_t num_threads, std::size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
    if (num_threads == 0) num_threads = 1;
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->cv_.wait(lock, [this] {
                        return this->stop_.load() || !this->tasks_.empty();
                    });

                    if (this->stop_.load() && this->tasks_.empty()) {
                        return;
                    }

                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                    this->cv_not_full_.notify_one();
                }

                ++busy_count_;
                try {
                    task();
                } catch (...) {
                    // Prevent worker thread termination on exception
                }
                --busy_count_;
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

bool ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_.load()) return false;
        if (tasks_.size() >= max_queue_size_) {
            return false; // Queue bounded rejection
        }
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void ThreadPool::stop() {
    bool expected = false;
    if (stop_.compare_exchange_strong(expected, true)) {
        cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}

std::size_t ThreadPool::pending_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

std::size_t ThreadPool::busy_workers() const {
    return busy_count_.load();
}

} // namespace maia::core

