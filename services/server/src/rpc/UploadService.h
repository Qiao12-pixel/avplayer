#pragma once
#include "StorageService.h"
#include <grpcpp/grpcpp.h>
class UploadService final : public avplayer::agent::v1::MediaService::Service {
  StorageService &storage;

public:
  explicit UploadService(StorageService &value) : storage(value) {}
  grpc::Status
  UploadMedia(grpc::ServerContext *serverContext,
              grpc::ServerReader<avplayer::agent::v1::MediaChunk> *reader,
              avplayer::agent::v1::MediaHandle *handle) override;
};
