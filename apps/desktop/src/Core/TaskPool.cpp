//
// Created by Joe on 26-3-23.
//

#include "TaskPool.h"

namespace av {
    TaskPool::TaskPool() {
        m_thread = std::thread([this]() {
            this->ThreadLoop();
        });
    }
    TaskPool::~TaskPool() {
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_stopFlag = true;
        }
        m_taskCondition.notify_all();
        //必须等线程结束之后再退出，防止崩溃
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    void TaskPool::Submit(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_tasks.push(task);
        }
        m_taskCondition.notify_one();
    }
    void TaskPool::ThreadLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_taskMutex);
                m_taskCondition.wait(lock, [this]() {//调用wait的瞬间，会自动释放这个互斥锁，让其他线程获得任务队列
                    return !m_tasks.empty() || m_stopFlag;
                });
                if (m_tasks.empty() && m_stopFlag) {
                    break;
                }
                task = m_tasks.front();
                m_tasks.pop();
            }
            task();
        }
    }




}