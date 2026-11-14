#include "LeastSessionsPolicy.h"
#include <limits>
#include <stdexcept>
namespace avserver {
std::string
LeastSessionsPolicy::Select(const std::vector<std::string> &endpoints,
                            const std::map<std::string, size_t> &counts) {
  if (endpoints.empty())
    throw std::invalid_argument("no media endpoints");
  size_t minimum = std::numeric_limits<size_t>::max();
  std::vector<std::string> candidates;
  for (const auto &endpoint : endpoints) {
    auto it = counts.find(endpoint);
    size_t count = it == counts.end() ? 0 : it->second;
    if (count < minimum) {
      minimum = count;
      candidates.clear();
    }
    if (count == minimum)
      candidates.push_back(endpoint);
  }
  return candidates[turn++ % candidates.size()];
}
} // namespace avserver
