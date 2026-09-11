#include "thread_pool.hpp"
#include <chrono>

ThreadPool::ThreadPool(std::size_t num_threads) {
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this](std::stop_token stop_tok) {
            while (!stop_tok.stop_requested()) {
                auto task_opt = task_queue_.pop();
                if (!task_opt.has_value()) {
                    break;
                }
                active_tasks_++;
                try {
                    (*task_opt)();
                } catch (...) {
                    // Exception safety guard for worker thread
                }
                active_tasks_--;
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(std::function<void()> task) {
    task_queue_.push(std::move(task));
}

void ThreadPool::stop() {
    task_queue_.stop();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.request_stop();
        }
    }
}

void ThreadPool::wait_until_empty() {
    while (!task_queue_.empty() || active_tasks_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
