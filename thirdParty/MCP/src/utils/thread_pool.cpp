/**
 * @file thread_pool.cpp
 * @brief 
 * @author Joe
 * @date 26-5-3
 */


#include "thread_pool.h"
namespace mcp {
    ThreadPool::ThreadPool(size_t num_threads, size_t max_queue_size)
        : m_max_queue_size_(max_queue_size) {
        if (num_threads == 0) {
            throw std::invalid_argument("ThreadPool requires at least one thread");
        }
        if (max_queue_size == 0) {
            throw std::invalid_argument("ThreadPool max queue size must be greater than zero");
        }

        for (size_t i = 0; i < num_threads; ++i) {
            m_workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_mutex_);
                        m_cv_.wait(lock, [this] {
                            return m_stop_.load() || !m_tasks_.empty();
                        });

                        if (m_stop_.load() && m_tasks_.empty()) {
                            return;
                        }

                        task = std::move(m_tasks_.front());
                        m_tasks_.pop();
                    }

                    task();
                }
            });
        }
    }

    ThreadPool::~ThreadPool() {
        Shutdown();
    }

    void ThreadPool::Shutdown() {
        {
            std::unique_lock<std::mutex> lock(m_mutex_);
            if (m_stop_.exchange(true)) {
                return;
            }
        }

        m_cv_.notify_all();
        for (auto& worker : m_workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    size_t ThreadPool::WorkerCount() const {
        return m_workers_.size();
    }

    size_t ThreadPool::PendingTasks() const {
        std::unique_lock<std::mutex> lock(m_mutex_);
        return m_tasks_.size();
    }

    size_t ThreadPool::MaxQueueSize() const {
        return m_max_queue_size_;
    }

}
