#pragma once
#include "network/common/NetworkSupport.h"
namespace avserver::network {
void StartWebSocket(beast::tcp_stream stream, bool agentMode,
                    net::thread_pool &workers,
                    http::request<http::empty_body> request);
}
