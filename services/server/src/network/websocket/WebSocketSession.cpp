#include "WebSocketSession.h"
namespace avserver::network {
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
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
  WebSocketSession(beast::tcp_stream stream, bool mode, net::thread_pool &pool)
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

void StartWebSocket(beast::tcp_stream stream, bool agentMode,
                    net::thread_pool &workers,
                    http::request<http::empty_body> request) {
  std::make_shared<WebSocketSession>(std::move(stream), agentMode, workers)
      ->run(std::move(request));
}
} // namespace avserver::network
