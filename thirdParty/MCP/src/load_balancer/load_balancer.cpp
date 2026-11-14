#include "load_balancer/load_balancer.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <nlohmann/json.hpp>

#include "logger/logger.h"

namespace mcp {
namespace lb {

using json = nlohmann::json;

LoadBalancer::LoadBalancer(int listen_port, const std::string& zk_hosts, int default_weight)
    : listen_port_(listen_port), zk_hosts_(zk_hosts), default_weight_(default_weight) {}

LoadBalancer::~LoadBalancer() {
    Stop();
}

void LoadBalancer::Start() {
    zk_ = std::make_unique<zk::ZkServiceRegistry>(zk_hosts_);
    zk_->Connect();
    zk_->WatchInstances([this](const auto& instances) {
        OnInstancesChanged(instances);
    });
    RefreshBackends();

    if (backends_.empty()) {
        MCP_LOG_WARN("No backends registered in ZooKeeper; LB will return 503");
    }

    server_ = std::make_unique<httplib::Server>();

    server_->Post("/jsonrpc", [this](const httplib::Request& req, httplib::Response& res) {
        ForwardJsonRpc(req, res);
    });
    server_->Get("/sse/events", [this](const httplib::Request& req, httplib::Response& res) {
        ForwardSseEvents(req, res);
    });
    server_->Get("/sse/tool_calls", [this](const httplib::Request& req, httplib::Response& res) {
        ForwardSseToolCalls(req, res);
    });
    server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","role":"proxy"})", "application/json");
    });

    running_ = true;
    health_check_thread_ = std::thread([this] { HealthCheckLoop(); });

    MCP_LOG_INFO("Load balancer listening on port {} (default weight={})",
                 listen_port_, default_weight_);
    server_->listen("0.0.0.0", listen_port_);
}

void LoadBalancer::Stop() {
    running_ = false;
    if (server_) server_->stop();
    if (health_check_thread_.joinable()) health_check_thread_.join();
}

// -----------------------------------------------------------------------
// Weight management
// -----------------------------------------------------------------------

void LoadBalancer::SetWeight(const std::string& host, int port, int weight) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    weights_[InstanceKey(host, port)] = {weight, 0};
}

void LoadBalancer::SetDefaultWeight(int weight) {
    default_weight_ = weight;
}

std::string LoadBalancer::InstanceKey(const BackendInstance& inst) const {
    return inst.host + ":" + std::to_string(inst.port);
}

std::string LoadBalancer::InstanceKey(const std::string& host, int port) const {
    return host + ":" + std::to_string(port);
}

// -----------------------------------------------------------------------
// Backend management
// -----------------------------------------------------------------------

void LoadBalancer::RefreshBackends() {
    if (!zk_) return;
    auto instances = zk_->GetInstances();
    OnInstancesChanged(instances);
}

void LoadBalancer::OnInstancesChanged(const std::vector<zk::InstanceInfo>& instances) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);

    std::vector<BackendInstance> new_backends;
    for (const auto& inst : instances) {
        BackendInstance be;
        be.host = inst.host;
        be.port = inst.port;
        be.node_name = inst.node_name;
        be.alive = true;

        // Preserve alive status and get weight.
        for (const auto& existing : backends_) {
            if (existing.host == be.host && existing.port == be.port) {
                be.alive = existing.alive;
                break;
            }
        }

        // Use configured weight or default.
        auto it = weights_.find(InstanceKey(be));
        be.weight = (it != weights_.end()) ? it->second.effective_weight : default_weight_;

        // Ensure weight is at least 1.
        if (be.weight < 1) be.weight = 1;

        new_backends.push_back(std::move(be));
    }

    std::sort(new_backends.begin(), new_backends.end(),
              [](const BackendInstance& a, const BackendInstance& b) {
                  return a.port < b.port;
              });

    backends_ = std::move(new_backends);
    MCP_LOG_INFO("Backends updated: {} instances", backends_.size());
}

// -----------------------------------------------------------------------
// Smooth Weighted Round-Robin (Nginx algorithm)
// -----------------------------------------------------------------------

