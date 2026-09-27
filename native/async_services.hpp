#pragma once
#include <memory>
#include <quickjs.h>
#include <string>

namespace cpzero {
class AsyncServices {
public:
  AsyncServices();
  ~AsyncServices();
  AsyncServices(const AsyncServices &) = delete;
  AsyncServices &operator=(const AsyncServices &) = delete;
  bool handles(const std::string &service) const;
  JSValue invoke(JSContext *ctx, const std::string &service,
                 const std::string &method, const std::string &jsonArgs);
  void shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cpzero
