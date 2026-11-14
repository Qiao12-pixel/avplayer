/**
 * @file Stdio_jsonrpc_server.cpp
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#include "stdio_jsonrpc_server.h"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include "logger/logger.h"
#include "jsonrpc_serialization.h"
namespace mcp {
    namespace jsonrpc {
        StdioJsonRpcServer::StdioJsonRpcServer(JsonRpcDispatcher dispatcher) : m_dispatcher_(std::move(dispatcher)) {}
        StdioJsonRpcServer::StdioJsonRpcServer(JsonRpcDispatcher dispatcher, std::istream &in, std::ostream &out)
        : m_dispatcher_(std::move(dispatcher)), m_in_(in), m_out_(out) {}
        // 读取一条完整的 JSON-RPC 消息
        /**
         *
        *   Content-Length: 123\r\n
            \r\n
            {"jsonrpc":"2.0","method":"test","id":1}
         */
        bool StdioJsonRpcServer::ReadMessage(std::string &out_body) {
            out_body.clear();

            std::string line;
            size_t content_length = 0;
            bool found_content_length = false;

            while (std::getline(m_in_, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                // 空行表示头部结束，后面就是消息体
                if (line.empty()) {
                    break;
                }
                auto colon = line.find_first_of(':');
                if (colon == std::string::npos) {
                    continue;
                }
                //分割字符串
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 1);//长度
                std::transform(key.begin(), key.end(), key.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                //去掉长度前的空格
                size_t pos = value.find_first_not_of(' ');//找第一个不是空格的字符
                if (pos != std::string::npos) {
                    MCP_LOG_DEBUG("Value: {}", value);
                    value = value.substr(pos);
                }
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);

                if (key == "content-length") {
                    try {
                        content_length = static_cast<size_t>(std::stoul(value));
                        found_content_length = true;
                    } catch (...) {
                        MCP_LOG_ERROR("Invalid parse content-length: {}", value);
                        return false;
                    }
                } else if (key == "content-type") {
                    MCP_LOG_DEBUG("Content-Type: {}", value);
                } else {
                    MCP_LOG_DEBUG("Ignore header: {}: {}", key, value);
                }
            }
            MCP_LOG_INFO("Found Content-Length: {}", found_content_length);
            MCP_LOG_INFO("Content-Length: {}", content_length);

            if (!found_content_length) {
                return false;
            }
            if (content_length == 0) {
                return true;
            }

            //按照长度读取消息体
            out_body.resize(content_length);
            size_t total_read = 0;

            while (total_read < content_length) {
                const std::streamsize to_read = static_cast<std::streamsize>(content_length - total_read);
                m_in_.read(&out_body[total_read], to_read);
                const std::streamsize bytes_read = m_in_.gcount();
                if (bytes_read <= 0) {
                    break;
                }
                total_read += static_cast<size_t>(bytes_read);
                if (!m_in_.eof() && !m_in_.good()) {
                    break;
                }
            }
            // 改进：添加日志，便于调试
            if (total_read != content_length) {
                MCP_LOG_ERROR("Incomplete message: expected {} bytes, got {}", content_length, total_read);
                return false;
            }
            return true;
        }
        //把长度写入信息体中
        void StdioJsonRpcServer::WriteMessage(const json &msg) {
            std::string payload = msg.dump();
            m_out_ << "Content-Length: " << payload.size() << "\r\n\r\n";
            m_out_ << payload;
            m_out_.flush();
        }

        /**
         * 处理单个 JSON-RPC 请求
         * 错误码: -32700(Parse), -32600(Invalid), -32601(NotFound), -32602(Params), -32603(Internal)
         * @param req
         * @return
         */
        JsonRpcResponse StdioJsonRpcServer::HandleRequest(const JsonRpcRequest &req) {
            JsonRpcResponse res;
            res.jsonrpc = "2.0";
            if (req.id.has_value()) {
                res.id = req.id.value();
            } else {
                res.id = nullptr;
            }

            try {
                //验证请求模式
                if (req.jsonrpc != "2.0") {
                    throw std::invalid_argument("Invalid JSON RPC request, jsonrpc must be 2.0");
                }
                if (req.method.empty()) {
                    throw std::invalid_argument("Invalid JSON RPC request, method missing");
                }

                const std::string method = req.method;
                json params = req.params.has_value() ? req.params.value() : json::object();

                MCP_LOG_DEBUG("Handle Method: {}", method);

                if (!m_dispatcher_.HasHandler(method)) {
                    res.error = JsonRpcError{jsonrpc_error_code::MethodNotFound, "Method not found", std::nullopt};
                    return res;
                }

                try {
                    json result = m_dispatcher_.Call(method, params);
                    res.result = std::move(result);
                    res.error.reset();
                } catch (const std::invalid_argument& ex) {
                    res.result.reset();
                    res.error = JsonRpcError{jsonrpc_error_code::InvalidRequest, ex.what(), std::nullopt};
                } catch (const std::exception& ex) {
                    res.result.reset();
                    res.error = JsonRpcError{jsonrpc_error_code::InternalError, ex.what(), std::nullopt};
                }
            } catch (const std::invalid_argument& ex) {
                res.result.reset();
                res.error = JsonRpcError{jsonrpc_error_code::InvalidRequest, ex.what(), std::nullopt};
            } catch (const json::parse_error& ex) {
                // 区分 JSON 解析错误
                res.result.reset();
                res.error = JsonRpcError{jsonrpc_error_code::ParseError, ex.what(), std::nullopt};
            } catch (const std::exception& ex) {
                res.result.reset();
                res.error = JsonRpcError{jsonrpc_error_code::InternalError, ex.what(), std::nullopt};
            }
            return res;
        }
        void StdioJsonRpcServer::Run() {
            MCP_LOG_INFO("JSON-RPC stdio Starting...");
            std::string body;
            while (true) {
                if (!ReadMessage(body)) {
                    if (m_in_.eof()) {//输入流关闭
                        MCP_LOG_INFO("stdin EOF reachedm exiting");
                        break;
                    }
                    continue;
                }
                try {
                    json j = json::parse(body);
                    JsonRpcRequest req;
                    try {
                        req = j.get<JsonRpcRequest>();
                    } catch (const std::exception& ex) {
                        JsonRpcResponse res;
                        res.jsonrpc = "2.0";
                        res.id = j.contains("id") ? j["id"] : nullptr;
                        res.error = JsonRpcError{jsonrpc_error_code::InvalidRequest, ex.what(), std::nullopt};
                        res.result.reset();
                        WriteMessage(json(res));
                        continue;
                    }
                    const bool is_notification = !req.id.has_value();
                    auto res = HandleRequest(req);
                    if (!is_notification) {
                        WriteMessage(json(res));
                    }
                } catch (const json::parse_error& ex) {
                    JsonRpcResponse res;
                    res.jsonrpc = "2.0";
                    res.id = nullptr;
                    res.error = JsonRpcError{jsonrpc_error_code::ParseError, ex.what(), std::nullopt};
                    WriteMessage(json(res));
                }
            }
        }

    }
}
