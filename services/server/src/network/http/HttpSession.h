#pragma once
#include "network/common/NetworkSupport.h"
class MediaRouter;
class UploadService;
namespace avserver::network {
void StartHttp(tcp::socket socket, net::thread_pool &workers,
               MediaRouter &router, UploadService &uploads);
}
