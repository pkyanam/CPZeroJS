#pragma once
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace cpzero {
// Service adapters return JSON and throw std::exception on errors. They execute
// on the UI thread: keep them bounded and nonblocking. Long-running I/O needs a
// worker/completion queue integration rather than a blocking callback here.
using ServiceHandler = std::function<std::string(
    const std::string &method, const std::string &jsonArguments)>;
inline std::unordered_map<std::string, ServiceHandler> &serviceRegistry() {
  static std::unordered_map<std::string, ServiceHandler> registry;
  return registry;
}
inline void registerService(const std::string &name, ServiceHandler handler) {
  if (name.empty() || name == "storage" || !handler)
    throw std::invalid_argument("Invalid or reserved CPZeroJS service name");
  if (!serviceRegistry().emplace(name, std::move(handler)).second)
    throw std::invalid_argument("Duplicate CPZeroJS native service: " + name);
}
inline const ServiceHandler *findService(const std::string &name) {
  const auto found = serviceRegistry().find(name);
  return found == serviceRegistry().end() ? nullptr : &found->second;
}
} // namespace cpzero
