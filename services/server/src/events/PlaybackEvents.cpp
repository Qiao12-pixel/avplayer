#include "PlaybackEvents.h"
#include "config/Environment.h"
#include "persistence/EventStore.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <librdkafka/rdkafka.h>
#include <mutex>
#include <random>
#include <thread>
namespace avserver {
using Json = nlohmann::json;
struct PlaybackEvents::Impl {
  persistence::EventStore store;
  std::atomic<bool> stop{false};
  std::thread sender, heartbeat;
  std::string instance = [] {
    auto fallback = "server-" + persistence::UniqueId();
    return Environment("AVPLAYER_INSTANCE_ID", fallback.c_str());
  }();
  Impl() {
    Enqueue("server.started", "", "", "", Json::object());
    sender = std::thread([this] { loop(); });
    heartbeat = std::thread([this] {
      while (!stop) {
        for (int i = 0; i < 50 && !stop; ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!stop) {
          try {
            Enqueue("server.heartbeat", "", "", "", Json::object());
          } catch (const std::exception &e) {
            std::cerr << "[event heartbeat] " << e.what() << std::endl;
          }
        }
      }
    });
  }
  ~Impl() {
    stop = true;
    if (heartbeat.joinable())
      heartbeat.join();
    if (sender.joinable())
      sender.join();
  }
  void Enqueue(const std::string &type, const std::string &session,
               const std::string &media, const std::string &playback,
               Json details) {
    auto eventId = persistence::UniqueId();
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    Json value = {{"schema_version", 1},
                  {"event_id", eventId},
                  {"event_type", type},
                  {"occurred_at_ms", milliseconds},
                  {"instance_id", instance},
                  {"session_id", session},
                  {"media_id", media},
                  {"playback_id", playback},
                  {"details", std::move(details)}};
    store.Append(
        "avplayer.playback",
        playback.empty() ? session + ":" + media : playback,
        Environment("AVPLAYER_KAFKA_TOPIC", "avplayer.playback.events"), value);
  }

  void loop() {
    while (!stop) {
      rd_kafka_t *producer = nullptr;
      try {
        char error[512];
        auto conf = rd_kafka_conf_new();
        auto set = [&](const char *key, const std::string &value) {
          if (rd_kafka_conf_set(conf, key, value.c_str(), error,
                                sizeof(error)) != RD_KAFKA_CONF_OK)
            throw std::runtime_error(error);
        };
        set("bootstrap.servers",
            Environment("AVPLAYER_KAFKA_BROKERS", "127.0.0.1:9092"));
        set("enable.idempotence", "true");
        set("acks", "all");
        set("message.timeout.ms", "4000");
        rd_kafka_conf_set_dr_msg_cb(
            conf, [](rd_kafka_t *, const rd_kafka_message_t *message, void *) {
              auto flag = static_cast<std::atomic<int> *>(message->_private);
              if (flag)
                flag->store(message->err ? -1 : 1);
            });
        producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf, error, sizeof(error));
        if (!producer)
          throw std::runtime_error(error);
        while (!stop) {
          auto events = store.Pending("avplayer.playback");
          if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
          }
          auto event = events.front();
          auto key = event.at("key").get<std::string>(),
               payload = event.at("payload").get<std::string>();
          std::atomic<int> delivered{0};
          auto topic = event.at("topic").get<std::string>();
          auto status = rd_kafka_producev(
              producer, RD_KAFKA_V_TOPIC(topic.c_str()),
              RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
              RD_KAFKA_V_KEY(key.data(), key.size()),
              RD_KAFKA_V_VALUE(payload.data(), payload.size()),
              RD_KAFKA_V_OPAQUE(&delivered), RD_KAFKA_V_END);
          if (status)
            throw std::runtime_error(rd_kafka_err2str(status));
          while (!delivered)
            rd_kafka_poll(producer, 100);
          if (delivered < 0)
            throw std::runtime_error("playback Kafka delivery failed");
          store.Sent(event);
        }
      } catch (const std::exception &e) {
        std::cerr << "[playback Kafka] " << e.what() << std::endl;
      }
      if (producer) {
        rd_kafka_flush(producer, 5000);
        rd_kafka_destroy(producer);
      }
      for (int i = 0; i < 10 && !stop; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
};
PlaybackEvents::PlaybackEvents() : impl(std::make_unique<Impl>()) {}
PlaybackEvents::~PlaybackEvents() = default;
void PlaybackEvents::Record(const std::string &type, const std::string &session,
                            const std::string &media,
                            const std::string &playback,
                            Json details) noexcept {
  try {
    impl->Enqueue(type, session, media, playback, std::move(details));
  } catch (const std::exception &e) {
    std::cerr << "[playback event dropped] " << e.what() << std::endl;
  }
}
Json PlaybackStatistics() {
  try {
    persistence::EventStore store;
    return store.Statistics(
        Environment("AVPLAYER_STATS_PROJECTION", "playback-v1"));
  } catch (const std::exception &e) {
    return {{"success", false}, {"error", e.what()}};
  }
}
} // namespace avserver
