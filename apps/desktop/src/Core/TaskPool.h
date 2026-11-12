//
// Created by Joe on 26-3-23.
//

#ifndef TASKPOOL_H
#define TASKPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
//目前仍是单线程的任务队列
namespace av {
    class TaskPool {
    public:
        TaskPool();
        ~TaskPool();
        void Submit(std::function<void()> task);

    private:
        void ThreadLoop();

    private:
        std::thread m_thread;
        std::queue<std::function<void()>> m_tasks;
        std::condition_variable m_taskCondition;
        std::mutex m_taskMutex;
        std::atomic<bool> m_stopFlag{false};
    };
}



#endif //TASKPOOL_H
