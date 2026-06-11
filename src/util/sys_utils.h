#pragma once

#include <cstdlib>
#include <string_view>

namespace SysUtils {

  [[nodiscard]] inline bool isEnvFlagOn(std::string_view name) {
    const char* s = std::getenv(name.data());
    if (s == nullptr) {
      return false;
    }
    std::string_view sv(s);
    return !sv.empty() && sv != "0" && sv != "false" && sv != "no" && sv != "off";
  }

} // namespace SysUtils
