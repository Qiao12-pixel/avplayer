#include "utils/thread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>

using mcp::ThreadPool;

TEST(ThreadPoolTest, RunsTaskAndReturnsResult) {
    ThreadPool pool(2, 8);

    auto future = pool.Submit([] {
        return 42;
    });

    EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, PropagatesTaskExceptionsThroughFuture) {
    ThreadPool pool(1, 4);

    auto future = pool.Submit([]() -> int {
        throw std::runtime_error("boom");
    });

    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(ThreadPoolTest, RejectsTasksWhenQueueIsFull) {
    ThreadPool pool(1, 1);
    std::promise<void> first_task_started;
    auto first_task_has_started = first_task_started.get_future();
    std::promise<void> allow_first_task;
    auto first_task_allowed = allow_first_task.get_future().share();

    auto first = pool.Submit([&first_task_started, first_task_allowed] {
        first_task_started.set_value();
        first_task_allowed.wait();
    });
    first_task_has_started.wait();

    auto second = pool.Submit([] {});

    EXPECT_THROW(pool.Submit([] {}), mcp::ThreadPoolQueueFull);

    allow_first_task.set_value();
    first.get();
    second.get();
}

TEST(ThreadPoolTest, RejectsSubmitAfterShutdown) {
    ThreadPool pool(1, 4);

    pool.Shutdown();

    EXPECT_THROW(pool.Submit([] {}), std::runtime_error);
}

TEST(ThreadPoolTest, RejectsInvalidConfiguration) {
    EXPECT_THROW(ThreadPool(0, 4), std::invalid_argument);
    EXPECT_THROW(ThreadPool(1, 0), std::invalid_argument);
}

TEST(ThreadPoolTest, ReportsWorkerAndQueueMetrics) {
    ThreadPool pool(1, 2);
    std::promise<void> first_task_started;
    auto first_task_has_started = first_task_started.get_future();
    std::promise<void> allow_first_task;
    auto first_task_allowed = allow_first_task.get_future().share();

    auto first = pool.Submit([&first_task_started, first_task_allowed] {
        first_task_started.set_value();
        first_task_allowed.wait();
    });
    first_task_has_started.wait();
    auto second = pool.Submit([] {});

    EXPECT_EQ(pool.WorkerCount(), 1);
    EXPECT_EQ(pool.MaxQueueSize(), 2);
    EXPECT_EQ(pool.PendingTasks(), 1);

    allow_first_task.set_value();
    first.get();
    second.get();
}
