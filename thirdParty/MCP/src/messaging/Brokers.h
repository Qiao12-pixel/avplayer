#pragma once
#if __has_include(<rabbitmq-c/amqp.h>)
#include <rabbitmq-c/amqp.h>
#else
#include <amqp.h>
#endif
#include <librdkafka/rdkafka.h>
#include <string>
namespace mcp::messaging {
class Rabbit {
  amqp_connection_state_t connection = nullptr;
  std::string queue;
  void check();

public:
  struct Delivery {
    std::string id;
    uint64_t tag = 0;
  };
  explicit Rabbit(std::string selectedQueue = "");
  ~Rabbit();
  Rabbit(const Rabbit &) = delete;
  void Publish(const std::string &id);
  Delivery Fetch();
  void Requeue(uint64_t tag);
  void Ack(uint64_t tag);
  void Reject(uint64_t tag);
};
class Kafka {
  rd_kafka_t *producer = nullptr;

public:
  Kafka();
  ~Kafka();
  void Publish(const std::string &key, const std::string &payload,
               const std::string &topic = "");
};
} // namespace mcp::messaging
