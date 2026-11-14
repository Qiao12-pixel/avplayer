#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace av {
// Optional ZooKeeper registration/discovery. No ZooKeeper traffic on I/O
// threads.
class Registry {
public:
  explicit Registry(std::string advertised = "",
                    std::string pathVariable = "AVPLAYER_ZK_MEDIA_PATH",
                    std::string defaultPath = "/avplayer/media");
  ~Registry();
  Registry(const Registry &) = delete;
  std::vector<std::string> endpoints() const;
  bool enabled() const { return !hosts.empty(); }
  bool ready() const { return connected.load(); }

private:
  void loop();
  std::string hosts, path, advertised;
  std::atomic<bool> running{true}, connected{false}, dirty{true};
  mutable std::mutex mutex;
  std::vector<std::string> nodes;
  std::thread thread;
};
} // namespace av
