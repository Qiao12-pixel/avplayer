#pragma once
#include <cstdlib>
#include <string>
namespace avserver {
inline std::string Environment(const char *key, const char *fallback = "") {
  const auto *value = std::getenv(key);
  return value ? value : fallback;
}
} // namespace avserver
