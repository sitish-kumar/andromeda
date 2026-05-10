#include "compositors/hyprland/hyprland_keyboard_backend.h"

#include "compositors/hyprland/hyprland_runtime.h"
#include "core/log.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

  constexpr Logger kLog("keyboard_hyprland");

} // namespace

HyprlandKeyboardBackend::HyprlandKeyboardBackend(compositors::hyprland::HyprlandRuntime& runtime)
    : m_runtime(runtime) {}

HyprlandKeyboardBackend::~HyprlandKeyboardBackend() { cleanup(); }

bool HyprlandKeyboardBackend::isAvailable() const noexcept { return m_runtime.available(); }

bool HyprlandKeyboardBackend::cycleLayout() const { return m_runtime.request("switchxkblayout all next").has_value(); }

std::optional<KeyboardLayoutState> HyprlandKeyboardBackend::layoutState() const {
  const auto current = currentLayoutName();
  if (!current.has_value()) {
    return std::nullopt;
  }
  return KeyboardLayoutState{{*current}, 0};
}

std::optional<std::string> HyprlandKeyboardBackend::currentLayoutName() const {
  if (m_currentLayoutName.empty()) {
    return std::nullopt;
  }
  return m_currentLayoutName;
}

bool HyprlandKeyboardBackend::connectSocket() {
  const auto& eventSocketPath = m_runtime.eventSocketPath();
  if (eventSocketPath.empty()) {
    return false;
  }

  cleanup();

  m_eventSocketFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (m_eventSocketFd < 0) {
    kLog.warn("failed to create hyprland keyboard IPC socket: {}", std::strerror(errno));
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (eventSocketPath.size() >= sizeof(addr.sun_path)) {
    kLog.warn("hyprland keyboard IPC socket path too long");
    cleanup();
    return false;
  }
  std::memcpy(addr.sun_path, eventSocketPath.c_str(), eventSocketPath.size() + 1);

  if (::connect(m_eventSocketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    kLog.warn("failed to connect to hyprland keyboard IPC {}: {}", eventSocketPath, std::strerror(errno));
    cleanup();
    return false;
  }

  const int flags = ::fcntl(m_eventSocketFd, F_GETFL, 0);
  if (flags >= 0) {
    (void)::fcntl(m_eventSocketFd, F_SETFL, flags | O_NONBLOCK);
  }

  seedLayoutFromDevices();
  kLog.info("connected to hyprland keyboard IPC at {}", eventSocketPath);
  return true;
}

void HyprlandKeyboardBackend::setChangeCallback(ChangeCallback callback) { m_changeCallback = std::move(callback); }

void HyprlandKeyboardBackend::dispatchPoll(short revents) {
  if (m_eventSocketFd < 0) {
    return;
  }
  if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    kLog.warn("hyprland keyboard IPC disconnected");
    cleanup();
    if (m_changeCallback) {
      m_changeCallback();
    }
    return;
  }
  if ((revents & POLLIN) != 0) {
    readSocket();
  }
}

void HyprlandKeyboardBackend::cleanup() {
  if (m_eventSocketFd >= 0) {
    ::close(m_eventSocketFd);
    m_eventSocketFd = -1;
  }
  m_readBuffer.clear();
  m_currentLayoutName.clear();
  m_mainKeyboardName.clear();
}

std::optional<nlohmann::json> HyprlandKeyboardBackend::requestJson(const std::string& request) const {
  const auto response = m_runtime.request(request);
  if (!response.has_value() || response->empty()) {
    return std::nullopt;
  }
  try {
    return nlohmann::json::parse(*response);
  } catch (const nlohmann::json::exception& e) {
    kLog.warn("failed to parse hyprland response for {}: {}", request, e.what());
    return std::nullopt;
  }
}

void HyprlandKeyboardBackend::seedLayoutFromDevices() {
  const auto json = requestJson("j/devices");
  if (!json || !json->is_object()) {
    return;
  }

  const auto keyboardsIt = json->find("keyboards");
  if (keyboardsIt == json->end() || !keyboardsIt->is_array()) {
    return;
  }

  for (const auto& keyboard : *keyboardsIt) {
    if (!keyboard.is_object() || !keyboard.value("main", false)) {
      continue;
    }
    const std::string layout = keyboard.value("active_keymap", "");
    if (!layout.empty() && layout != "error") {
      m_currentLayoutName = layout;
      m_mainKeyboardName = keyboard.value("name", "");
      return;
    }
  }
}

void HyprlandKeyboardBackend::readSocket() {
  char buffer[4096];
  while (true) {
    const ssize_t n = ::recv(m_eventSocketFd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (n > 0) {
      m_readBuffer.insert(m_readBuffer.end(), buffer, buffer + n);
      continue;
    }
    if (n == 0) {
      cleanup();
      if (m_changeCallback) {
        m_changeCallback();
      }
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    kLog.warn("failed to read from hyprland keyboard IPC: {}", std::strerror(errno));
    cleanup();
    if (m_changeCallback) {
      m_changeCallback();
    }
    return;
  }

  parseMessages();
}

void HyprlandKeyboardBackend::parseMessages() {
  while (true) {
    auto it = std::find(m_readBuffer.begin(), m_readBuffer.end(), '\n');
    if (it == m_readBuffer.end()) {
      return;
    }
    std::string line(m_readBuffer.begin(), it);
    m_readBuffer.erase(m_readBuffer.begin(), it + 1);
    if (!line.empty()) {
      handleEvent(line);
    }
  }
}

void HyprlandKeyboardBackend::handleEvent(std::string_view line) {
  const auto split = line.find(">>");
  if (split == std::string_view::npos) {
    return;
  }

  const std::string_view event = line.substr(0, split);
  if (event != "activelayout") {
    return;
  }

  const std::string_view data = line.substr(split + 2);
  const auto comma = data.find(',');
  if (comma == std::string_view::npos || comma + 1 >= data.size()) {
    return;
  }

  const std::string_view keyboardName = data.substr(0, comma);
  const std::string_view layoutName = data.substr(comma + 1);

  if (!m_mainKeyboardName.empty() && keyboardName != m_mainKeyboardName) {
    return;
  }

  m_currentLayoutName = std::string(layoutName);
  if (m_changeCallback) {
    m_changeCallback();
  }
}
