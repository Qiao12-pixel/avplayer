#pragma once
#include "network/common/NetworkSupport.h"
class MediaRouter;
namespace avserver::network {
void StartHttp(tcp::socket socket, net::thread_pool &workers,
               MediaRouter &router);
}
