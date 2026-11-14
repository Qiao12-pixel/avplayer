/**
 * @file jsonrpc_serialization.h
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#ifndef JSONRPC_SERIALIZATION_H
#define JSONRPC_SERIALIZATION_H
#include "jsonrpc_types.h"

namespace mcp {
    namespace jsonrpc {
        void to_json(json& j, const JsonRpcRequest& req);//对象 → JSON（序列化)
        void from_json(const json& j, JsonRpcRequest& req);//Json -> 对象[反序列化]

        void to_json(json& j, const JsonRpcResponse& res);
        void from_json(const json& j, JsonRpcResponse& res);

        void to_json(json& j, const JsonRpcError& err);
        void from_json(const json& j, JsonRpcError& err);

        JsonRpcResponse MakeErrorResponse(const json& id,
                                          int code,
                                          const std::string& message,
                                          std::optional<json> data = std::nullopt);
        json SerializeResponse(const JsonRpcResponse& res);
        std::string DumpResponse(const JsonRpcResponse& res);
    }
}



#endif //JSONRPC_SERIALIZATION_H
