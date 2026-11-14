#pragma once
class MediaRouter;
namespace avserver::network {
// Owns Asio listener and gRPC-bridge work pool for the unified process.
void RunHttpServer(MediaRouter &router);
} // namespace avserver::network
