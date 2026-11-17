#pragma once
#include <memory>
#include <nlohmann/json.hpp>
namespace avserver {
class PlaybackEvents {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  PlaybackEvents();
  ~PlaybackEvents();
  PlaybackEvents(const PlaybackEvents &) = delete;
  void Record(const std::string &type, const std::string &session,
              const std::string &media, const std::string &playback,
              nlohmann::json details = {}) noexcept;
};
nlohmann::json PlaybackStatistics();
} // namespace avserver
