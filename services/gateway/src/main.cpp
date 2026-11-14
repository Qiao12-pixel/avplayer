#include "MediaRouter.h"
#include "Storage.h"
#include "Upload.h"
#include "agent.grpc.pb.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ws = beast::websocket;
namespace api = avplayer::agent::v1;
using tcp = net::ip::tcp;
using json = nlohmann::json;
std::string env(const char *k, const char *d) {
  auto v = std::getenv(k);
  return v ? v : d;
}
auto channel(const char *key, const char *fallback) {
  return grpc::CreateChannel(env(key, fallback),
                             grpc::InsecureChannelCredentials());
}
std::string decode(std::string s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      char *end = nullptr;
      auto h = s.substr(i + 1, 2);
      long v = strtol(h.c_str(), &end, 16);
      if (end == h.c_str() + 2) {
        out += char(v);
        i += 2;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}
class Socket : public std::enable_shared_from_this<Socket> {
  ws::stream<beast::tcp_stream> socket;
  beast::flat_buffer buffer;
  net::thread_pool &worker;
  grpc::ClientContext context;
  std::unique_ptr<api::StreamingService::Stub> media;
  std::unique_ptr<api::AgentService::Stub> agent;
  std::unique_ptr<
      grpc::ClientReaderWriter<api::SignalMessage, api::SignalMessage>>
      signal;
  std::unique_ptr<
      grpc::ClientReaderWriter<api::ClientMessage, api::ServerMessage>>
      converse;
  std::deque<std::string> writes;
  size_t queued = 0;
  bool stopped = false;
  bool agentMode;
  std::mutex rpcMutex;
  bool rpcFinished = false;
  void stop() {
    if (stopped)
      return;
    stopped = true;
    context.TryCancel();
    beast::error_code ec;
    beast::get_lowest_layer(socket).socket().close(ec);
  }
  void send(json data) {
    auto self = shared_from_this();
    net::post(socket.get_executor(), [self, text = data.dump()] {
      if (self->stopped)
        return;
      if (self->queued + text.size() > 4 * 1024 * 1024) {
        self->stop();
        return;
      }
      self->queued += text.size();
      self->writes.push_back(text);
      if (self->writes.size() == 1)
        self->write();
    });
  }
  void write() {
    auto self = shared_from_this();
    socket.async_write(net::buffer(writes.front()),
                       [self](beast::error_code ec, size_t) {
                         if (ec) {
                           self->stop();
                           return;
                         }
                         self->queued -= self->writes.front().size();
                         self->writes.pop_front();
                         if (!self->writes.empty())
                           self->write();
                       });
  }
  void read() {
    auto self = shared_from_this();
    socket.async_read(buffer, [self](beast::error_code ec, size_t) {
      if (ec) {
        self->stop();
        return;
      }
      try {
        auto data = json::parse(beast::buffers_to_string(self->buffer.data()));
        self->buffer.consume(self->buffer.size());
        net::post(self->worker, [self, data] {
          bool ok = false;
          try {
            std::lock_guard<std::mutex> lock(self->rpcMutex);
            if (self->rpcFinished)
              return;
            if (self->agentMode) {
              api::ClientMessage m;
              m.set_type(data.value("type", ""));
              m.set_session_id(data.value("session_id", "web"));
              m.set_media_id(data.value("media_id", ""));
              m.set_request_id(data.value("request_id", ""));
              m.set_tool(data.value("tool", ""));
              m.mutable_payload()->set_json(
                  data.value("payload", json::object()).dump());
              m.mutable_arguments()->set_json(
                  data.value("arguments", json::object()).dump());
              ok = self->converse->Write(m);
            } else {
              api::SignalMessage m;
              m.set_kind(data.value("kind", ""));
              m.set_session_id(data.value("session_id", ""));
              m.set_media_id(data.value("media_id", ""));
              m.set_sdp(data.value("sdp", ""));
              m.set_candidate(data.value("candidate", ""));
              m.set_mid(data.value("mid", ""));
              ok = self->signal->Write(m);
            }
          } catch (const std::exception &e) {
            self->send(
                {{"kind", "error"}, {"type", "error"}, {"error", e.what()}});
          }
          net::post(self->socket.get_executor(), [self, ok] {
            if (!ok)
              self->stop();
            else if (!self->stopped)
              self->read();
          });
        });
      } catch (const std::exception &e) {
        self->send({{"error", e.what()}});
        self->stop();
      }
    });
  }

public:
  Socket(beast::tcp_stream stream, bool mode, net::thread_pool &pool)
      : socket(std::move(stream)), worker(pool), agentMode(mode) {}
  void run(http::request<http::empty_body> req) {
    socket.read_message_max(4 * 1024 * 1024);
    socket.set_option(
        ws::stream_base::timeout::suggested(beast::role_type::server));
    auto self = shared_from_this();
    auto upgrade =
        std::make_shared<http::request<http::empty_body>>(std::move(req));
    socket.async_accept(*upgrade, [self, upgrade](beast::error_code ec) {
      if (ec)
        return;
      if (self->agentMode) {
        self->agent = api::AgentService::NewStub(
            channel("AVPLAYER_AGENT_SERVER", "localhost:50061"));
        self->converse = self->agent->Converse(&self->context);
      } else {
        self->media = api::StreamingService::NewStub(
            channel("AVPLAYER_DATA_SERVER", "localhost:50066"));
        self->signal = self->media->Signal(&self->context);
      }
      std::thread([self] {
        if (self->agentMode) {
          api::ServerMessage m;
          while (self->converse->Read(&m)) {
            self->send({{"type", m.type()},
                        {"request_id", m.request_id()},
                        {"session_id", m.session_id()},
                        {"tool", m.tool()},
                        {"payload", json::parse(m.payload().json().empty()
                                                    ? "{}"
                                                    : m.payload().json(),
                                                nullptr, false)},
                        {"arguments", json::parse(m.arguments().json().empty()
                                                      ? "{}"
                                                      : m.arguments().json(),
                                                  nullptr, false)}});
          }
        } else {
          api::SignalMessage m;
          while (self->signal->Read(&m))
            self->send({{"kind", m.kind()},
                        {"sdp", m.sdp()},
                        {"candidate", m.candidate()},
                        {"mid", m.mid()}});
        }
        self->context.TryCancel();
        std::lock_guard<std::mutex> lock(self->rpcMutex);
        self->rpcFinished = true;
        auto status =
            self->agentMode ? self->converse->Finish() : self->signal->Finish();
        net::post(self->socket.get_executor(),
                  [self, status] { self->stop(); });
      }).detach();
      self->read();
    });
  }
};
class Http : public std::enable_shared_from_this<Http> {
  beast::tcp_stream stream;
  beast::flat_buffer buffer;
  http::request_parser<http::buffer_body> parser;
  net::thread_pool &workers;
  MediaRouter &router;
  std::array<char, 65536> chunk{};
  std::string body;
  grpc::ClientContext uploadContext;
  api::MediaHandle handle;
  std::unique_ptr<api::MediaService::Stub> uploader;
  std::unique_ptr<grpc::ClientWriter<api::MediaChunk>> upload;
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
          args[decode(part.substr(0, eq))] = decode(part.substr(eq + 1));
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
          self->upload->WritesDone();
          auto s = self->upload->Finish();
          net::post(self->stream.get_executor(), [self, s] {
            if (s.ok())
              self->reply(200, {{"media_id", self->handle.media_id()},
                                {"size_bytes", self->handle.size_bytes()}});
            else
              self->reply(502, {{"error", s.error_message()}});
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
            self->uploadContext.TryCancel();
            return;
          }
          auto size = self->chunk.size() - self->parser.get().body().size;
          if (self->upload) {
            std::string bytes(self->chunk.data(), size);
            net::post(self->workers, [self, bytes] {
              api::MediaChunk c;
              c.set_data(bytes);
              bool ok = bytes.empty() || self->upload->Write(c);
              net::post(self->stream.get_executor(), [self, ok] {
                if (!ok) {
                  self->uploadContext.TryCancel();
                  self->reply(502, {{"error", "upload failed"}});
                } else if (self->parser.is_done())
                  self->finish();
                else
                  self->readBody();
              });
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
  Http(tcp::socket s, net::thread_pool &pool, MediaRouter &router)
      : stream(std::move(s)), workers(pool), router(router) {
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
            std::make_shared<Socket>(std::move(self->stream),
                                     target == "/agent", self->workers)
                ->run(std::move(upgrade));
            return;
          }
          if (req.method() == http::verb::get &&
              target.rfind("/download?", 0) == 0) {
            self->downloadFile(target);
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
            std::string name = decode(std::string(req["X-File-Name"]));
            net::post(self->workers, [self, session, name] {
              self->uploader = api::MediaService::NewStub(
                  channel("AVPLAYER_DATA_SERVER", "localhost:50066"));
              self->uploadContext.set_deadline(
                  std::chrono::system_clock::now() + std::chrono::minutes(30));
              self->upload = self->uploader->UploadMedia(&self->uploadContext,
                                                         &self->handle);
              api::MediaChunk first;
              first.set_session_id(session);
              first.set_file_name(name);
              bool ok = self->upload->Write(first);
              net::post(self->stream.get_executor(), [self, ok] {
                if (ok)
                  self->readBody();
                else
                  self->reply(502, {{"error", "upload unavailable"}});
              });
            });
            return;
          }
          self->readBody();
        });
  }
};
int main() {
  try {
    Storage storage;
    MediaRouter router;
    UploadService upload(storage);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(
        env("AVPLAYER_DATA_SERVER_ADDRESS", "127.0.0.1:50066"),
        grpc::InsecureServerCredentials());
    builder.RegisterService(&storage);
    builder.RegisterService(&upload);
    builder.RegisterService(&router);
    auto grpcServer = builder.BuildAndStart();
    if (!grpcServer)
      throw std::runtime_error("cannot start data service");
    net::io_context io;
    net::thread_pool workers(8);
    auto host = env("AVPLAYER_WEB_GATEWAY_HOST", "127.0.0.1");
    auto port = static_cast<unsigned short>(
        std::stoi(env("AVPLAYER_WEB_GATEWAY_PORT", "50065")));
    tcp::acceptor acceptor(io, {net::ip::make_address(host), port});
    std::function<void()> accept;
    accept = [&] {
      acceptor.async_accept(
          net::make_strand(io), [&](beast::error_code ec, tcp::socket socket) {
            if (!ec)
              std::make_shared<Http>(std::move(socket), workers, router)->run();
            accept();
          });
    };
    accept();
    std::cout << "C++ gateway listening on " << host << ":" << port
              << std::endl;
    io.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
