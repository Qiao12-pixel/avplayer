#ifndef ZK_SERVICE_REGISTRY_H
#define ZK_SERVICE_REGISTRY_H

#include <atomic>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward declaration for opaque zhandle_t pointer (full type in zookeeper.h).
struct _zhandle;
typedef struct _zhandle zhandle_t;

namespace mcp {
namespace zk {

struct InstanceInfo {
    std::string host;
    int port = 0;
    std::string node_name;
};

class ZkServiceRegistry {
public:
    using InstanceListCallback = std::function<void(const std::vector<InstanceInfo>&)>;

    ZkServiceRegistry(const std::string& hosts, int timeout_sec = 10);
    ~ZkServiceRegistry();

    // Non-copyable.
    ZkServiceRegistry(const ZkServiceRegistry&) = delete;
    ZkServiceRegistry& operator=(const ZkServiceRegistry&) = delete;

    // Connect to ZooKeeper and wait for session.
    void Connect();

    // Register current instance as an ephemeral sequential node.
    // Path: /mcp-servers/worker-{host}-{port}-
    void Register(const std::string& host, int port);

    // Get all registered instances.
    std::vector<InstanceInfo> GetInstances();

    // Watch for child node changes. cb is called on each change.
    void WatchInstances(InstanceListCallback cb = nullptr);

    // Unregister (delete own node).
    void Unregister();

    bool IsConnected() const;
    bool IsRegistered() const;

private:
    static void GlobalWatcher(zhandle_t* zh, int type, int state,
                              const char* path, void* context);
    static void ChildWatcher(zhandle_t* zh, int type, int state,
                             const char* path, void* context);

    void EnsurePathExists(const std::string& path);
    void OnConnected();
    void OnDisconnected();
    void OnChildChanged();

    std::string hosts_;
    int timeout_ms_;
    zhandle_t* zh_ = nullptr;
    std::string my_node_path_;
    std::string my_data_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> registered_{false};
    mutable std::mutex mutex_;
    InstanceListCallback instance_cb_;
};

} // namespace zk
} // namespace mcp

#endif // ZK_SERVICE_REGISTRY_H
