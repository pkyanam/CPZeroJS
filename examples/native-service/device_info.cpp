#include "services.hpp"

// Compile with -DCPZERO_EXTENSION_SOURCES=/absolute/path/to/device_info.cpp.
// Applications call services.call('deviceInfo', 'profile').
namespace {
const bool registered = [] {
  cpzero::registerService("deviceInfo", [](const std::string& method, const std::string&) {
    if (method != "profile") throw std::invalid_argument("Unknown deviceInfo method");
    return std::string(R"({"width":320,"height":170,"profile":"cardputer-zero"})");
  });
  return true;
}();
}
