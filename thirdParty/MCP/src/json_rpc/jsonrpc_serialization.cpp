/**
 * @file jsonrpc_serialization.cpp
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#include "jsonrpc_serialization.h"

#include <utility>

namespace mcp {
    namespace jsonrpc {

        void to_json(json &j, const JsonRpcRequest &req) {
            j = json{
                {"jsonrpc", "2.0"},
                {"method", req.method}
            };
            if (req.id.has_value()) {
                j["id"] = req.id.value();
            }
            if (req.params.has_value()) {
                j["params"] = req.params.value();
            }
        }
        void from_json(const json &j, JsonRpcRequest &req) {
            req.jsonrpc = j.at("jsonrpc").get<std::string>();
            req.method = j.at("method").get<std::string>();
            if (j.contains("id")) {
                req.id = j.at("id");
            } else {
                req.id.reset();
            }
            if (j.contains("params")) {
                req.params = j.at("params");
            } else {
                req.params.reset();
            }
        }


        void to_json(json& j, const JsonRpcResponse& res) {
            j = json{
                {"jsonrpc", "2.0"},
                {"id", res.id}
            };
            if (res.result.has_value()) {
                j["result"] = res.result.value();
            } else if (res.error.has_value()) {
                j["error"] = res.error.value();
            } else {
                j["error"] = JsonRpcError{-32603, "Internal error"};
            }
        }
        void from_json(const json &j, JsonRpcResponse &res) {
            res.jsonrpc = j.at("jsonrpc").get<std::string>();
            res.id = j.at("id");

            if (j.contains("result")) {
                res.result = j.at("result");
                res.error.reset();
            } else if (j.contains("error")) {
                res.error = j.at("error");
                res.result.reset();
            } else {
                res.error.reset();
                res.result.reset();
            }
        }


        void to_json(json &j, const JsonRpcError &err) {
            j = json {
                    {"code", err.code},
                    {"message", err.message}
            };
            if (err.data.has_value()) {
                j["data"] = err.data.value();
            }
        }
        void from_json(const json &j, JsonRpcError &err) {
            err.code = j.at("code").get<int>();
            err.message = j.at("message").get<std::string>();
            if (j.contains("data")) {
                err.data = j.at("data");
            } else {
                err.data.reset();
            }
        }

        JsonRpcResponse MakeErrorResponse(const json& id,
                                          int code,
                                          const std::string& message,
                                          std::optional<json> data) {
            JsonRpcResponse res;
            res.jsonrpc = "2.0";
            res.id = id;
            res.error = JsonRpcError{
                .code = code,
                .message = message,
                .data = std::move(data),
            };
            return res;
        }

        json SerializeResponse(const JsonRpcResponse& res) {
            return json(res);
        }

        std::string DumpResponse(const JsonRpcResponse& res) {
            return SerializeResponse(res).dump();
        }
    }

}
