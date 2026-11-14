#include "zk/zk_service_registry.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <zookeeper/zookeeper.h>

#include "logger/logger.h"

namespace mcp {
namespace zk {

namespace {

constexpr const char* BASE_PATH = "/mcp-servers";

// Async completion helpers.
struct ZkStringResult {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    int rc = ZOK;
    std::string value;
};

struct ZkVoidResult {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    int rc = ZOK;
};

struct ZkDataResult {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    int rc = ZOK;
    std::string buffer;
};

struct ZkChildrenResult {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    int rc = ZOK;
    String_vector children{0, nullptr};
};

void SyncConnectedWatcher(zhandle_t*, int type, int state,
                          const char*, void* context) {
    if (type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE) {
        auto* flag = static_cast<std::atomic<bool>*>(context);
        flag->store(true);
    }
}

void StringCompletion(int rc, const char* value, const void* data) {
    auto* r = static_cast<ZkStringResult*>(const_cast<void*>(data));
    std::lock_guard<std::mutex> lock(r->mtx);
    r->rc = rc;
    if (value) r->value = value;
    r->done = true;
    r->cv.notify_all();
}

void VoidCompletion(int rc, const void* data) {
    auto* r = static_cast<ZkVoidResult*>(const_cast<void*>(data));
    std::lock_guard<std::mutex> lock(r->mtx);
    r->rc = rc;
    r->done = true;
    r->cv.notify_all();
}

void DataCompletion(int rc, const char* value, int value_len,
                    const Stat*, const void* data) {
    auto* r = static_cast<ZkDataResult*>(const_cast<void*>(data));
    std::lock_guard<std::mutex> lock(r->mtx);
    r->rc = rc;
    if (value && value_len > 0) r->buffer.assign(value, value_len);
    r->done = true;
    r->cv.notify_all();
}

void ChildrenCompletion(int rc, const String_vector* values, const void* data) {
    auto* r = static_cast<ZkChildrenResult*>(const_cast<void*>(data));
    std::lock_guard<std::mutex> lock(r->mtx);
    r->rc = rc;
    if (values && values->count > 0) {
        r->children.count = values->count;
        r->children.data = static_cast<char**>(
            malloc(sizeof(char*) * values->count));
        for (int i = 0; i < values->count; ++i) {
            r->children.data[i] = strdup(values->data[i]);
        }
    }
    r->done = true;
    r->cv.notify_all();
}

} // anonymous namespace

// -----------------------------------------------------------------------
// Public
// -----------------------------------------------------------------------

ZkServiceRegistry::ZkServiceRegistry(const std::string& hosts, int timeout_sec)
    : hosts_(hosts), timeout_ms_(timeout_sec * 1000) {}

ZkServiceRegistry::~ZkServiceRegistry() {
    Unregister();
    if (zh_ != nullptr) {
        zookeeper_close(zh_);
        zh_ = nullptr;
    }
}

void ZkServiceRegistry::Connect() {
    std::atomic<bool> connected_flag{false};
    zh_ = zookeeper_init(hosts_.c_str(), SyncConnectedWatcher, timeout_ms_,
                         nullptr, &connected_flag, 0);
    if (zh_ == nullptr) {
        throw std::runtime_error("zookeeper_init failed");
    }
    // Wait up to timeout_sec for connected state.
    for (int i = 0; i < timeout_ms_ / 100 + 1; ++i) {
        if (connected_flag.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!connected_flag.load()) {
        zookeeper_close(zh_);
        zh_ = nullptr;
        throw std::runtime_error("zookeeper connection timeout");
    }
    connected_ = true;
    MCP_LOG_INFO("ZooKeeper connected: {}", hosts_);
    EnsurePathExists(BASE_PATH);
    zoo_set_watcher(zh_, GlobalWatcher);
    zoo_set_context(zh_, this);
}

void ZkServiceRegistry::Register(const std::string& host, int port) {
    if (zh_ == nullptr || !connected_.load()) {
        throw std::runtime_error("ZooKeeper not connected");
    }
    std::ostringstream data;
    data << host << ":" << port;
    my_data_ = data.str();
    std::string node_path = std::string(BASE_PATH) + "/worker-" + host + "-"
                            + std::to_string(port) + "-";

    ZkStringResult result;
    int rc = zoo_acreate(zh_, node_path.c_str(), my_data_.c_str(),
                         static_cast<int>(my_data_.size()),
                         &ZOO_OPEN_ACL_UNSAFE,
                         ZOO_EPHEMERAL | ZOO_SEQUENCE,
                         StringCompletion, &result);
    if (rc != ZOK) {
        throw std::runtime_error(std::string("zoo_acreate failed: ") + zerror(rc));
    }
    // Wait for async completion.
    {
        std::unique_lock<std::mutex> lock(result.mtx);
        result.cv.wait_for(lock, std::chrono::seconds(5),
                           [&result] { return result.done; });
    }
    if (result.rc != ZOK) {
        throw std::runtime_error(std::string("zoo_acreate completion failed: ")
                                 + zerror(result.rc));
    }
    my_node_path_ = result.value;
    registered_ = true;
    MCP_LOG_INFO("Registered ZooKeeper node: {}", my_node_path_);
}

std::vector<InstanceInfo> ZkServiceRegistry::GetInstances() {
    std::vector<InstanceInfo> instances;
    if (zh_ == nullptr) return instances;

    ZkChildrenResult child_result;
    int rc = zoo_aget_children(zh_, BASE_PATH, 0, ChildrenCompletion, &child_result);
    if (rc != ZOK) {
        MCP_LOG_WARN("zoo_aget_children failed: {}", zerror(rc));
        return instances;
    }
    {
        std::unique_lock<std::mutex> lock(child_result.mtx);
        child_result.cv.wait_for(lock, std::chrono::seconds(5),
                                 [&child_result] { return child_result.done; });
    }
    if (child_result.rc != ZOK || child_result.children.count == 0) {
        return instances;
    }

    for (int i = 0; i < child_result.children.count; ++i) {
        std::string child_path = std::string(BASE_PATH) + "/"
                                 + child_result.children.data[i];

        ZkDataResult data_result;
        rc = zoo_aget(zh_, child_path.c_str(), 0, DataCompletion, &data_result);
        if (rc != ZOK) continue;
        {
            std::unique_lock<std::mutex> lock(data_result.mtx);
            data_result.cv.wait_for(lock, std::chrono::seconds(5),
                                    [&data_result] { return data_result.done; });
        }
        if (data_result.rc != ZOK || data_result.buffer.empty()) continue;

        auto colon = data_result.buffer.find(':');
        if (colon == std::string::npos) continue;

        InstanceInfo info;
        info.host = data_result.buffer.substr(0, colon);
        try {
            info.port = std::stoi(data_result.buffer.substr(colon + 1));
        } catch (...) { continue; }
        info.node_name = child_result.children.data[i];
        instances.push_back(std::move(info));
    }

    deallocate_String_vector(&child_result.children);
    return instances;
}

void ZkServiceRegistry::WatchInstances(InstanceListCallback cb) {
    instance_cb_ = std::move(cb);
    if (zh_ != nullptr) {
        // zoo_awget_children: async get children with a watcher.
        // We pass a noop completion because the watcher handles updates.
        auto noop = [](int, const String_vector*, const void*) {};
        zoo_awget_children(zh_, BASE_PATH, ChildWatcher, this, noop, nullptr);
    }
}

void ZkServiceRegistry::Unregister() {
    if (!my_node_path_.empty() && zh_ != nullptr) {
        ZkVoidResult result;
        zoo_adelete(zh_, my_node_path_.c_str(), -1, VoidCompletion, &result);
        {
            std::unique_lock<std::mutex> lock(result.mtx);
            result.cv.wait_for(lock, std::chrono::seconds(2),
                               [&result] { return result.done; });
        }
        MCP_LOG_INFO("Unregistered ZooKeeper node: {}", my_node_path_);
        my_node_path_.clear();
        registered_ = false;
    }
}

bool ZkServiceRegistry::IsConnected() const { return connected_.load(); }
bool ZkServiceRegistry::IsRegistered() const { return registered_.load(); }

// -----------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------

void ZkServiceRegistry::EnsurePathExists(const std::string& path) {
    if (zh_ == nullptr) return;
    // Try to create; if already exists, that's fine.
    ZkStringResult result;
    int rc = zoo_acreate(zh_, path.c_str(), nullptr, -1,
                         &ZOO_OPEN_ACL_UNSAFE, 0,
                         StringCompletion, &result);
    if (rc == ZOK) {
        std::unique_lock<std::mutex> lock(result.mtx);
        result.cv.wait_for(lock, std::chrono::seconds(5),
                           [&result] { return result.done; });
        // ZNODEEXISTS is OK — path already exists.
        if (result.rc != ZOK && result.rc != ZNODEEXISTS) {
            throw std::runtime_error(std::string("create base path failed: ")
                                     + zerror(result.rc));
        }
    }
}

void ZkServiceRegistry::OnConnected() {
    connected_ = true;
    MCP_LOG_INFO("ZooKeeper session established");
}

void ZkServiceRegistry::OnDisconnected() {
    connected_ = false;
    registered_ = false;
    MCP_LOG_WARN("ZooKeeper session lost");
}

void ZkServiceRegistry::OnChildChanged() {
    auto instances = GetInstances();
    if (instance_cb_) {
        instance_cb_(instances);
    }
}

// -----------------------------------------------------------------------
// Static watchers
// -----------------------------------------------------------------------

void ZkServiceRegistry::GlobalWatcher(zhandle_t* /*zh*/, int type, int state,
                                       const char* /*path*/, void* context) {
    auto* self = static_cast<ZkServiceRegistry*>(context);
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            self->OnConnected();
        } else if (state == ZOO_EXPIRED_SESSION_STATE ||
                   state == ZOO_CONNECTING_STATE) {
            self->OnDisconnected();
        }
    }
}

void ZkServiceRegistry::ChildWatcher(zhandle_t* zh, int type, int state,
                                      const char* /*path*/, void* context) {
    auto* self = static_cast<ZkServiceRegistry*>(context);
    if (type == ZOO_CHILD_EVENT && state == ZOO_CONNECTED_STATE) {
        self->OnChildChanged();
        // Re-register the watch for future changes.
        auto noop = [](int, const String_vector*, const void*) {};
        zoo_awget_children(zh, BASE_PATH, ChildWatcher, self, noop, nullptr);
    }
}

} // namespace zk
} // namespace mcp
