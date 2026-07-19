#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pywalfox_host::settings {

  [[nodiscard]] std::optional<std::string> get(std::string_view key);
  bool set(std::string_view key, std::string_view value);

} // namespace pywalfox_host::settings
