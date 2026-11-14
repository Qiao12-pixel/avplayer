/**
 * @file JsonRpcDispatcher.h
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#ifndef JSONRPCDISPATCHER_H
#define JSONRPCDISPATCHER_H
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "jsonrpc_types.h"
#include "utils/thread_pool.h"


namespace mcp {
    namespace jsonrpc {
        class JsonRpcDispatcher {
        public:
            /**
             * @brief 接受参数，返回json结果
             */
            using handler = std::function<json(const json& params)>;

            JsonRpcDispatcher() = default;
            JsonRpcDispatcher(size_t thread_count, size_t max_queue_size);
            JsonRpcDispatcher(size_t thread_count, size_t max_queue_size, std::vector<std::string> pooled_methods);

            void RegisterHandler(const std::string& method, handler handler);

            bool HasHandler(const std::string& method) const;

            json Call(const std::string& method, const json& params) const;
            std::future<json> CallAsync(const std::string& method, const json& params) const;

            void EnableThreadPool(size_t thread_count, size_t max_queue_size);
            void EnableThreadPool(size_t thread_count, size_t max_queue_size, std::vector<std::string> pooled_methods);
            void SetPooledMethods(std::vector<std::string> pooled_methods);
            size_t ThreadPoolWorkerCount() const;
            size_t ThreadPoolPendingTasks() const;
            size_t ThreadPoolMaxQueueSize() const;
        private:
            bool ShouldRunInThreadPool(const std::string& method) const;

            /// 方法处理器映射，用来查询已经注册的方法
            std::unordered_map<std::string, handler> m_handlers_;
            std::shared_ptr<ThreadPool> m_thread_pool_;
            std::unordered_set<std::string> m_pooled_methods_ = {
                "tools/call",
                "resources/read",
                "prompts/get"
            };
        };
    }
}



#endif //JSONRPCDISPATCHER_H
