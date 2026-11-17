#pragma once
#include "StorageService.h"
#include "events/PlaybackEvents.h"
#include <grpcpp/grpcpp.h>
class UploadService final : public avplayer::agent::v1::MediaService::Service {
  StorageService &storage;
  avserver::PlaybackEvents *events;

public:
  explicit UploadService(StorageService &value,
                         avserver::PlaybackEvents *sink = nullptr)
      : storage(value), events(sink) {}
  grpc::Status
  UploadMedia(grpc::ServerContext *serverContext,
              grpc::ServerReader<avplayer::agent::v1::MediaChunk> *reader,
              avplayer::agent::v1::MediaHandle *handle) override;
};
