#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace compositors::hyprland {

  struct IpcSocketPaths {
    std::string request;
    std::string event;
  };

  class HyprlandRuntime {
  public:
    HyprlandRuntime() = default;

    [[nodiscard]] bool available() const;
    [[nodiscard]] const std::string& requestSocketPath() const;
    [[nodiscard]] const std::string& eventSocketPath() const;
    [[nodiscard]] std::optional<std::string> request(std::string_view command) const;
    void refresh();

  private:
    void ensureResolved() const;
    void resolveSocketPaths() const;

    mutable bool m_resolved = false;
    mutable IpcSocketPaths m_socketPaths;
  };

} // namespace compositors::hyprland
