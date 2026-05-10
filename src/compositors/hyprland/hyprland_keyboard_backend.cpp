#include "compositors/hyprland/hyprland_keyboard_backend.h"

#include "core/log.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

  constexpr Logger kLog("keyboard_hyprland");

} // namespace

HyprlandKeyboardBackend::HyprlandKeyboardBackend(std::string_view compositorHint) {
  const bool hinted = StringUtils::containsInsensitive(compositorHint, "hyprland") ||
                      StringUtils::containsInsensitive(compositorHint, "hypr");
  const char* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  m_enabled = hinted || (signature != nullptr && signature[0] != '\0');
}

HyprlandKeyboardBackend::~HyprlandKeyboardBackend() { cleanup(); }

bool HyprlandKeyboardBackend::isAvailable() const noexcept { return m_enabled; }

bool HyprlandKeyboardBackend::cycleLayout() const {
  if (!m_enabled || m_requestSocketPath.empty()) {
    return false;
  }
  std::string response;
  return sendRequest("switchxkblayout all next", response);
}

std::optional<KeyboardLayoutState> HyprlandKeyboardBackend::layoutState() const {
  const auto current = currentLayoutName();
  if (!current.has_value()) {
    return std::nullopt;
  }
  return KeyboardLayoutState{{*current}, 0};
}

std::optional<std::string> HyprlandKeyboardBackend::currentLayoutName() const {
  if (!m_enabled || m_currentLayoutName.empty()) {
    return std::nullopt;
  }
  return m_currentLayoutName;
}

bool HyprlandKeyboardBackend::connectSocket() {
  if (!m_enabled || !ensureSocketPaths()) {
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
  if (m_eventSocketPath.size() >= sizeof(addr.sun_path)) {
    kLog.warn("hyprland keyboard IPC socket path too long");
    cleanup();
    return false;
  }
  std::memcpy(addr.sun_path, m_eventSocketPath.c_str(), m_eventSocketPath.size() + 1);

  if (::connect(m_eventSocketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    kLog.warn("failed to connect to hyprland keyboard IPC {}: {}", m_eventSocketPath, std::strerror(errno));
    cleanup();
    return false;
  }

  const int flags = ::fcntl(m_eventSocketFd, F_GETFL, 0);
  if (flags >= 0) {
    (void)::fcntl(m_eventSocketFd, F_SETFL, flags | O_NONBLOCK);
  }

  seedLayoutFromDevices();
  kLog.info("connected to hyprland keyboard IPC at {}", m_eventSocketPath);
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

bool HyprlandKeyboardBackend::ensureSocketPaths() {
  if (!m_requestSocketPath.empty() && !m_eventSocketPath.empty()) {
    return true;
  }

  const char* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (signature == nullptr || signature[0] == '\0') {
    return false;
  }

  std::string hyprDir;
  const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
  if (runtimeDir != nullptr && runtimeDir[0] != '\0') {
    hyprDir = std::string(runtimeDir) + "/hypr/" + signature;
  }

  if (hyprDir.empty() || !std::filesystem::is_directory(hyprDir)) {
    hyprDir = std::string("/tmp/hypr/") + signature;
  }

  if (!std::filesystem::is_directory(hyprDir)) {
    return false;
  }

  m_requestSocketPath = hyprDir + "/.socket.sock";
  m_eventSocketPath = hyprDir + "/.socket2.sock";
  return true;
}

bool HyprlandKeyboardBackend::sendRequest(const std::string& request, std::string& response) const {
  if (m_requestSocketPath.empty()) {
    return false;
  }

  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (m_requestSocketPath.size() >= sizeof(addr.sun_path)) {
    ::close(fd);
    return false;
  }
  std::memcpy(addr.sun_path, m_requestSocketPath.c_str(), m_requestSocketPath.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return false;
  }

  std::size_t offset = 0;
  while (offset < request.size()) {
    const ssize_t written = ::send(fd, request.data() + offset, request.size() - offset, MSG_NOSIGNAL);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      ::close(fd);
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }

  ::shutdown(fd, SHUT_WR);

  std::string out;
  char buffer[4096];
  while (true) {
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n > 0) {
      out.append(buffer, buffer + n);
      continue;
    }
    if (n == 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    ::close(fd);
    return false;
  }

  ::close(fd);
  response = std::move(out);
  return true;
}

std::optional<nlohmann::json> HyprlandKeyboardBackend::requestJson(const std::string& request) const {
  std::string response;
  if (!sendRequest(request, response) || response.empty()) {
    return std::nullopt;
  }
  try {
    return nlohmann::json::parse(response);
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
