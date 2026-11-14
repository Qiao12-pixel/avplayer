#pragma once
#include <memory>
#include <nlohmann/json.hpp>
namespace mcp {
class MediaBackend {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  MediaBackend();
  ~MediaBackend();
  nlohmann::json Call(const std::string &tool, const nlohmann::json &args);
};
} // namespace mcp
