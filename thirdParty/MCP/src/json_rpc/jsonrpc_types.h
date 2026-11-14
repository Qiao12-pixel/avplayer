/**
 * @file jsonrpc.h
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#ifndef JSONRPC_H
#define JSONRPC_H

#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <iostream>

namespace mcp {
    namespace jsonrpc {
        using json = nlohmann::json;

        /***********************************
         * Json-RPC 2.0基础类型
         */
        struct JsonRpcError {
            int code;
            std::string message;
            std::optional<json> data;
        };
        struct JsonRpcRequest {
            std::string jsonrpc = "2.0";
            std::string method;
            std::optional<json> params;
            std::optional<json> id;//兼容数字，字符串，null,同时设置optional表明可能没有id
        };
        struct JsonRpcResponse {
            std::string jsonrpc = "2.0";
            json id;
            std::optional<json> result;
            std::optional<JsonRpcError> error;
        };

        namespace jsonrpc_error_code {
            constexpr int ParseError = -32700;
            constexpr int InvalidRequest = -32600;
            constexpr int MethodNotFound = -32601;
            constexpr int InvalidParams = -32602;
            constexpr int InternalError = -32603;
            // 应用自定义错误建议使用 -32000 ~ -32099
            constexpr int ServerBusy = -32000;
        }

    }
}



#endif //JSONRPC_H
