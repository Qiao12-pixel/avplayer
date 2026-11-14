#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <httplib.h>

#include "zk/zk_service_registry.h"

namespace mcp {
namespace lb {

struct BackendInstance {
    std::string host = "127.0.0.1";
    int port = 0;
    bool alive = true;
    int weight = 1;
    std::string node_name;
};

struct NodeState {
    int effective_weight = 1;
    int current_weight = 0;
};

class LoadBalancer {
public:
    LoadBalancer(int listen_port, const std::string& zk_hosts, int default_weight = 1);
    ~LoadBalancer();

    LoadBalancer(const LoadBalancer&) = delete;
    LoadBalancer& operator=(const LoadBalancer&) = delete;

    void Start();
    void Stop();

    void SetWeight(const std::string& host, int port, int weight);
    void SetDefaultWeight(int weight);

private:
    void RefreshBackends();
    void ForwardJsonRpc(const httplib::Request& req, httplib::Response& res);
    void ForwardSseEvents(const httplib::Request& req, httplib::Response& res);
    void ForwardSseToolCalls(const httplib::Request& req, httplib::Response& res);
    BackendInstance* PickBackend();
    void HealthCheckLoop();
    void OnInstancesChanged(const std::vector<zk::InstanceInfo>& instances);

    std::string InstanceKey(const BackendInstance& inst) const;
    std::string InstanceKey(const std::string& host, int port) const;

    int listen_port_;
    std::string zk_hosts_;
    int default_weight_;
    std::unique_ptr<httplib::Server> server_;
    std::unique_ptr<zk::ZkServiceRegistry> zk_;
    std::vector<BackendInstance> backends_;
    std::unordered_map<std::string, NodeState> weights_;
    std::shared_mutex rw_mutex_;
    std::thread health_check_thread_;
    std::atomic<bool> running_{false};
};

} // namespace lb
} // namespace mcp

#endif // LOAD_BALANCER_H
