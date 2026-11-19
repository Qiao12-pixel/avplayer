#include "ServerApplication.h"
#include "config/Environment.h"
#include "discovery/zookeeper/Registry.h"
#include "events/PlaybackEvents.h"
#include "network/http/HttpServer.h"
#include "rpc/MediaRouter.h"
#include "rpc/StorageService.h"
#include "rpc/UploadService.h"
#include <iostream>
int avserver::ServerApplication::Run() {
  try {
    PlaybackEvents events;
    StorageService storage(&events);
    av::StreamingServiceImpl media(
        Environment("AVPLAYER_STORAGE_ROOT", "storage"),
        [&events](const std::string &type, const std::string &session,
                  const std::string &media, const std::string &playback,
                  nlohmann::json details) {
          events.Record(type, session, media, playback, std::move(details));
        });
    MediaRouter router(media);
    // Private media endpoint is hosted in this same process for cluster
    // routing.
    grpc::ServerBuilder mediaBuilder;
    mediaBuilder.AddListeningPort(
        Environment("AVPLAYER_MEDIA_SERVER_ADDRESS", "127.0.0.1:50062"),
        grpc::InsecureServerCredentials());
    mediaBuilder.RegisterService(&media);
    auto mediaServer = mediaBuilder.BuildAndStart();
    if (!mediaServer)
      throw std::runtime_error("cannot start embedded media endpoint");
    auto zk = Environment("AVPLAYER_ZK_HOSTS", "");
    auto advertise = Environment("AVPLAYER_MEDIA_ADVERTISE", "");
    if (!zk.empty() && advertise.empty())
      throw std::runtime_error(
          "AVPLAYER_MEDIA_ADVERTISE required with ZooKeeper");
    av::Registry registration(
        Environment("AVPLAYER_SERVER_REGISTER", "1") == "0" ? "" : advertise);
    UploadService upload(storage, &events);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(
        Environment("AVPLAYER_DATA_SERVER_ADDRESS", "127.0.0.1:50066"),
        grpc::InsecureServerCredentials());
    builder.RegisterService(&storage);
    builder.RegisterService(&upload);
    builder.RegisterService(&router);
    auto grpcServer = builder.BuildAndStart();
    if (!grpcServer)
      throw std::runtime_error("cannot start data service");
    network::RunHttpServer(router, upload);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
