/**
 * @file http_jsonrpc.cpp
 * @brief 
 * @author Joe
 * @date 26-5-1
 */


#include "http_jsonrpc.h"
#include <future>
#include <httplib.h>
#include <vector>

#include "config/config.h"
#include "jsonrpc_serialization.h"
#include "logger/logger.h"
#include "utils/thread_pool.h"

namespace mcp {
    namespace jsonrpc {
        class HttpJsonRpcServer::Pimpl {
        public:
            httplib::Server server;

            Pimpl() {
                //设置日志回调
                server.set_logger([](const httplib::Request &req, const httplib::Response &res) {
                    MCP_LOG_INFO("HTTP {} {} -> {}", req.method, req.path, res.status);
                });
                //设置错误处理
                server.set_error_handler([](const httplib::Request& /*req*/, httplib::Response &res) {
                    res.set_content(DumpResponse(MakeErrorResponse(
                        nullptr,
                        jsonrpc_error_code::InternalError,
                        "Internal server error"
                    )), "application/json");
                });
            }
        };
        HttpJsonRpcServer::HttpJsonRpcServer(JsonRpcDispatcher dispatcher)
            : HttpJsonRpcServer(std::move(dispatcher), "0.0.0.0", config::MCP_CONFIG.GetServerPort()){
            MCP_LOG_INFO("Http JSON-RPC server initialized from config");
        }
        std::string HttpJsonRpcServer::HandleRequest(const std::string &request_body) {
            MCP_LOG_DEBUG("Request body: {}", request_body);
            try {
                //解析json
                json request_json = json::parse(request_body);

                //检查是否为批量请求
                if (request_json.is_array()) {
                    struct PendingBatchResponse {
                        JsonRpcResponse response;
                        std::future<json> future;
                        bool has_future = false;
                    };

                    std::vector<PendingBatchResponse> pending_responses;
                    for (const auto& single_req_json : request_json) {
                        bool is_notification = false;
                        std::optional<json> request_id;
                        try {
                            if (single_req_json.is_object() && single_req_json.contains("id")) {
                                request_id = single_req_json.at("id");
                            }
                            //从Json解析请求==>构建JsonRpcRequest
                            JsonRpcRequest req;
                            req.jsonrpc = single_req_json.value("jsonrpc", std::string("2.0"));
                            req.method = single_req_json.at("method").get<std::string>();
                            if (single_req_json.contains("params")) {
                                req.params = single_req_json.at("params");
                            }
                            if (request_id.has_value()) {
                                req.id = request_id.value();
                            }
                            //处理请求
                            if (!req.id.has_value()) {
                                is_notification = true;
                                //通知请求
                                if (m_dispatcher_.HasHandler(req.method)) {
                                    m_dispatcher_.CallAsync(req.method, req.params.value_or(json::object()));
                                }
                                continue;
                            }
                            //响应
                            JsonRpcResponse res;
                            res.jsonrpc = "2.0";
                            res.id = req.id.value();

                            try {
                                if (!m_dispatcher_.HasHandler(req.method)) {
                                    res.error = JsonRpcError{
                                        .code = jsonrpc_error_code::MethodNotFound,
                                        .message = "Method not found: " + req.method,
                                        .data = std::nullopt,
                                    };
                                } else {
                                    auto future = m_dispatcher_.CallAsync(req.method, req.params.value_or(json::object()));
                                    pending_responses.emplace_back(PendingBatchResponse{
                                            .response = std::move(res),
                                            .future = std::move(future),
                                            .has_future = true,
                                    });
                                    continue;
                                }
                            } catch (const ThreadPoolQueueFull &e) {
                                res.error = JsonRpcError{
                                .code = jsonrpc_error_code::ServerBusy,
                                .message = e.what(),
                                .data = std::nullopt,
                                };
                            } catch (const std::exception &e) {
                                res.error = JsonRpcError{
                                .code = jsonrpc_error_code::InternalError,
                                .message = e.what(),
                                .data = std::nullopt,
                                };
                            }
                            pending_responses.emplace_back(PendingBatchResponse{
                                    .response = std::move(res),
                            });
                        } catch (const std::exception &e) {
                            if (is_notification) {
                                MCP_LOG_ERROR("Error handling batch notification: {}", e.what());
                                continue;
                            }
                            MCP_LOG_ERROR("Error in batch request: {}", e.what());
                            JsonRpcResponse error_res;
                            error_res.jsonrpc = "2.0";
                            error_res.id = request_id.value_or(nullptr);
                            error_res.error = JsonRpcError{
                                    .code = jsonrpc_error_code::InternalError,
                                    .message = e.what(),
                                    .data = std::nullopt,
                            };
                            pending_responses.emplace_back(PendingBatchResponse{
                                    .response = std::move(error_res),
                            });
                        }
                    }

                    if (pending_responses.empty()) {
                        return "";
                    }

                    json batch_json = json::array();
                    for (auto& pending : pending_responses) {
                        if (pending.has_future) {
                            try {
                                pending.response.result = pending.future.get();
                            } catch (const ThreadPoolQueueFull &e) {
                                pending.response.error = JsonRpcError{
                                .code = jsonrpc_error_code::ServerBusy,
                                .message = e.what(),
                                .data = std::nullopt,
                                };
                            } catch (const std::exception &e) {
                                pending.response.error = JsonRpcError{
                                .code = jsonrpc_error_code::InternalError,
                                .message = e.what(),
                                .data = std::nullopt,
                                };
                            }
                        }
                        batch_json.emplace_back(SerializeResponse(pending.response));
                    }
                    std::string response = batch_json.dump();
                    MCP_LOG_DEBUG("Batch Response: {}", response);
                    return response;
                }
                //处理单个请求
                JsonRpcRequest request;
                request.jsonrpc = request_json.value("jsonrpc", std::string("2.0"));
                request.method = request_json.at("method").get<std::string>();

                if (request_json.contains("id")) {
                    request.id = request_json.at("id");
                }
                if (request_json.contains("params")) {
                    request.params = request_json.at("params");
                }
                if (!request.id.has_value()) {
                    //通知请求，不返回响应
                    if (m_dispatcher_.HasHandler(request.method)) {
                        m_dispatcher_.Call(request.method, request.params.value_or(json::object()));
                    }
                    MCP_LOG_DEBUG("Notification request, no response");
                    return "";
                }
                //构造响应
                JsonRpcResponse response;
                response.jsonrpc = "2.0";
                response.id = request.id.value();
                try {
                    if (!m_dispatcher_.HasHandler(request.method)) {
                        response.error = JsonRpcError{
                        .code = jsonrpc_error_code::MethodNotFound,
                        .message = "Method not found: " + request.method,
                        .data = std::nullopt,
                        };
                    } else {
                        response.result = m_dispatcher_.Call(request.method, request.params.value_or(json::object()));
                    }
                } catch (const ThreadPoolQueueFull &e) {
                    response.error = JsonRpcError{
                    .code = jsonrpc_error_code::ServerBusy,
                    .message = e.what(),
                    .data = std::nullopt,
                    };
                } catch (const std::exception &e) {
                    response.error = JsonRpcError{
                    .code = jsonrpc_error_code::InternalError,
                    .message = e.what(),
                    .data = std::nullopt,
                    };
                }
                return DumpResponse(response);
            } catch (const json::parse_error& e) {
                MCP_LOG_ERROR("Parse error: {}", e.what());
                return DumpResponse(MakeErrorResponse(
                    nullptr,
                    jsonrpc_error_code::ParseError,
                    std::string("Parse error: ") + e.what()
                ));
            } catch (const std::exception& e) {
                MCP_LOG_ERROR("Error handling request: {}", e.what());
                return DumpResponse(MakeErrorResponse(
                    nullptr,
                    jsonrpc_error_code::InternalError,
                    std::string("Internal error: ") + e.what()
                ));
            }
        }
        HttpJsonRpcServer::HttpJsonRpcServer(JsonRpcDispatcher dispatcher, const std::string &host, int port)
            : m_dispatcher_(std::move(dispatcher)),
              m_host_(host),
              m_port_(port),
              m_pimpl_(std::make_unique<Pimpl>()) {
            MCP_LOG_INFO("HTTP JSON-RPC server created on {}:{}", m_host_, m_port_);

            //注册POST/jsonrpc端点
            m_pimpl_->server.Post("/jsonrpc", [this](const httplib::Request &req, httplib::Response &res) {
                MCP_LOG_DEBUG("Received JSON-RPC request, body size: {}", req.body.size());

                //设置CORS头
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
                res.set_header("Access-Control-Allow-Headers", "Content-Type");

                try {
                    std::string response = HandleRequest(req.body);
                    res.set_content(response, "application/json");
                    res.status = 200;
                } catch (const std::exception &e) {
                    MCP_LOG_ERROR("Error handling request: {}", e.what());
                    res.set_content(DumpResponse(MakeErrorResponse(
                        nullptr,
                        jsonrpc_error_code::InternalError,
                        e.what()
                    )), "application/json");
                    res.status = 500;
                }
            });

            //处理OPTIONS请求【CORS预检】
            m_pimpl_->server.Options("/jsonrpc", [](const httplib::Request& /*req*/, httplib::Response& res) {
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
                res.set_header("Access-Control-Allow-Headers", "Content-Type");
                res.status = 204;
            });
            //注册健康节点检测
            m_pimpl_->server.Get("/health", [](const httplib::Request& /*req*/, httplib::Response& res) {
                json health = {
                    {"status", "ok"},
                    {"service", "mcp-http-jsonrpc"}
                };
                res.set_content(health.dump(), "application/json");
            });
            //注册根路径(返回服务信息)
            m_pimpl_->server.Get("/", [this](const httplib::Request& /*req*/, httplib::Response& res) {
                json info = {
                    {"service", "MCP HTTP JSON-RPC Server"},
                    {"version", "1.0.0"},
                    {"endpoints", {
                        {{"path", "/jsonrpc"}, {"method", "POST"}, {"description", "JSON-RPC 2.0 endpoint"}},
                        {{"path", "/health"}, {"method", "GET"}, {"description", "Health check"}},
                        {{"path", "/sse/events"}, {"method", "GET"}, {"description", "Server status event stream (SSE)"}},
                        {{"path", "/sse/tool_calls"}, {"method", "GET"}, {"description", "Tool call monitoring stream (SSE)"}},
                        {{"path", "/"}, {"method", "GET"}, {"description", "Server information"}}
                    }}
                };
                res.set_content(info.dump(2), "application/json");
            });
        }

