#include "persistence/EventStore.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <librdkafka/rdkafka.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
std::atomic<bool> running{true};
}

int main() {
  rd_kafka_t *consumer = nullptr;
  try {
    std::signal(SIGINT, [](int) { running = false; });
    std::signal(SIGTERM, [](int) { running = false; });

    persistence::EventStore store;
    const auto projection =
        persistence::Env("MCP_EVENT_PROJECTION", "mcp-audit-v1");
    auto *config = rd_kafka_conf_new();
    char error[512];
    auto set = [&](const char *key, const std::string &value) {
      if (rd_kafka_conf_set(config, key, value.c_str(), error,
                            sizeof(error)) != RD_KAFKA_CONF_OK)
        throw std::runtime_error(error);
    };
    set("bootstrap.servers",
        persistence::Env("MCP_KAFKA_BROKERS", "127.0.0.1:9092"));
    set("group.id",
        persistence::Env("MCP_KAFKA_GROUP", "avplayer-media-audit"));
    set("enable.auto.commit", "false");
    set("auto.offset.reset", "earliest");
    set("allow.auto.create.topics", "true");
    consumer = rd_kafka_new(RD_KAFKA_CONSUMER, config, error, sizeof(error));
    if (!consumer)
      throw std::runtime_error(error);
    rd_kafka_poll_set_consumer(consumer);

    const auto topicName =
        persistence::Env("MCP_KAFKA_TOPIC", "avplayer.media.events");
    auto *topics = rd_kafka_topic_partition_list_new(1);
    rd_kafka_topic_partition_list_add(topics, topicName.c_str(),
                                      RD_KAFKA_PARTITION_UA);
    auto rc = rd_kafka_subscribe(consumer, topics);
    rd_kafka_topic_partition_list_destroy(topics);
    if (rc)
      throw std::runtime_error(rd_kafka_err2str(rc));

    while (running) {
      auto *message = rd_kafka_consumer_poll(consumer, 500);
      if (!message)
        continue;
      if (message->err) {
        const auto err = message->err;
        rd_kafka_message_destroy(message);
        if (err == RD_KAFKA_RESP_ERR__PARTITION_EOF)
          continue;
        if (rd_kafka_fatal_error(consumer, error, sizeof(error)) !=
            RD_KAFKA_RESP_ERR_NO_ERROR)
          throw std::runtime_error(error);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        continue;
      }

      try {
        const auto payload =
            std::string(static_cast<const char *>(message->payload),
                        message->len);
        const auto event = nlohmann::json::parse(payload);
        store.Audit(projection, event);
        const auto commit = rd_kafka_commit_message(consumer, message, 0);
        if (commit)
          throw std::runtime_error(rd_kafka_err2str(commit));
        std::cout << "[mcp-audit] committed "
                  << event.at("event_id").get<std::string>() << std::endl;
      } catch (...) {
        rd_kafka_message_destroy(message);
        throw;
      }
      rd_kafka_message_destroy(message);
    }

    rd_kafka_consumer_close(consumer);
    rd_kafka_destroy(consumer);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "MCP event consumer stopped without committing the failed "
                 "record: "
              << e.what() << std::endl;
    if (consumer) {
      rd_kafka_consumer_close(consumer);
      rd_kafka_destroy(consumer);
    }
    return 1;
  }
}
