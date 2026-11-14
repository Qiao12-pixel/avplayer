/**
 * @file thread_pool.h
 * @brief 
 * @author Joe
 * @date 26-5-3
 */


#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <atomic>
#include <memory>


namespace mcp {
    class ThreadPoolQueueFull : public std::runtime_error {
    public:
        ThreadPoolQueueFull() : std::runtime_error("ThreadPool queue is full") {}
    };

    class ThreadPool {
    public:
        explicit ThreadPool(size_t num_threads, size_t max_queue_size);
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        void Shutdown();
        size_t WorkerCount() const;
        size_t PendingTasks() const;
        size_t MaxQueueSize() const;

        template <typename F, typename... Args>
        auto Submit(F&& f, Args&&... args)->std::future<std::invoke_result_t<F, Args...>> {
            using return_type = std::invoke_result_t<F, Args...>;//拿到返回值
            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );//包装任务
            std::future<return_type> res = task->get_future();
            {
                std::unique_lock<std::mutex> lock(m_mutex_);
                if (m_stop_.load()) {
                    throw std::runtime_error("ThreadPool stopped");
                }
                if (m_tasks_.size() >= m_max_queue_size_) {
                    throw ThreadPoolQueueFull();
                }
                m_tasks_.emplace([task] {
                    (*task)();
                });//将单个任务放进任务队列
            }
            m_cv_.notify_one();
            return res;
        }

    private:
        std::vector<std::thread> m_workers_;
        std::queue<std::function<void()>> m_tasks_;
        std::condition_variable m_cv_;
        mutable std::mutex m_mutex_;
        std::atomic_bool m_stop_{false};
        size_t m_max_queue_size_;
    };
}



#endif //THREAD_POOL_H
