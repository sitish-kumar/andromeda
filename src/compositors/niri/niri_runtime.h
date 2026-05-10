#pragma once

#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace compositors::niri {

  class NiriRuntime {
  public:
    NiriRuntime() = default;

    [[nodiscard]] bool available() const;
    [[nodiscard]] const std::string& socketPath() const;
    [[nodiscard]] std::optional<nlohmann::json> requestJson(std::string_view request) const;
    void refresh();

  private:
    void ensureResolved() const;
    void resolveSocketPath() const;

    mutable bool m_resolved = false;
    mutable std::string m_socketPath;
  };

} // namespace compositors::niri
