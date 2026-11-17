#include "config/Environment.h"
#include "persistence/EventStore.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <librdkafka/rdkafka.h>
#include <nlohmann/json.hpp>
#include <thread>
using Json = nlohmann::json;
namespace {
std::atomic<bool> running{true};
}
int main() {

  rd_kafka_t *consumer = nullptr;
  try {
    std::signal(SIGINT, [](int) { running = false; });
    std::signal(SIGTERM, [](int) { running = false; });
    persistence::EventStore store;
    auto projection =
        avserver::Environment("AVPLAYER_STATS_PROJECTION", "playback-v1");
    auto conf = rd_kafka_conf_new();
    char error[512];
    auto set = [&](const char *key, const std::string &value) {
      if (rd_kafka_conf_set(conf, key, value.c_str(), error, sizeof(error)) !=
          RD_KAFKA_CONF_OK)
        throw std::runtime_error(error);
    };
    set("bootstrap.servers",
        avserver::Environment("AVPLAYER_KAFKA_BROKERS", "127.0.0.1:9092"));
    set("group.id", avserver::Environment("AVPLAYER_KAFKA_GROUP",
                                          "avplayer-playback-stats"));
    set("enable.auto.commit", "false");
    set("auto.offset.reset", "earliest");
    set("allow.auto.create.topics", "true");
    consumer = rd_kafka_new(RD_KAFKA_CONSUMER, conf, error, sizeof(error));
    if (!consumer)
      throw std::runtime_error(error);
    rd_kafka_poll_set_consumer(consumer);
    auto topics = rd_kafka_topic_partition_list_new(1);
    auto topic = avserver::Environment("AVPLAYER_KAFKA_TOPIC",
                                       "avplayer.playback.events");
    rd_kafka_topic_partition_list_add(topics, topic.c_str(),
                                      RD_KAFKA_PARTITION_UA);
    auto rc = rd_kafka_subscribe(consumer, topics);
    rd_kafka_topic_partition_list_destroy(topics);
    if (rc)
      throw std::runtime_error(rd_kafka_err2str(rc));
    while (running) {
      auto message = rd_kafka_consumer_poll(consumer, 500);
      if (!message)
        continue;
      if (message->err) {
        rd_kafka_message_destroy(message);
        if (rd_kafka_fatal_error(consumer, error, sizeof(error)) !=
            RD_KAFKA_RESP_ERR_NO_ERROR)
          throw std::runtime_error(error);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        continue;
      }
      try {
        auto payload =
            std::string(static_cast<char *>(message->payload), message->len);
        auto value = Json::parse(payload);
        store.Playback(projection, value);
        auto status = rd_kafka_commit_message(consumer, message, 0);
        if (status)
          throw std::runtime_error(rd_kafka_err2str(status));
        std::cout << "[playback-stats] committed "
                  << value.at("event_id").get<std::string>() << std::endl;
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
    std::cerr << "playback consumer stopped without skipping failed event: "
              << e.what() << std::endl;
    if (consumer) {
      rd_kafka_consumer_close(consumer);
      rd_kafka_destroy(consumer);
    }
    return 1;
  }
}
