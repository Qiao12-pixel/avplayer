#pragma once
#include <atomic>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace avtools {
namespace fs = std::filesystem;
using json = nlohmann::json;
inline std::string env(const char *key, const char *fallback) {
  auto value = std::getenv(key);
  return value ? value : fallback;
}
inline bool id(const std::string &v) {
  return !v.empty() && v.size() <= 128 &&
         v.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVW"
                             "XYZ0123456789-_") == std::string::npos;
}
inline fs::path root() {
  return fs::weakly_canonical(env("AVPLAYER_STORAGE_ROOT", "storage"));
}
inline bool inside(const fs::path &base, const fs::path &path) {
  auto rel =
      fs::weakly_canonical(path).lexically_relative(fs::weakly_canonical(base));
  return !rel.empty() && !rel.is_absolute() && *rel.begin() != "..";
}
inline fs::path mediaDirectory(const std::string &session,
                               const std::string &media) {
  if (!id(session) || !id(media))
    throw std::invalid_argument("invalid session/media ID");
  auto path = root() / session / media;
  if (!inside(root(), path))
    throw std::invalid_argument("media directory outside storage");
  return path;
}
inline fs::path original(const fs::path &dir) {
  if (!fs::is_directory(dir))
    return {};
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (entry.path().stem() == "original" && entry.is_regular_file()) {
      if (!inside(root(), entry.path()))
        throw std::invalid_argument("source outside storage");
      return entry.path();
    }
  }
  return {};
}
inline json readJson(const fs::path &path) {
  if (!inside(root(), path))
    throw std::invalid_argument("file outside storage");
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read artifact");
  json result;
  input >> result;
  return result;
}
inline void atomicWrite(const fs::path &path, const std::string &text) {
  if (!inside(root(), path))
    throw std::invalid_argument("file outside storage");
  fs::create_directories(path.parent_path());
  static std::atomic<unsigned long> serial{0};
  auto temp = path.string() + ".tmp-" + std::to_string(getpid()) + "-" +
              std::to_string(++serial);
  try {
    {
      std::ofstream output(temp, std::ios::binary);
      output << text;
      output.close();
      if (!output)
        throw std::runtime_error("artifact write failed");
    }
    fs::rename(temp, path);
  } catch (...) {
    std::error_code ec;
    fs::remove(temp, ec);
    throw;
  }
}
} // namespace avtools
