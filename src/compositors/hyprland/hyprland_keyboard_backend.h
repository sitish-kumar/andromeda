#pragma once

#include "compositors/keyboard_backend.h"

#include <functional>
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class HyprlandKeyboardBackend {
public:
  using ChangeCallback = std::function<void()>;

  explicit HyprlandKeyboardBackend(std::string_view compositorHint);
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
  bool ensureSocketPaths();
  [[nodiscard]] bool sendRequest(const std::string& request, std::string& response) const;
  [[nodiscard]] std::optional<nlohmann::json> requestJson(const std::string& request) const;
  void seedLayoutFromDevices();
  void readSocket();
  void parseMessages();
  void handleEvent(std::string_view line);

  bool m_enabled = false;
  int m_eventSocketFd = -1;
  std::string m_requestSocketPath;
  std::string m_eventSocketPath;
  std::string m_currentLayoutName;
  std::string m_mainKeyboardName;
  std::vector<char> m_readBuffer;
  ChangeCallback m_changeCallback;
};