        void HttpJsonRpcServer::RegisterSseEndpoint(const std::string &path, SseCallback callback) {
            m_pimpl_->server.Get(path, [callback](const httplib::Request& /*req*/, httplib::Response& res) {
                res.set_header("Content-Type", "text/event-stream");
                res.set_header("Cache-Control", "no-cache");
                res.set_header("Connection", "keep-alive");
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("X-Accel-Buffering", "no");

                res.set_chunked_content_provider("text/event-stream", [callback](size_t /*offset*/, httplib::DataSink& sink) {
                    auto send_event = [&sink](const std::string& data) {
                        std::string event = "data: " + data + "\n\n";
                        sink.write(event.c_str(), event.size());
                    };
                    try {
                        callback(send_event);
                    } catch (const std::exception &e) {
                        MCP_LOG_ERROR("Error handling request: {}", e.what());
                    }
                    return true;
                });
            });
        }
        void HttpJsonRpcServer::Stop() {
            if (!m_running_.exchange(false)) {//修改并返回修改前的值
                return;
            }
            MCP_LOG_INFO("Stopping HTTP JSON-RPC server");
            m_pimpl_->server.stop();
        }

        HttpJsonRpcServer::~HttpJsonRpcServer() {
            Stop();
        }
        bool HttpJsonRpcServer::IsRunning() const {
            return m_running_.load();
        }
        const std::string &HttpJsonRpcServer::GetHost() const {
            return m_host_;
        }
        int HttpJsonRpcServer::GetPort() const {
            return m_port_;
        }
        void HttpJsonRpcServer::Run() {
            if (m_running_.exchange(true)) {
                MCP_LOG_INFO("Already Running HTTP JSON-RPC server");
                return;
            }
            MCP_LOG_INFO("Starting HTTP JSON-RPC server: {}, {}", m_host_, m_port_);

            if (!m_pimpl_->server.listen(m_host_, m_port_)) {
                m_running_ = false;
                throw std::runtime_error("Failed to start HTTP JSON-RPC server on" + m_host_ + ":" + std::to_string(m_port_));
            }
            MCP_LOG_INFO("HTTP JSON-RPC server stopped");
            m_running_ = false;
        }









    }
}
