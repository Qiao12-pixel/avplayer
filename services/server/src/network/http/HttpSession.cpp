#include "HttpSession.h"
#include "events/PlaybackEvents.h"
#include "integrations/mcp/WeatherService.h"
#include "network/websocket/WebSocketSession.h"
#include "rpc/MediaRouter.h"
#include "rpc/UploadService.h"
namespace avserver::network {
class HttpSession : public std::enable_shared_from_this<HttpSession> {
  beast::tcp_stream stream;
  beast::flat_buffer buffer;
  http::request_parser<http::buffer_body> parser;
  net::thread_pool &workers;
  MediaRouter &router;
  std::array<char, 65536> chunk{};
  std::string body;
  api::MediaHandle handle;
  UploadService &uploads;
  std::unique_ptr<UploadTransaction> upload;
  void reply(unsigned code, json data) {
    auto r = std::make_shared<http::response<http::string_body>>(
        http::status(code), 11);
    r->set(http::field::content_type, "application/json");
    r->keep_alive(false);
    r->body() = data.dump();
    r->prepare_payload();
    auto self = shared_from_this();
    http::async_write(stream, *r, [self, r](beast::error_code, size_t) {
      beast::error_code ec;
      self->stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    });
  }
  void downloadFile(const std::string &target) {
    std::map<std::string, std::string> args;
    auto q = target.find('?');
    if (q != std::string::npos) {
      std::stringstream ss(target.substr(q + 1));
      std::string part;
      while (std::getline(ss, part, '&')) {
        auto eq = part.find('=');
        if (eq != std::string::npos)
          args[DecodeUrl(part.substr(0, eq))] = DecodeUrl(part.substr(eq + 1));
      }
    }
    namespace fs = std::filesystem;
    try {
      auto root = fs::weakly_canonical(env("AVPLAYER_STORAGE_ROOT", "storage"));
      if (args["session_id"].empty() || args["media_id"].empty() ||
          args["file"].empty())
        throw std::runtime_error("missing download parameters");
      auto path = fs::weakly_canonical(root / args["session_id"] /
                                       args["media_id"] / args["file"]);
      auto rel = path.lexically_relative(root);
      if (rel.empty() || *rel.begin() == ".." || !fs::is_regular_file(path))
        throw std::runtime_error("file not found");
      auto response = std::make_shared<http::response<http::file_body>>(
          http::status::ok, 11);
      beast::error_code ec;
      response->body().open(path.string().c_str(), beast::file_mode::scan, ec);
      if (ec)
        throw std::runtime_error(ec.message());
      response->set(http::field::content_type, "application/octet-stream");
      response->keep_alive(false);
      response->content_length(response->body().size());
      auto self = shared_from_this();
      http::async_write(stream, *response,
                        [self, response](beast::error_code, size_t) {
                          beast::error_code ignored;
                          self->stream.socket().close(ignored);
                        });
    } catch (const std::exception &e) {
      reply(404, {{"error", e.what()}});
    }
  }
  void finish() {
    auto self = shared_from_this();
    net::post(workers, [self] {
      try {
        if (self->upload) {
          self->handle = self->upload->Finish();
          self->upload.reset();
          net::post(self->stream.get_executor(), [self] {
            self->reply(200, {{"media_id", self->handle.media_id()},
                              {"size_bytes", self->handle.size_bytes()}});
          });
          return;
        }
        auto target = std::string(self->parser.get().target());
        if (target == "/control") {
          auto j = json::parse(self->body);
          api::ControlRequest req;
          req.set_session_id(j.at("session_id"));
          req.set_media_id(j.at("media_id"));
          req.set_action(j.at("action"));
          req.set_position(j.value("position", 0.f));
          api::ControlAck ack;
          grpc::ClientContext ctx;
          ctx.set_deadline(std::chrono::system_clock::now() +
                           std::chrono::seconds(10));
          auto stub = api::StreamingService::NewStub(
              channel("AVPLAYER_DATA_SERVER", "localhost:50066"));
          auto s = stub->Control(&ctx, req, &ack);
          json result = {{"success", ack.success()},
                         {"error", s.ok() ? ack.error() : s.error_message()},
                         {"duration_ms", ack.duration_ms()},
                         {"position_ms", ack.position_ms()},
                         {"playing", ack.playing()},
                         {"recording", ack.recording()},
                         {"output_path", ack.output_path()}};
          net::post(self->stream.get_executor(),
                    [self, result, s, ok = ack.success()] {
                      self->reply(s.ok() ? (ok ? 200 : 400) : 502, result);
                    });
        } else
          net::post(self->stream.get_executor(),
                    [self] { self->reply(404, {{"error", "not found"}}); });
      } catch (const std::exception &e) {
        std::string msg = e.what();
        net::post(self->stream.get_executor(),
                  [self, msg] { self->reply(400, {{"error", msg}}); });
      }
    });
  }
  void readBody() {
    parser.get().body().data = chunk.data();
    parser.get().body().size = chunk.size();
    auto self = shared_from_this();
    http::async_read_some(
        stream, buffer, parser, [self](beast::error_code ec, size_t) {
          if (ec == http::error::need_buffer)
            ec = {};
          if (ec) {
            self->upload.reset();
            return;
          }
          auto size = self->chunk.size() - self->parser.get().body().size;
          if (self->upload) {
            std::string bytes(self->chunk.data(), size);
            net::post(self->workers, [self, bytes] {
              try {
                self->upload->Write(bytes);
                net::post(self->stream.get_executor(), [self] {
                  if (self->parser.is_done())
                    self->finish();
                  else
                    self->readBody();
                });
              } catch (const std::exception &error) {
                auto message = std::string(error.what());
                self->upload.reset();
                net::post(self->stream.get_executor(), [self, message] {
                  self->reply(500, {{"error", message}});
                });
              }
            });
          } else {
            self->body.append(self->chunk.data(), size);
            if (self->body.size() > 1024 * 1024) {
              self->reply(413, {{"error", "request too large"}});
              return;
            }
            if (self->parser.is_done())
              self->finish();
            else
              self->readBody();
          }
        });
  }

public:
  HttpSession(tcp::socket s, net::thread_pool &pool, MediaRouter &router,
              UploadService &uploadService)
      : stream(std::move(s)), workers(pool), router(router),
        uploads(uploadService) {
    parser.body_limit(8ULL * 1024 * 1024 * 1024);
  }
  void run() {
    stream.expires_after(std::chrono::seconds(60));
    auto self = shared_from_this();
    http::async_read_header(
        stream, buffer, parser, [self](beast::error_code ec, size_t) {
          if (ec)
            return;
          auto &req = self->parser.get();
          std::string target(req.target());
          if (ws::is_upgrade(req) &&
              (target == "/signal" || target == "/agent")) {
            http::request<http::empty_body> upgrade;
            upgrade.method(req.method());
            upgrade.target(req.target());
            upgrade.version(req.version());
            upgrade.base() = req.base();
            self->stream.expires_never();
            StartWebSocket(std::move(self->stream), target == "/agent",
                           self->workers, std::move(upgrade));
            return;
          }
          if (req.method() == http::verb::get &&
              target.rfind("/download?", 0) == 0) {
            self->downloadFile(target);
            return;
          }
          if (req.method() == http::verb::get &&
              target.rfind("/location?", 0) == 0) {
            net::post(self->workers, [self, target] {
              json result;
              try {
                std::map<std::string, std::string> values;
                std::stringstream query(target.substr(10));
                std::string part;
                while (std::getline(query, part, '&')) {
                  auto pos = part.find('=');
                  if (pos != std::string::npos)
                    values[part.substr(0, pos)] =
                        DecodeUrl(part.substr(pos + 1));
                }
                size_t latEnd = 0, lonEnd = 0;
                auto lat = std::stod(values.at("lat"), &latEnd),
                     lon = std::stod(values.at("lon"), &lonEnd);
                if (latEnd != values.at("lat").size() ||
                    lonEnd != values.at("lon").size())
                  throw std::invalid_argument("invalid coordinates");
                result = avserver::QueryWeatherAt(lat, lon);
              } catch (const std::exception &) {
                result = {{"success", false}, {"error", "无效定位坐标"}};
              }
              net::post(self->stream.get_executor(), [self, result] {
                self->reply(result.value("success", false) ? 200 : 502, result);
              });
            });
            return;
          }
          if (req.method() == http::verb::get &&
              target.rfind("/weather?city=", 0) == 0) {
            auto city =
                DecodeUrl(target.substr(std::string("/weather?city=").size()));
            net::post(self->workers, [self, city] {
              auto result = avserver::QueryWeather(city);
              net::post(self->stream.get_executor(), [self, result] {
                self->reply(result.value("success", false) ? 200 : 502, result);
              });
            });
            return;
          }
          if (req.method() == http::verb::get && target == "/playback-stats") {
            net::post(self->workers, [self] {
              auto result = avserver::PlaybackStatistics();
              net::post(self->stream.get_executor(), [self, result] {
                self->reply(result.value("success", false) ? 200 : 503, result);
              });
            });
            return;
          }
          if (req.method() == http::verb::get && target == "/health") {
            self->reply(
                200, {{"status", "ok"}, {"routing", self->router.snapshot()}});
            return;
          }
          if (req.method() == http::verb::get && target == "/config") {
            json ice = json::array();
            std::stringstream ss(
                env("AVPLAYER_ICE_SERVERS", "stun:stun.l.google.com:19302"));
            std::string url;
            while (std::getline(ss, url, ','))
              if (!url.empty())
                ice.push_back(json{{"urls", url}});
            self->reply(200,
                        {{"ice_servers", ice},
                         {"provider", env("AVPLAYER_AI_PROVIDER", "local")}});
            return;
          }
          if (target == "/upload" && req.method() == http::verb::post) {
            std::string session(req["X-Session-Id"]);
            if (session.empty()) {
              self->reply(400, {{"error", "X-Session-Id required"}});
              return;
            }
            std::string name = DecodeUrl(std::string(req["X-File-Name"]));
            net::post(self->workers, [self, session, name] {
              try {
                self->upload = self->uploads.BeginUpload(session, name);
                net::post(self->stream.get_executor(), [self] {
                  self->readBody();
                });
              } catch (const std::exception &error) {
                auto message = std::string(error.what());
                net::post(self->stream.get_executor(), [self, message] {
                  self->reply(400, {{"error", message}});
                });
              }
            });
            return;
          }
          self->readBody();
        });
  }
};

void StartHttp(tcp::socket socket, net::thread_pool &workers,
               MediaRouter &router, UploadService &uploads) {
  std::make_shared<HttpSession>(std::move(socket), workers, router, uploads)
      ->run();
}
} // namespace avserver::network
