#include "Registry.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <zookeeper/zookeeper.h>
namespace av {
static std::string setting(const char *key, const char *value) {
  auto p = std::getenv(key);
  return p ? p : value;
}
Registry::Registry(std::string endpoint, std::string pathVariable,
                   std::string defaultPath)
    : hosts(setting("AVPLAYER_ZK_HOSTS", "")),
      path(setting(pathVariable.c_str(), defaultPath.c_str())),
      advertised(std::move(endpoint)) {
  if (path.empty() || path.front() != '/' || path.back() == '/')
    throw std::invalid_argument("invalid ZooKeeper media path");
  if (!hosts.empty())
    thread = std::thread([this] { loop(); });
}
Registry::~Registry() {
  running = false;
  if (thread.joinable())
    thread.join();
}
std::vector<std::string> Registry::endpoints() const {
  std::lock_guard<std::mutex> lock(mutex);
  return connected ? nodes : std::vector<std::string>{};
}
void Registry::loop() {
  static std::once_flag logging;
  std::call_once(logging, [] { zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR); });
  while (running) {
    auto watcher = [](zhandle_t *, int, int, const char *, void *context) {
      static_cast<Registry *>(context)->dirty = true;
    };
    zhandle_t *handle =
        zookeeper_init(hosts.c_str(), watcher, 6000, nullptr, this, 0);
    if (!handle) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }
    bool registered = false;
    int ticks = 0;
    while (running) {
      int state = zoo_state(handle);
      const bool online = state == ZOO_CONNECTED_STATE;
      if (!online)
        connected = false;
      if (state == ZOO_EXPIRED_SESSION_STATE || state == ZOO_AUTH_FAILED_STATE)
        break;
      if (online) {
        if (!connected)
          dirty = true;
        if (!registered) {
          bool ok = true;
          size_t pos = 1;
          while (ok) {
            pos = path.find('/', pos);
            auto parent = path.substr(0, pos);
            int rc = zoo_create(handle, parent.c_str(), "", 0,
                                &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
            ok = rc == ZOK || rc == ZNODEEXISTS;
            if (pos == std::string::npos)
              break;
            ++pos;
          }
          if (ok && !advertised.empty()) {
            char created[1024];
            auto prefix = path + "/instance-";
            int rc = zoo_create(
                handle, prefix.c_str(), advertised.data(),
                static_cast<int>(advertised.size()), &ZOO_OPEN_ACL_UNSAFE,
                ZOO_EPHEMERAL | ZOO_SEQUENCE, created, sizeof(created));
            ok = rc == ZOK;
            if (ok)
              std::cout << "[registry] registered " << advertised << " at "
                        << created << std::endl;
          }
          registered = ok;
        }
        if (dirty.exchange(false) || ++ticks >= 20) {
          ticks = 0;
          String_vector children{};
          std::vector<std::string> next;
          bool complete = true;
          int rc = zoo_get_children(handle, path.c_str(), 1, &children);
          if (rc == ZOK) {
            for (int i = 0; i < children.count; ++i) {
              char data[1024];
              int size = sizeof(data);
              auto child = path + "/" + children.data[i];
              int result =
                  zoo_get(handle, child.c_str(), 1, data, &size, nullptr);
              if (result == ZNONODE)
                continue;
              if (result != ZOK) {
                complete = false;
                break;
              }
              if (size <= 0 || size > static_cast<int>(sizeof(data)))
                continue;
              std::string value(data, size);
              if (!value.empty() && value.find(':') != std::string::npos)
                next.push_back(value);
            }
            deallocate_String_vector(&children);
          } else
            complete = false;
          if (complete) {
            std::sort(next.begin(), next.end());
            next.erase(std::unique(next.begin(), next.end()), next.end());
            std::lock_guard<std::mutex> lock(mutex);
            nodes = std::move(next);
            connected = true;
          } else {
            connected = false;
            dirty = true;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    connected = false;
    zookeeper_close(handle);
    dirty = true;
  }
  connected = false;
}
} // namespace av
