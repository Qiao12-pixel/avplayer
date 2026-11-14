#include "NetworkSupport.h"
namespace avserver::network {
std::string DecodeUrl(std::string s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      char *end = nullptr;
      auto h = s.substr(i + 1, 2);
      long v = strtol(h.c_str(), &end, 16);
      if (end == h.c_str() + 2) {
        out += char(v);
        i += 2;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}
} // namespace avserver::network
