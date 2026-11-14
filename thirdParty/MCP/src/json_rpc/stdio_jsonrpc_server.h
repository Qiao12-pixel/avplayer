/**
 * @file Stdio_jsonrpc_server.h
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#ifndef STDIO_JSONRPC_SERVER_H
#define STDIO_JSONRPC_SERVER_H

#include "jsonrpc_types.h"
#include "jsonrpc_dispatcher.h"
namespace mcp {
    namespace jsonrpc {
        // stdio + Content-Length 封包的 JSON-RPC 服务器

        /**
        * Content-Length: 123\r\n\r\n
          { JSON 内容 }
         */
        class StdioJsonRpcServer {
        public:
            explicit StdioJsonRpcServer(JsonRpcDispatcher dispatcher);
            StdioJsonRpcServer(JsonRpcDispatcher dispatcher, std::istream& in, std::ostream& out);
            // 阻塞运行：循环读取请求 → 处理 → 写响应
            /**
            * 读一条完整消息 readMessage()
            * 解析成 JsonRpcRequest
            * 交给 handleRequest() 处理
            * 把 JsonRpcResponse 发回客户端
             */
            void Run();
        private:
            JsonRpcDispatcher m_dispatcher_;
            std::istream& m_in_ = std::cin;
            std::ostream& m_out_ = std::cout;
            // 从输入流读取一条完整的 JSON-RPC 消息体（依据 Content-Length）
            bool ReadMessage(std::string& out_body);
            // 将 JSON 消息写到输出流，并附带 Content-Length 头
            void WriteMessage(const json& msg);

            JsonRpcResponse HandleRequest(const JsonRpcRequest& req);

        };
    }
}



#endif //STDIO_JSONRPC_SERVER_H
