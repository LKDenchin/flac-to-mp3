#pragma once

#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include "safe_queue.hpp"

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads = std::max(1u, std::thread::hardware_concurrency() - 1));
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(std::function<void()> task);
    void stop();
    void wait_until_empty();

    std::size_t thread_count() const { return workers_.size(); }

private:
    SafeTaskQueue<std::function<void()>> task_queue_;
    std::vector<std::jthread> workers_;
    std::atomic<std::size_t> active_tasks_{0};
};
