#pragma once
class MediaRouter;
class UploadService;
namespace avserver::network {
// Owns the HTTP listener and worker pool for the unified process.
void RunHttpServer(MediaRouter &router, UploadService &uploads);
} // namespace avserver::network
