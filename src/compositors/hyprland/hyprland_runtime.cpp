#include "compositors/hyprland/hyprland_runtime.h"

#include "core/log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

namespace compositors::hyprland {

  namespace {

    constexpr Logger kLog("hyprland_runtime");

  } // namespace

  bool HyprlandRuntime::available() const {
    ensureResolved();
    return !m_socketPaths.request.empty();
  }

  const std::string& HyprlandRuntime::requestSocketPath() const {
    ensureResolved();
    return m_socketPaths.request;
  }

  const std::string& HyprlandRuntime::eventSocketPath() const {
    ensureResolved();
    return m_socketPaths.event;
  }

  std::optional<std::string> HyprlandRuntime::request(std::string_view command) const {
    ensureResolved();
    if (m_socketPaths.request.empty() || command.empty()) {
      return std::nullopt;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      kLog.debug("failed to create request socket: {}", std::strerror(errno));
      return std::nullopt;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (m_socketPaths.request.size() >= sizeof(addr.sun_path)) {
      kLog.debug("request socket path too long");
      ::close(fd);
      return std::nullopt;
    }
    std::memcpy(addr.sun_path, m_socketPaths.request.c_str(), m_socketPaths.request.size() + 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      kLog.debug("failed to connect to request socket {}: {}", m_socketPaths.request, std::strerror(errno));
      ::close(fd);
      return std::nullopt;
    }

    std::size_t offset = 0;
    while (offset < command.size()) {
      const ssize_t written = ::send(fd, command.data() + offset, command.size() - offset, MSG_NOSIGNAL);
      if (written <= 0) {
        if (written < 0 && errno == EINTR) {
          continue;
        }
        kLog.debug("failed to write request: {}", written < 0 ? std::strerror(errno) : "short write");
        ::close(fd);
        return std::nullopt;
      }
      offset += static_cast<std::size_t>(written);
    }

    ::shutdown(fd, SHUT_WR);

    std::string response;
    char buffer[4096];
    while (true) {
      const ssize_t read = ::recv(fd, buffer, sizeof(buffer), 0);
      if (read > 0) {
        response.append(buffer, buffer + read);
        continue;
      }
      if (read == 0) {
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      kLog.debug("failed to read response: {}", std::strerror(errno));
      ::close(fd);
      return std::nullopt;
    }

    ::close(fd);
    return response;
  }

  void HyprlandRuntime::refresh() {
    m_socketPaths = {};
    m_resolved = false;
    resolveSocketPaths();
  }

  void HyprlandRuntime::ensureResolved() const {
    if (!m_resolved) {
      resolveSocketPaths();
    }
  }

  void HyprlandRuntime::resolveSocketPaths() const {
    m_resolved = true;
    const char* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature == nullptr || signature[0] == '\0') {
      return;
    }

    std::string hyprDir;
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (runtimeDir != nullptr && runtimeDir[0] != '\0') {
      hyprDir = std::string(runtimeDir) + "/hypr/" + signature;
    }

    std::error_code ec;
    if (hyprDir.empty() || !std::filesystem::is_directory(hyprDir, ec)) {
      hyprDir = std::string("/tmp/hypr/") + signature;
    }

    ec.clear();
    if (!std::filesystem::is_directory(hyprDir, ec)) {
      return;
    }

    m_socketPaths = IpcSocketPaths{
        .request = hyprDir + "/.socket.sock",
        .event = hyprDir + "/.socket2.sock",
    };
  }

} // namespace compositors::hyprland
