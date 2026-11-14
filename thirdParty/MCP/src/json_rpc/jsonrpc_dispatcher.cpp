/**
 * @file JsonRpcDispatcher.cpp
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#include "jsonrpc_dispatcher.h"

#include "logger/logger.h"

namespace mcp {
    namespace jsonrpc {
        namespace {
            std::future<json> MakeReadyFuture(json value) {
                std::promise<json> promise;
                promise.set_value(std::move(value));
                return promise.get_future();
            }

            std::future<json> MakeExceptionalFuture(std::exception_ptr exception) {
                std::promise<json> promise;
                promise.set_exception(exception);
                return promise.get_future();
            }
        }

        JsonRpcDispatcher::JsonRpcDispatcher(size_t thread_count, size_t max_queue_size) {
            EnableThreadPool(thread_count, max_queue_size);
        }

        JsonRpcDispatcher::JsonRpcDispatcher(size_t thread_count, size_t max_queue_size,
                                             std::vector<std::string> pooled_methods) {
            EnableThreadPool(thread_count, max_queue_size, std::move(pooled_methods));
        }

        void JsonRpcDispatcher::RegisterHandler(const std::string &method, handler handler) {
            m_handlers_[method] = std::move(handler);
        }

        bool JsonRpcDispatcher::HasHandler(const std::string &method) const {
            return m_handlers_.find(method) != m_handlers_.end();
        }

        json JsonRpcDispatcher::Call(const std::string &method, const json &params) const {
            return CallAsync(method, params).get();
        }

        std::future<json> JsonRpcDispatcher::CallAsync(const std::string &method, const json &params) const {
            auto it = m_handlers_.find(method);
            if (it == m_handlers_.end()) {
                return MakeExceptionalFuture(std::make_exception_ptr(
                        std::runtime_error("Unknown method: " + method)));
            }

            const auto& registered_handler = it->second;
            if (m_thread_pool_ && ShouldRunInThreadPool(method)) {
                MCP_LOG_INFO("Dispatching method '{}' in thread pool", method);
                return m_thread_pool_->Submit([registered_handler, params] {
                    return registered_handler(params);
                });
            }

            try {
                return MakeReadyFuture(registered_handler(params));
            } catch (...) {
                return MakeExceptionalFuture(std::current_exception());
            }
        }

        void JsonRpcDispatcher::EnableThreadPool(size_t thread_count, size_t max_queue_size) {
            m_thread_pool_ = std::make_shared<ThreadPool>(thread_count, max_queue_size);
        }

        void JsonRpcDispatcher::EnableThreadPool(size_t thread_count, size_t max_queue_size,
                                                 std::vector<std::string> pooled_methods) {
            SetPooledMethods(std::move(pooled_methods));
            EnableThreadPool(thread_count, max_queue_size);
        }

        void JsonRpcDispatcher::SetPooledMethods(std::vector<std::string> pooled_methods) {
            m_pooled_methods_.clear();
            for (auto& method : pooled_methods) {
                if (!method.empty()) {
                    m_pooled_methods_.insert(std::move(method));
                }
            }
        }

        size_t JsonRpcDispatcher::ThreadPoolWorkerCount() const {
            return m_thread_pool_ ? m_thread_pool_->WorkerCount() : 0;
        }

        size_t JsonRpcDispatcher::ThreadPoolPendingTasks() const {
            return m_thread_pool_ ? m_thread_pool_->PendingTasks() : 0;
        }

        size_t JsonRpcDispatcher::ThreadPoolMaxQueueSize() const {
            return m_thread_pool_ ? m_thread_pool_->MaxQueueSize() : 0;
        }

        bool JsonRpcDispatcher::ShouldRunInThreadPool(const std::string &method) const {
            return m_pooled_methods_.find(method) != m_pooled_methods_.end();
        }
    }
}
