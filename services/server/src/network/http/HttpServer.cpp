#include "HttpServer.h"
#include "HttpSession.h"
#include "config/Environment.h"
#include "rpc/UploadService.h"
#include <iostream>
namespace avserver::network {
void RunHttpServer(MediaRouter &router, UploadService &uploads) {
  net::io_context io;
  net::thread_pool workers(8);
  auto host = Environment("AVPLAYER_WEB_GATEWAY_HOST", "127.0.0.1");
  auto port = static_cast<unsigned short>(
      std::stoi(Environment("AVPLAYER_WEB_GATEWAY_PORT", "50065")));
  tcp::acceptor acceptor(io, {net::ip::make_address(host), port});
  std::function<void()> accept;
  accept = [&] {
    acceptor.async_accept(net::make_strand(io),
                          [&](beast::error_code ec, tcp::socket socket) {
                            if (!ec)
                              StartHttp(std::move(socket), workers, router,
                                        uploads);
                            accept();
                          });
  };
  accept();
  std::cout << "avplayer_server HTTP listening on " << host << ":" << port
            << std::endl;
  io.run();
}
} // namespace avserver::network
