#pragma once
#include "StorageService.h"
#include "events/PlaybackEvents.h"
#include <grpcpp/grpcpp.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

class UploadTransaction {
  StorageService &storage;
  avserver::PlaybackEvents *events;
  std::string session;
  std::string originalName;
  std::string id;
  std::filesystem::path directory;
  std::ofstream file;
  int64_t size = 0;
  bool completed = false;

  UploadTransaction(StorageService &storage, avserver::PlaybackEvents *events,
                    std::string session, std::string fileName);
  friend class UploadService;

public:
  ~UploadTransaction();
  UploadTransaction(const UploadTransaction &) = delete;
  UploadTransaction &operator=(const UploadTransaction &) = delete;

  void Write(const std::string &bytes);
  avplayer::agent::v1::MediaHandle Finish();
};

class UploadService final : public avplayer::agent::v1::MediaService::Service {
  StorageService &storage;
  avserver::PlaybackEvents *events;

public:
  explicit UploadService(StorageService &value,
                         avserver::PlaybackEvents *sink = nullptr)
      : storage(value), events(sink) {}
  std::unique_ptr<UploadTransaction>
  BeginUpload(const std::string &session, const std::string &fileName);
  grpc::Status
  UploadMedia(grpc::ServerContext *serverContext,
              grpc::ServerReader<avplayer::agent::v1::MediaChunk> *reader,
              avplayer::agent::v1::MediaHandle *handle) override;
};
