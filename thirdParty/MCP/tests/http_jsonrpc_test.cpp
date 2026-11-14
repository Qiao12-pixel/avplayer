#include <gtest/gtest.h>

#include <logger/logger.h>
#include <json_rpc/http_jsonrpc.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace mcp::jsonrpc;

class HttpJsonRpcServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mcp::logger::Logger::GetInstance().Shutdown();
        mcp::logger::Logger::GetInstance().Init("http_jsonrpc_test", "", 1024 * 1024, 3, false);
    }

    void TearDown() override {
        mcp::logger::Logger::GetInstance().Shutdown();
    }

    static HttpJsonRpcServer MakeServer(JsonRpcDispatcher dispatcher = JsonRpcDispatcher{}) {
        return HttpJsonRpcServer(std::move(dispatcher), "127.0.0.1", 0);
    }
};

TEST_F(HttpJsonRpcServerTest, ConstructorInitializesServerAndExposesAddress) {
    auto server = MakeServer();

    EXPECT_EQ(server.GetHost(), "127.0.0.1");
    EXPECT_EQ(server.GetPort(), 0);
    EXPECT_FALSE(server.IsRunning());
}

TEST_F(HttpJsonRpcServerTest, HandlesSingleRequest) {
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("add", [](const nlohmann::json& params) {
        return params.at("a").get<int>() + params.at("b").get<int>();
    });
    auto server = MakeServer(dispatcher);

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "add"},
        {"params", {{"a", 2}, {"b", 3}}}
    };

    auto response = nlohmann::json::parse(server.HandleRequest(request.dump()));

    EXPECT_EQ(response.at("jsonrpc"), "2.0");
    EXPECT_EQ(response.at("id"), 1);
    EXPECT_EQ(response.at("result"), 5);
}

TEST_F(HttpJsonRpcServerTest, HandlesNotificationWithoutResponseBody) {
    bool called = false;
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("notify", [&](const nlohmann::json& params) {
        called = params.at("ok").get<bool>();
        return nlohmann::json{{"ignored", true}};
    });
    auto server = MakeServer(dispatcher);

    nlohmann::json notification = {
        {"jsonrpc", "2.0"},
        {"method", "notify"},
        {"params", {{"ok", true}}}
    };

    EXPECT_TRUE(server.HandleRequest(notification.dump()).empty());
    EXPECT_TRUE(called);
}

TEST_F(HttpJsonRpcServerTest, ReturnsMethodNotFound) {
    auto server = MakeServer();

    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", "missing"},
        {"method", "missing"}
    };

    auto response = nlohmann::json::parse(server.HandleRequest(request.dump()));

    EXPECT_EQ(response.at("id"), "missing");
    EXPECT_EQ(response.at("error").at("code"), jsonrpc_error_code::MethodNotFound);
}

TEST_F(HttpJsonRpcServerTest, ReturnsParseErrorForInvalidJson) {
    auto server = MakeServer();

    auto response = nlohmann::json::parse(server.HandleRequest("{bad json"));

    EXPECT_EQ(response.at("jsonrpc"), "2.0");
    EXPECT_TRUE(response.at("id").is_null());
    EXPECT_EQ(response.at("error").at("code"), jsonrpc_error_code::ParseError);
}

TEST_F(HttpJsonRpcServerTest, ReturnsServerBusyWhenThreadPoolQueueIsFull) {
    JsonRpcDispatcher dispatcher;
    dispatcher.EnableThreadPool(1, 1);
    std::atomic_bool first_start_seen{false};
    std::promise<void> first_task_started;
    auto first_task_has_started = first_task_started.get_future();
    std::promise<void> allow_tasks;
    auto tasks_allowed = allow_tasks.get_future().share();

    dispatcher.RegisterHandler("tools/call", [&first_start_seen, &first_task_started, tasks_allowed](const nlohmann::json&) {
        if (!first_start_seen.exchange(true)) {
            first_task_started.set_value();
        }
        tasks_allowed.wait();
        return nlohmann::json{{"ok", true}};
    });
    auto server = MakeServer(dispatcher);

    const auto make_request = [](int id) {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", "tools/call"},
            {"params", nlohmann::json::object()}
        }.dump();
    };

    auto first = std::async(std::launch::async, [&server, &make_request] {
        return server.HandleRequest(make_request(1));
    });
    first_task_has_started.wait();
    auto second = std::async(std::launch::async, [&server, &make_request] {
        return server.HandleRequest(make_request(2));
    });
    for (int attempt = 0; attempt < 100 && dispatcher.ThreadPoolPendingTasks() == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(dispatcher.ThreadPoolPendingTasks(), 1);

    auto response = nlohmann::json::parse(server.HandleRequest(make_request(3)));

    EXPECT_EQ(response.at("id"), 3);
    EXPECT_EQ(response.at("error").at("code"), jsonrpc_error_code::ServerBusy);

    allow_tasks.set_value();
    EXPECT_EQ(nlohmann::json::parse(first.get()).at("result").at("ok"), true);
    EXPECT_EQ(nlohmann::json::parse(second.get()).at("result").at("ok"), true);
}

