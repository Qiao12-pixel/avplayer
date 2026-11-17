#include "Brokers.h"
#include "media/MediaFiles.h"
#if __has_include(<rabbitmq-c/tcp_socket.h>)
#include <rabbitmq-c/framing.h>
#include <rabbitmq-c/tcp_socket.h>
#else
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#endif
#include <atomic>
#include <chrono>
#include <stdexcept>
namespace mcp::messaging {
void Rabbit::check() {
  auto reply = amqp_get_rpc_reply(connection);
  if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    throw std::runtime_error("RabbitMQ RPC failed");
}
Rabbit::Rabbit(std::string selectedQueue) {
  queue = selectedQueue.empty()
              ? avtools::env("MCP_RABBIT_QUEUE", "avplayer.media.tasks")
              : std::move(selectedQueue);
  connection = amqp_new_connection();
  try {
    auto socket = amqp_tcp_socket_new(connection);
    timeval timeout{3, 0};
    if (amqp_socket_open_noblock(
            socket, avtools::env("MCP_RABBIT_HOST", "127.0.0.1").c_str(),
            std::stoi(avtools::env("MCP_RABBIT_PORT", "5672")),
            &timeout) != AMQP_STATUS_OK)
      throw std::runtime_error("RabbitMQ unavailable");
    amqp_set_rpc_timeout(connection, &timeout);
    auto login =
        amqp_login(connection, avtools::env("MCP_RABBIT_VHOST", "/").c_str(), 0,
                   131072, 0, AMQP_SASL_METHOD_PLAIN,
                   avtools::env("MCP_RABBIT_USER", "guest").c_str(),
                   avtools::env("MCP_RABBIT_PASSWORD", "guest").c_str());
    if (login.reply_type != AMQP_RESPONSE_NORMAL)
      throw std::runtime_error("RabbitMQ login failed");
    amqp_channel_open(connection, 1);
    check();
    auto dlq = queue + ".dead";
    amqp_queue_declare(connection, 1, amqp_cstring_bytes(dlq.c_str()), 0, 1, 0,
                       0, amqp_empty_table);
    check();
    amqp_table_entry_t fields[2]{};
    fields[0].key = amqp_cstring_bytes("x-dead-letter-exchange");
    fields[0].value.kind = AMQP_FIELD_KIND_UTF8;
    fields[0].value.value.bytes = amqp_empty_bytes;
    fields[1].key = amqp_cstring_bytes("x-dead-letter-routing-key");
    fields[1].value.kind = AMQP_FIELD_KIND_UTF8;
    fields[1].value.value.bytes = amqp_cstring_bytes(dlq.c_str());
    amqp_table_t table{2, fields};
    amqp_queue_declare(connection, 1, amqp_cstring_bytes(queue.c_str()), 0, 1,
                       0, 0, table);
    check();
    amqp_confirm_select(connection, 1);
    check();
  } catch (...) {
    amqp_destroy_connection(connection);
    connection = nullptr;
    throw;
  }
}
Rabbit::~Rabbit() {
  if (connection)
    amqp_destroy_connection(connection);
}
void Rabbit::Publish(const std::string &id) {
  amqp_basic_properties_t properties{};
  properties._flags = AMQP_BASIC_DELIVERY_MODE_FLAG |
                      AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_MESSAGE_ID_FLAG;
  properties.delivery_mode = 2;
  properties.content_type = amqp_cstring_bytes("text/plain");
  properties.message_id = amqp_cstring_bytes(id.c_str());
  if (amqp_basic_publish(connection, 1, amqp_empty_bytes,
                         amqp_cstring_bytes(queue.c_str()), 0, 0, &properties,
                         amqp_cstring_bytes(id.c_str())) != AMQP_STATUS_OK)
    throw std::runtime_error("Rabbit publish failed");
  amqp_frame_t frame;
  timeval timeout{3, 0};
  if (amqp_simple_wait_frame_noblock(connection, &frame, &timeout) !=
          AMQP_STATUS_OK ||
      frame.frame_type != AMQP_FRAME_METHOD ||
      frame.payload.method.id != AMQP_BASIC_ACK_METHOD)
    throw std::runtime_error("Rabbit publisher confirmation missing");
}
Rabbit::Delivery Rabbit::Fetch() {
  amqp_maybe_release_buffers(connection);
  auto reply =
      amqp_basic_get(connection, 1, amqp_cstring_bytes(queue.c_str()), 0);
  if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    throw std::runtime_error("Rabbit fetch failed");
  if (reply.reply.id == AMQP_BASIC_GET_EMPTY_METHOD)
    return {};
  if (reply.reply.id != AMQP_BASIC_GET_OK_METHOD)
    throw std::runtime_error("unexpected Rabbit response");
  auto info = static_cast<amqp_basic_get_ok_t *>(reply.reply.decoded);
  uint64_t tag = info->delivery_tag;
  amqp_message_t message{};
  auto result = amqp_read_message(connection, 1, &message, 0);
  if (result.reply_type != AMQP_RESPONSE_NORMAL)
    throw std::runtime_error("Rabbit message read failed");
  std::string id(static_cast<char *>(message.body.bytes), message.body.len);
  amqp_destroy_message(&message);
  return {id, tag};
}
void Rabbit::Requeue(uint64_t tag) {
  if (amqp_basic_reject(connection, 1, tag, 1) != AMQP_STATUS_OK)
    throw std::runtime_error("Rabbit requeue failed");
}
void Rabbit::Ack(uint64_t tag) {
  if (amqp_basic_ack(connection, 1, tag, 0) != AMQP_STATUS_OK)
    throw std::runtime_error("Rabbit ack failed");
}
void Rabbit::Reject(uint64_t tag) {
  if (amqp_basic_reject(connection, 1, tag, 0) != AMQP_STATUS_OK)
    throw std::runtime_error("Rabbit reject failed");
}
Kafka::Kafka() {
  auto config = rd_kafka_conf_new();
  char error[512];
  auto set = [&](const char *key, const std::string &value) {
    if (rd_kafka_conf_set(config, key, value.c_str(), error, sizeof(error)) !=
        RD_KAFKA_CONF_OK)
      throw std::runtime_error(error);
  };
  try {
    set("bootstrap.servers",
        avtools::env("MCP_KAFKA_BROKERS", "127.0.0.1:9092"));
    set("enable.idempotence", "true");
    set("acks", "all");
    set("message.timeout.ms", "5000");
    rd_kafka_conf_set_dr_msg_cb(
        config, [](rd_kafka_t *, const rd_kafka_message_t *message, void *) {
          auto result = static_cast<std::atomic<int> *>(message->_private);
          if (result)
            result->store(message->err ? -1 : 1);
        });
    producer = rd_kafka_new(RD_KAFKA_PRODUCER, config, error, sizeof(error));
    if (!producer)
      throw std::runtime_error(error);
  } catch (...) {
    if (!producer)
      rd_kafka_conf_destroy(config);
    throw;
  }
}
Kafka::~Kafka() {
  if (producer) {
    rd_kafka_flush(producer, 6000);
    rd_kafka_destroy(producer);
  }
}
void Kafka::Publish(const std::string &key, const std::string &payload,
                    const std::string &selectedTopic) {
  auto topic = selectedTopic.empty()
                   ? avtools::env("MCP_KAFKA_TOPIC", "avplayer.media.events")
                   : selectedTopic;
  std::atomic<int> delivered{0};
  auto error = rd_kafka_producev(
      producer, RD_KAFKA_V_TOPIC(topic.c_str()),
      RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
      RD_KAFKA_V_KEY(key.data(), key.size()),
      RD_KAFKA_V_VALUE(const_cast<char *>(payload.data()), payload.size()),
      RD_KAFKA_V_OPAQUE(&delivered), RD_KAFKA_V_END);
  if (error)
    throw std::runtime_error(rd_kafka_err2str(error));
  while (delivered.load() == 0)
    rd_kafka_poll(producer, 100);
  if (delivered < 0)
    throw std::runtime_error("Kafka delivery failed");
}
} // namespace mcp::messaging