BackendInstance* LoadBalancer::PickBackend() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);

    if (backends_.empty()) return nullptr;

    // Initialize weights for new backends.
    for (const auto& be : backends_) {
        std::string key = InstanceKey(be);
        if (weights_.find(key) == weights_.end()) {
            NodeState ns;
            ns.effective_weight = be.weight > 0 ? be.weight : default_weight_;
            ns.current_weight = 0;
            weights_[key] = ns;
        }
    }

    int total_weight = 0;
    BackendInstance* best = nullptr;
    int max_current = std::numeric_limits<int>::min();

    for (auto& be : backends_) {
        if (!be.alive) continue;

        auto& state = weights_[InstanceKey(be)];
        state.current_weight += state.effective_weight;
        total_weight += state.effective_weight;

        if (state.current_weight > max_current) {
            max_current = state.current_weight;
            best = &be;
        }
    }

    if (best == nullptr) return nullptr;  // All dead.

    weights_[InstanceKey(*best)].current_weight -= total_weight;
    return best;
}

// -----------------------------------------------------------------------
// Request forwarding
// -----------------------------------------------------------------------

void LoadBalancer::ForwardJsonRpc(const httplib::Request& req, httplib::Response& res) {
    auto* backend = PickBackend();
    if (backend == nullptr) {
        json error = {
            {"jsonrpc", "2.0"},
            {"error", {{"code", -32000}, {"message", "No available backends"}}},
            {"id", nullptr},
        };
        res.status = 503;
        res.set_content(error.dump(), "application/json");
        return;
    }

    std::string backend_url = "http://" + backend->host + ":" + std::to_string(backend->port);
    httplib::Client client(backend_url);

    auto result = client.Post("/jsonrpc", req.body, "application/json");
    if (result && result->status == 200) {
        res.set_content(result->body, "application/json");
    } else {
        backend->alive = false;
        MCP_LOG_WARN("Backend {}:{} unreachable, marking dead", backend->host, backend->port);
        ForwardJsonRpc(req, res);
    }
}

void LoadBalancer::ForwardSseEvents(const httplib::Request& /*req*/, httplib::Response& res) {
    auto* backend = PickBackend();
    if (backend == nullptr) {
        res.status = 503;
        res.set_content("No available backends", "text/plain");
        return;
    }
    std::string backend_url = "http://" + backend->host + ":" + std::to_string(backend->port);
    httplib::Client client(backend_url);
    auto result = client.Get("/sse/events");
    if (result) {
        res.set_content("SSE streamed from " + backend_url, "text/plain");
    } else {
        res.status = 502;
        res.set_content("Backend SSE failed", "text/plain");
    }
}

void LoadBalancer::ForwardSseToolCalls(const httplib::Request& /*req*/, httplib::Response& res) {
    auto* backend = PickBackend();
    if (backend == nullptr) {
        res.status = 503;
        res.set_content("No available backends", "text/plain");
        return;
    }
    std::string backend_url = "http://" + backend->host + ":" + std::to_string(backend->port);
    httplib::Client client(backend_url);
    auto result = client.Get("/sse/tool_calls");
    if (result) {
        res.set_content("OK", "text/plain");
    } else {
        res.status = 502;
        res.set_content("Backend SSE failed", "text/plain");
    }
}

// -----------------------------------------------------------------------
// Health check
// -----------------------------------------------------------------------

void LoadBalancer::HealthCheckLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        for (auto& backend : backends_) {
            std::string url = "http://" + backend.host + ":" + std::to_string(backend.port);
            httplib::Client client(url);
            client.set_connection_timeout(2);
            client.set_read_timeout(2);

            auto result = client.Get("/health");
            bool was_alive = backend.alive;
            backend.alive = (result && result->status == 200);

            if (was_alive && !backend.alive) {
                MCP_LOG_WARN("Backend {}:{} went DOWN", backend.host, backend.port);
            } else if (!was_alive && backend.alive) {
                MCP_LOG_INFO("Backend {}:{} is BACK UP", backend.host, backend.port);
            }
        }
    }
}

} // namespace lb
} // namespace mcp
