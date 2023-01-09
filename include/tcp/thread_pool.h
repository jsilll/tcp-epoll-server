#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
    using task_type = std::function<void()>;

public:
    [[nodiscard]] explicit ThreadPool(std::size_t num) {
        for (std::size_t i = 0; i < num; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    task_type task;
                    {
                        std::unique_lock<std::mutex> lock(task_mutex_);
                        task_cond_.wait(lock, [this] { return !tasks_.empty(); });
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    if (!task) {
                        push_stop_task();
                        return;
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() noexcept {
        Stop();
    }

    void Stop() {
        push_stop_task();
        for (auto &worker: workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        // clear all pending tasks
        std::queue<task_type> empty{};
        std::swap(tasks_, empty);
    }

    template<typename F, typename... Args>
    auto Push(F &&f, Args &&... args) {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task
                = std::make_shared<std::packaged_task<return_type()>>(
                        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        auto res = task->get_future();

        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            tasks_.emplace([task] { (*task)(); });
        }
        task_cond_.notify_one();

        return res;
    }

private:
    void push_stop_task() {
        std::lock_guard<std::mutex> lock(task_mutex_);
        tasks_.emplace();
        task_cond_.notify_one();
    }

    std::vector<std::thread> workers_;
    std::queue<task_type> tasks_;
    std::mutex task_mutex_;
    std::condition_variable task_cond_;
};