TEST_F(HttpJsonRpcServerTest, PreservesBatchResponseIdWhenThreadPoolQueueIsFull) {
    JsonRpcDispatcher dispatcher;
    dispatcher.EnableThreadPool(1, 1);
    std::atomic_bool first_start_seen{false};
    std::promise<void> first_task_started;
    auto first_task_has_started = first_task_started.get_future();
    std::promise<void> allow_tasks;
    auto tasks_allowed = allow_tasks.get_future().share();

    dispatcher.RegisterHandler("tools/call", [&first_start_seen, &first_task_started, tasks_allowed](const nlohmann::json&) {
        if (!first_start_seen.exchange(true)) {
            first_task_started.set_value();
        }
        tasks_allowed.wait();
        return nlohmann::json{{"ok", true}};
    });
    auto server = MakeServer(dispatcher);

    nlohmann::json batch = nlohmann::json::array({
        {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "tools/call"},
            {"params", nlohmann::json::object()}
        },
        {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"method", "tools/call"},
            {"params", nlohmann::json::object()}
        },
        {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"method", "tools/call"},
            {"params", nlohmann::json::object()}
        }
    });

    auto response_future = std::async(std::launch::async, [&server, batch] {
        return server.HandleRequest(batch.dump());
    });
    first_task_has_started.wait();
    for (int attempt = 0; attempt < 100 && dispatcher.ThreadPoolPendingTasks() == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(dispatcher.ThreadPoolPendingTasks(), 1);

    allow_tasks.set_value();

    auto response = nlohmann::json::parse(response_future.get());
    ASSERT_TRUE(response.is_array());
    ASSERT_EQ(response.size(), 3);
    EXPECT_EQ(response.at(0).at("id"), 1);
    EXPECT_EQ(response.at(1).at("id"), 2);
    EXPECT_EQ(response.at(2).at("id"), 3);
    EXPECT_EQ(response.at(2).at("error").at("code"), jsonrpc_error_code::ServerBusy);
}

TEST_F(HttpJsonRpcServerTest, SkipsBatchNotificationErrorsWhenThreadPoolQueueIsFull) {
    JsonRpcDispatcher dispatcher;
    dispatcher.EnableThreadPool(1, 1);
    std::atomic_bool first_start_seen{false};
    std::promise<void> first_task_started;
    auto first_task_has_started = first_task_started.get_future();
    std::promise<void> allow_tasks;
    auto tasks_allowed = allow_tasks.get_future().share();

    dispatcher.RegisterHandler("tools/call", [&first_start_seen, &first_task_started, tasks_allowed](const nlohmann::json&) {
        if (!first_start_seen.exchange(true)) {
            first_task_started.set_value();
        }
        tasks_allowed.wait();
        return nlohmann::json{{"ok", true}};
    });
    auto server = MakeServer(dispatcher);

    auto first = dispatcher.CallAsync("tools/call", nlohmann::json::object());
    first_task_has_started.wait();
    auto second = dispatcher.CallAsync("tools/call", nlohmann::json::object());
    ASSERT_EQ(dispatcher.ThreadPoolPendingTasks(), 1);

    nlohmann::json batch = nlohmann::json::array({
        {
            {"jsonrpc", "2.0"},
            {"method", "tools/call"},
            {"params", nlohmann::json::object()}
        }
    });

    EXPECT_TRUE(server.HandleRequest(batch.dump()).empty());

    allow_tasks.set_value();
    EXPECT_EQ(first.get().at("ok"), true);
    EXPECT_EQ(second.get().at("ok"), true);
}

TEST_F(HttpJsonRpcServerTest, RunsBatchPooledRequestsConcurrently) {
    JsonRpcDispatcher dispatcher;
    dispatcher.EnableThreadPool(2, 4);
    std::atomic_int started_count{0};
    std::promise<void> both_handlers_started;
    auto both_handlers_have_started = both_handlers_started.get_future();
    std::promise<void> allow_handlers;
    auto handlers_allowed = allow_handlers.get_future().share();

    dispatcher.RegisterHandler("tools/call", [&started_count, &both_handlers_started, handlers_allowed](const nlohmann::json&) {
        if (started_count.fetch_add(1) == 1) {
            both_handlers_started.set_value();
        }
        handlers_allowed.wait();
        return nlohmann::json{{"ok", true}};
    });
    auto server = MakeServer(std::move(dispatcher));

    nlohmann::json batch = nlohmann::json::array({
        {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "tools/call"},
            {"params", nlohmann::json::object()}
        },
        {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"method", "tools/call"},
            {"params", nlohmann::json::object()}
        }
    });

    auto response_future = std::async(std::launch::async, [&server, batch] {
        return server.HandleRequest(batch.dump());
    });

    ASSERT_EQ(both_handlers_have_started.wait_for(std::chrono::milliseconds(100)),
              std::future_status::ready);
    allow_handlers.set_value();

    auto response = nlohmann::json::parse(response_future.get());
    ASSERT_TRUE(response.is_array());
    ASSERT_EQ(response.size(), 2);
    EXPECT_EQ(response.at(0).at("result").at("ok"), true);
    EXPECT_EQ(response.at(1).at("result").at("ok"), true);
}

TEST_F(HttpJsonRpcServerTest, HandlesBatchAndSkipsNotifications) {
    bool notified = false;
    JsonRpcDispatcher dispatcher;
    dispatcher.RegisterHandler("echo", [](const nlohmann::json& params) {
        return params;
    });
    dispatcher.RegisterHandler("notify", [&](const nlohmann::json&) {
        notified = true;
        return nlohmann::json(nullptr);
    });
    auto server = MakeServer(std::move(dispatcher));

    nlohmann::json batch = nlohmann::json::array({
        {
            {"jsonrpc", "2.0"},
            {"id", 7},
            {"method", "echo"},
            {"params", {{"value", "pong"}}}
        },
        {
            {"jsonrpc", "2.0"},
            {"method", "notify"}
        }
    });

    auto response = nlohmann::json::parse(server.HandleRequest(batch.dump()));

    ASSERT_TRUE(response.is_array());
    ASSERT_EQ(response.size(), 1);
    EXPECT_EQ(response.at(0).at("id"), 7);
    EXPECT_EQ(response.at(0).at("result").at("value"), "pong");
    EXPECT_TRUE(notified);
}
