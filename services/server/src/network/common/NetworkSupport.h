#pragma once
#include "agent.grpc.pb.h"
#include "config/Environment.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <deque>
#include <filesystem>
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
namespace avserver::network {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ws = beast::websocket;
namespace api = avplayer::agent::v1;
using tcp = net::ip::tcp;
using json = nlohmann::json;
inline std::string env(const char *key, const char *fallback) {
  return Environment(key, fallback);
}
inline auto channel(const char *key, const char *fallback) {
  return grpc::CreateChannel(env(key, fallback),
                             grpc::InsecureChannelCredentials());
}
std::string DecodeUrl(std::string text);
} // namespace avserver::network
