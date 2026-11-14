#include "load_balancing/LeastSessionsPolicy.h"
#include <iostream>
#include <stdexcept>
static void Check(bool condition) {
  if (!condition)
    throw std::runtime_error("load balancing policy assertion failed");
}
int main() {
  avserver::LeastSessionsPolicy policy;
  std::vector<std::string> endpoints = {"a", "b"};
  std::map<std::string, size_t> counts;
  for (int i = 0; i < 4; ++i)
    ++counts[policy.Select(endpoints, counts)];
  Check(counts["a"] == 2 && counts["b"] == 2);
  Check(policy.Select(endpoints, {{"a", 8}, {"b", 1}}) == "b");
  Check(policy.Select({"b"}, {{"a", 0}, {"b", 9}}) == "b");
  bool rejected = false;
  try {
    policy.Select({}, {});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  Check(rejected);
  std::cout << "least sessions, fair ties, membership and empty registry "
               "checks passed\n";
}
