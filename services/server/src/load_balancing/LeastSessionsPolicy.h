#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

//负载均衡-最少活跃会话选择策略
namespace avserver {
// Caller serializes selection and reservation under the session routing lock.
class LeastSessionsPolicy {
  uint64_t turn = 0;

public:
  std::string Select(const std::vector<std::string> &endpoints,
                     const std::map<std::string, size_t> &counts);
};
} // namespace avserver
