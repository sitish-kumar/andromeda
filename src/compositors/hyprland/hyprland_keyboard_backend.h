#pragma once

#include "compositors/keyboard_backend.h"

#include <functional>
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace compositors::hyprland {
  class HyprlandRuntime;
} // namespace compositors::hyprland

class HyprlandKeyboardBackend {
public:
  using ChangeCallback = std::function<void()>;

  explicit HyprlandKeyboardBackend(compositors::hyprland::HyprlandRuntime& runtime);
  ~HyprlandKeyboardBackend();

  HyprlandKeyboardBackend(const HyprlandKeyboardBackend&) = delete;
  HyprlandKeyboardBackend& operator=(const HyprlandKeyboardBackend&) = delete;

  [[nodiscard]] bool isAvailable() const noexcept;
  [[nodiscard]] bool cycleLayout() const;
  [[nodiscard]] std::optional<KeyboardLayoutState> layoutState() const;
  [[nodiscard]] std::optional<std::string> currentLayoutName() const;

  bool connectSocket();
  void setChangeCallback(ChangeCallback callback);
  [[nodiscard]] int pollFd() const noexcept { return m_eventSocketFd; }
  void dispatchPoll(short revents);
  void cleanup();

private:
  [[nodiscard]] std::optional<nlohmann::json> requestJson(const std::string& request) const;
  void seedLayoutFromDevices();
  void readSocket();
  void parseMessages();
  void handleEvent(std::string_view line);

  compositors::hyprland::HyprlandRuntime& m_runtime;
  int m_eventSocketFd = -1;
  std::string m_currentLayoutName;
  std::string m_mainKeyboardName;
  std::vector<char> m_readBuffer;
  ChangeCallback m_changeCallback;
};
