#include "MediaRouter.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <thread>
namespace api = avplayer::agent::v1;
std::string MediaRouter::key(const std::string &s, const std::string &m) const {
  return std::to_string(s.size()) + ":" + s + m;
}
grpc::Status MediaRouter::Signal(
    grpc::ServerContext *server,
    grpc::ServerReaderWriter<api::SignalMessage, api::SignalMessage> *stream) {
  api::SignalMessage first;
  if (!stream->Read(&first) || first.kind() != "offer" ||
      first.session_id().empty() || first.media_id().empty())
    return {grpc::StatusCode::INVALID_ARGUMENT,
            "first message must be offer with session/media IDs"};
  auto endpoints = registry.endpoints();
  if (!registry.enabled()) {
    auto value = getenv("AVPLAYER_MEDIA_SERVER");
    endpoints = {value ? value : "localhost:50062"};
  }
  if (endpoints.empty())
    return {grpc::StatusCode::UNAVAILABLE,
            "no discovered media backends (ZooKeeper disconnected or registry "
            "empty)"};
  std::string routeKey = key(first.session_id(), first.media_id()), endpoint;
  uint64_t token;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (routes.count(routeKey))
      return {grpc::StatusCode::ALREADY_EXISTS,
              "playback session already active"};
    size_t minimum = routes.size() + 1;
    std::vector<std::string> candidates;
    for (auto &e : endpoints) {
      size_t count = 0;
      for (auto &r : routes)
        if (r.second.endpoint == e)
          ++count;
      if (count < minimum) {
        minimum = count;
        candidates.clear();
      }
      if (count == minimum)
        candidates.push_back(e);
    }
    endpoint = candidates[tie++ % candidates.size()];
    token = ++sequence;
    routes.emplace(routeKey, Route{endpoint, token});
  }
  server->AddInitialMetadata("x-avplayer-media-backend", endpoint);
  std::cout << "[router] assign " << first.session_id() << "/"
            << first.media_id() << " -> " << endpoint << std::endl;
  struct Cleanup {
    MediaRouter *router;
    std::string key;
    uint64_t token;
    ~Cleanup() {
      std::lock_guard<std::mutex> lock(router->mutex);
      auto it = router->routes.find(key);
      if (it != router->routes.end() && it->second.token == token)
        router->routes.erase(it);
    }
  } cleanup{this, routeKey, token};
  auto stub = api::StreamingService::NewStub(
      grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials()));
  grpc::ClientContext context;
  context.set_wait_for_ready(false);
  auto upstream = stub->Signal(&context);
  std::atomic<bool> done{false};
  std::thread cancellation([&] {
    while (!done) {
      if (server->IsCancelled()) {
        context.TryCancel();
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });
  grpc::Status status;
  if (!upstream->Write(first)) {
    context.TryCancel();
    status = upstream->Finish();
  } else {
    std::thread reader([&] {
      api::SignalMessage message;
      while (upstream->Read(&message)) {
        if (!stream->Write(message))
          break;
      }
      context.TryCancel();
      server->TryCancel();
    });
    api::SignalMessage message;
    while (stream->Read(&message)) {
      if (message.kind() != "ice" ||
          message.session_id() != first.session_id() ||
          message.media_id() != first.media_id())
        break;
      if (!upstream->Write(message))
        break;
    }
    context.TryCancel();
    upstream->WritesDone();
    reader.join();
    status = upstream->Finish();
  }
  done = true;
  cancellation.join();
  return status;
}
grpc::Status MediaRouter::Control(grpc::ServerContext *server,
                                  const api::ControlRequest *request,
                                  api::ControlAck *response) {
  std::string endpoint;
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = routes.find(key(request->session_id(), request->media_id()));
    if (it == routes.end()) {
      response->set_success(false);
      response->set_error("session not found; negotiate via gateway first");
      return grpc::Status::OK;
    }
    endpoint = it->second.endpoint;
  }
  server->AddInitialMetadata("x-avplayer-media-backend", endpoint);
  grpc::ClientContext context;
  context.set_deadline(
      std::min(server->deadline(),
               std::chrono::system_clock::now() + std::chrono::seconds(5)));
  auto stub = api::StreamingService::NewStub(
      grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials()));
  return stub->Control(&context, *request, response);
}
nlohmann::json MediaRouter::snapshot() {
  auto result = nlohmann::json{{"discovery_enabled", registry.enabled()},
                               {"discovery_connected", registry.ready()},
                               {"backends", registry.endpoints()},
                               {"sessions", nlohmann::json::array()}};
  std::lock_guard<std::mutex> lock(mutex);
  std::map<std::string, size_t> counts;
  for (auto &r : routes)
    ++counts[r.second.endpoint];
  for (auto &entry : counts)
    result["sessions"].push_back(
        {{"endpoint", entry.first}, {"active", entry.second}});
  return result;
}
