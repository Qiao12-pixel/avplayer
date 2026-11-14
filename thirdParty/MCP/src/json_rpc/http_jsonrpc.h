/**
 * @file http_jsonrpc.h
 * @brief 
 * @author Joe
 * @date 26-5-1
 */


#ifndef HTTP_JSONRPC_H
#define HTTP_JSONRPC_H
#include "jsonrpc_types.h"
#include <string>
#include <functional>
#include <atomic>
#include <memory>

#include "jsonrpc_dispatcher.h"


namespace mcp {
    namespace jsonrpc {
        class HttpJsonRpcServer {
        public:
            explicit HttpJsonRpcServer(JsonRpcDispatcher dispatcher);
            HttpJsonRpcServer(JsonRpcDispatcher dispatcher, const std::string &host, int port);
            ~HttpJsonRpcServer();

            HttpJsonRpcServer(const HttpJsonRpcServer &) = delete;
            HttpJsonRpcServer &operator=(const HttpJsonRpcServer &) = delete;

            void Run();

            void Stop();

            bool IsRunning() const;

            const std::string &GetHost() const;
            int GetPort() const;
            std::string HandleRequest(const std::string &request_body);

            // SseCallback 是一个接收 SseSend 的回调
            using SseSend = std::function<void(const std::string&)>;
            using SseCallback = std::function<void(const SseSend&)>;
            // using SseCallback = std::function<void(const std::function<void(const std::string&)>&)>;
            //注册一个 SSE 接口。当客户端访问 path 这个路径时，就执行 callback。
            void RegisterSseEndpoint(const std::string &path, SseCallback callback);
        private:
            JsonRpcDispatcher m_dispatcher_;
            std::string m_host_;
            int m_port_;
            std::atomic<bool> m_running_{false};

            //使用pimpl模式隐藏httplib实现细节
            class Pimpl;
            std::unique_ptr<Pimpl> m_pimpl_;
        };
    }
}



#endif //HTTP_JSONRPC_H
