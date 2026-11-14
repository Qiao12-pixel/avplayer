#pragma once
#include <grpcpp/grpcpp.h>

#include "Storage.h"
#include "agent.grpc.pb.h"
#include <filesystem>
#include <fstream>
#include <random>
class UploadService final : public avplayer::agent::v1::MediaService::Service {
  Storage &storage;

public:
  explicit UploadService(Storage &value) : storage(value) {}
  grpc::Status
  UploadMedia(grpc::ServerContext *serverContext,
              grpc::ServerReader<avplayer::agent::v1::MediaChunk> *reader,
              avplayer::agent::v1::MediaHandle *handle) override {
    namespace fs = std::filesystem;
    namespace api = avplayer::agent::v1;
    api::MediaChunk part;
    if (!reader->Read(&part) || part.session_id().empty())
      return {grpc::StatusCode::INVALID_ARGUMENT, "session_id required"};
    const auto session = part.session_id();
    const auto originalName = part.file_name();
    if (session.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQR"
                                  "STUVWXYZ0123456789-_") != std::string::npos)
      return {grpc::StatusCode::INVALID_ARGUMENT, "invalid session"};
    std::random_device random;
    const char *hex = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 32; ++i)
      id += hex[random() % 16];
    auto root = getenv("AVPLAYER_STORAGE_ROOT");
    fs::path dir = fs::path(root ? root : "storage") / session / id;
    try {
      fs::create_directories(dir);
      auto suffix = fs::path(part.file_name()).extension().string();
      if (suffix.empty())
        suffix = ".bin";
      std::ofstream file(dir / ("original" + suffix), std::ios::binary);
      if (!file)
        throw std::runtime_error("cannot create upload");
      int64_t size = 0;
      do {
        file.write(part.data().data(), part.data().size());
        size += part.data().size();
        if (!file)
          throw std::runtime_error("upload write failed");
      } while (reader->Read(&part));
      if (serverContext->IsCancelled())
        throw std::runtime_error("upload cancelled");
      file.close();
      auto agentAddress = getenv("AVPLAYER_AGENT_SERVER");
      auto stub = api::AgentService::NewStub(
          grpc::CreateChannel(agentAddress ? agentAddress : "localhost:50061",
                              grpc::InsecureChannelCredentials()));
      grpc::ClientContext context;
      context.set_deadline(std::chrono::system_clock::now() +
                           std::chrono::seconds(5));
      api::MediaRegistration registration;
      registration.set_session_id(session);
      registration.set_media_id(id);
      api::ControlAck ack;
      auto status = stub->RegisterMedia(&context, registration, &ack);
      if (!status.ok() || !ack.success())
        throw std::runtime_error("Agent media registration failed");
      api::Struct request, response;
      request.set_json(nlohmann::json{
          {"action", "record_media"},
          {"session_id", session},
          {"media_id", id},
          {"file_name", originalName},
          {"size_bytes",
           size}}.dump());
      storage.Execute(nullptr, &request, &response);
      handle->set_media_id(id);
      handle->set_size_bytes(size);
      return grpc::Status::OK;
    } catch (const std::exception &e) {
      std::error_code ignored;
      fs::remove_all(dir, ignored);
      return {grpc::StatusCode::INTERNAL, e.what()};
    }
  }
};
