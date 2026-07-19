#include "tools/pywalfox/pywalfox_host.h"

#include "tools/pywalfox/css.h"
#include "tools/pywalfox/native_messaging.h"
#include "tools/pywalfox/settings.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <print>
#include <string>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace pywalfox_host {
  namespace {

    constexpr std::string_view kActionVersion = "debug:version";
    constexpr std::string_view kActionColors = "action:colors";
    constexpr std::string_view kActionInvalid = "action:invalid";
    constexpr std::string_view kActionCssEnable = "css:enable";
    constexpr std::string_view kActionCssDisable = "css:disable";
    constexpr std::string_view kActionCssFontSize = "css:font:size";
    constexpr std::string_view kActionThemeMode = "theme:mode";

    constexpr std::string_view kCmdUpdate = "action:update";
    constexpr std::string_view kCmdDark = "theme:mode:dark";
    constexpr std::string_view kCmdLight = "theme:mode:light";
    constexpr std::string_view kCmdAuto = "theme:mode:auto";

    [[nodiscard]] std::filesystem::path homeDir() {
      if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return home;
      }
      return {};
    }

    [[nodiscard]] std::filesystem::path xdgCacheHome() {
      if (const char* cache = std::getenv("XDG_CACHE_HOME"); cache != nullptr && cache[0] != '\0') {
        return cache;
      }
      const auto home = homeDir();
      return home.empty() ? std::filesystem::path{} : home / ".cache";
    }

    [[nodiscard]] std::string selfExePath() {
      char buf[4096];
      const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
      if (n <= 0) {
        return {};
      }
      buf[n] = '\0';
      return std::string(buf, static_cast<std::size_t>(n));
    }

    bool setCloexec(int fd) {
      const int flags = ::fcntl(fd, F_GETFD);
      if (flags < 0) {
        return false;
      }
      return ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
    }

    bool setNonBlocking(int fd) {
      const int flags = ::fcntl(fd, F_GETFL);
      if (flags < 0) {
        return false;
      }
      return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    struct ColorsPayload {
      std::vector<std::string> colors;
      std::string wallpaper;
      std::string error;
      bool ok = false;
    };

    [[nodiscard]] ColorsPayload loadColors() {
      ColorsPayload out;
      const auto path = colorsJsonPath();
      std::ifstream in(path);
      if (!in) {
        out.error = "Could not read colors from: " + path.string();
        return out;
      }

      nlohmann::json root;
      try {
        in >> root;
      } catch (const std::exception& e) {
        out.error = std::string("Failed to read colors: ") + e.what();
        return out;
      }

      if (!root.contains("colors") || !root["colors"].is_object()) {
        out.error = path.string() + " does not contain any color values";
        return out;
      }
      if (!root.contains("wallpaper") || !root["wallpaper"].is_string()) {
        out.error = path.string() + " does not contain a wallpaper path";
        return out;
      }

      // Preserve pywal key order color0..color15 when present.
      const auto& colorsObj = root["colors"];
      for (int i = 0; i < 16; ++i) {
        const std::string key = "color" + std::to_string(i);
        if (colorsObj.contains(key) && colorsObj[key].is_string()) {
          out.colors.push_back(colorsObj[key].get<std::string>());
        }
      }
      if (out.colors.size() < 16) {
        out.colors.clear();
        for (auto it = colorsObj.begin(); it != colorsObj.end(); ++it) {
          if (it.value().is_string()) {
            out.colors.push_back(it.value().get<std::string>());
          }
        }
      }

      if (out.colors.size() < 16) {
        out.error = path.string() + " containing the generated Pywal colors is invalid";
        return out;
      }

      out.wallpaper = root["wallpaper"].get<std::string>();
      out.ok = true;
      return out;
    }

    void sendMessage(const nlohmann::json& message) { native_messaging::writeMessage(message); }

    void sendVersion() {
      sendMessage({{"action", std::string(kActionVersion)}, {"success", true}, {"data", std::string(kDaemonVersion)}});
    }

    void sendColors() {
      const auto payload = loadColors();
      nlohmann::json msg = {{"action", std::string(kActionColors)}, {"success", payload.ok}};
      if (payload.ok) {
        msg["data"] = {{"colors", payload.colors}, {"wallpaper", payload.wallpaper}};
      } else {
        msg["error"] = payload.error;
      }
      sendMessage(msg);
    }

    void sendInvalid() { sendMessage({{"action", std::string(kActionInvalid)}, {"success", false}}); }

    void sendThemeMode(std::string_view mode) {
      sendMessage({{"action", std::string(kActionThemeMode)}, {"success", true}, {"data", std::string(mode)}});
    }

    void handleExtensionMessage(const nlohmann::json& message, bool& persistedStateSent) {
      if (!message.contains("action") || !message["action"].is_string()) {
        sendInvalid();
        return;
      }
      const std::string action = message["action"].get<std::string>();
      if (action == kActionVersion) {
        sendVersion();
        return;
      }
      if (action == kActionColors) {
        sendColors();
        if (!persistedStateSent) {
          persistedStateSent = true;
          if (const auto mode = settings::get("theme_mode"); mode.has_value() && !mode->empty()) {
            sendThemeMode(*mode);
          }
        }
        return;
      }
      if (action == kActionCssEnable || action == kActionCssDisable || action == kActionCssFontSize) {
        if (!message.contains("target")
            || !message["target"].is_string()
            || message["target"].get<std::string>().empty()) {
          sendInvalid();
          return;
        }
        const std::string target = message["target"].get<std::string>();
        std::string err;
        const auto chrome = css::firefoxChromePath(&err);
        if (!chrome) {
          sendMessage(
              {{"action", action},
               {"success", false},
               {"data", target},
               {"error", err.empty() ? "Could not find path to chrome folder" : err}}
          );
          return;
        }

        bool ok = false;
        std::string messageText;
        nlohmann::json data = target;
        if (action == kActionCssEnable) {
          ok = css::enable(*chrome, target, &messageText);
        } else if (action == kActionCssDisable) {
          ok = css::disable(*chrome, target, &messageText);
        } else {
          if (!message.contains("size")) {
            sendInvalid();
            return;
          }
          int size = 0;
          if (message["size"].is_number_integer()) {
            size = message["size"].get<int>();
          } else if (message["size"].is_string()) {
            try {
              size = std::stoi(message["size"].get<std::string>());
            } catch (...) {
              sendInvalid();
              return;
            }
          } else {
            sendInvalid();
            return;
          }
          ok = css::setFontSize(*chrome, target, size, &messageText);
          data = size;
        }
        nlohmann::json msg = {{"action", action}, {"success", ok}, {"data", data}};
        if (ok) {
          msg["message"] = messageText;
        } else {
          msg["error"] = messageText;
        }
        sendMessage(msg);
        return;
      }
      sendInvalid();
    }

    void handleSocketCommand(std::string_view command) {
      if (command == kCmdUpdate) {
        sendColors();
        return;
      }
      if (command == kCmdDark) {
        settings::set("theme_mode", "dark");
        sendThemeMode("dark");
        return;
      }
      if (command == kCmdLight) {
        settings::set("theme_mode", "light");
        sendThemeMode("light");
        return;
      }
      if (command == kCmdAuto) {
        settings::set("theme_mode", "auto");
        sendThemeMode("auto");
      }
    }

    [[nodiscard]] int openUnixDatagramServer(const std::filesystem::path& path) {
      ::unlink(path.c_str());
      const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
      if (fd < 0) {
        return -1;
      }
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      const std::string pathStr = path.string();
      if (pathStr.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return -1;
      }
      std::memcpy(addr.sun_path, pathStr.c_str(), pathStr.size() + 1);
      if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
      }
      ::chmod(path.c_str(), 0600);
      (void)setNonBlocking(fd);
      return fd;
    }

    [[nodiscard]] int openColorsInotify(const std::filesystem::path& colorsPath, int* watchFdOut) {
      *watchFdOut = -1;
      const int fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
      if (fd < 0) {
        return -1;
      }
      const auto dir = colorsPath.parent_path();
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      const int wd = ::inotify_add_watch(fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
      if (wd < 0) {
        ::close(fd);
        return -1;
      }
      *watchFdOut = wd;
      return fd;
    }

  } // namespace

  std::filesystem::path colorsJsonPath() {
    const auto cache = xdgCacheHome();
    return cache.empty() ? std::filesystem::path{} : cache / "wal" / "colors.json";
  }

  std::filesystem::path unixSocketPath() {
    return std::filesystem::temp_directory_path() / ("pywalfox_socket_" + std::to_string(::getuid()));
  }

  std::filesystem::path userManifestPath() {
    const auto home = homeDir();
    if (home.empty()) {
      return {};
    }
    return home / ".mozilla" / "native-messaging-hosts" / "pywalfox.json";
  }

  std::filesystem::path resolveHostExecutable() {
    const std::string self = selfExePath();
    if (!self.empty()) {
      const auto selfPath = std::filesystem::path(self);
      if (selfPath.filename() == "noctalia-pywalfox") {
        return selfPath;
      }
      const auto sibling = selfPath.parent_path() / "noctalia-pywalfox";
      std::error_code ec;
      if (std::filesystem::is_regular_file(sibling, ec)) {
        return sibling;
      }
    }

#ifdef NOCTALIA_INSTALL_PREFIX
    const auto installed = std::filesystem::path(NOCTALIA_INSTALL_PREFIX) / "bin" / "noctalia-pywalfox";
    std::error_code ec;
    if (std::filesystem::is_regular_file(installed, ec)) {
      return installed;
    }
#endif
    return {};
  }

  std::filesystem::path cssAssetDir() {
#ifdef NOCTALIA_SOURCE_ASSETS_DIR
    {
      const auto source = std::filesystem::path(NOCTALIA_SOURCE_ASSETS_DIR) / "pywalfox" / "css";
      std::error_code ec;
      if (std::filesystem::is_directory(source, ec)) {
        return source;
      }
    }
#endif
#ifdef NOCTALIA_INSTALL_PREFIX
#ifdef NOCTALIA_INSTALL_DATADIR
    {
      const std::filesystem::path datadir(NOCTALIA_INSTALL_DATADIR);
      const auto installed = datadir.is_absolute()
          ? datadir / "noctalia" / "assets" / "pywalfox" / "css"
          : std::filesystem::path(NOCTALIA_INSTALL_PREFIX) / datadir / "noctalia" / "assets" / "pywalfox" / "css";
      std::error_code ec;
      if (std::filesystem::is_directory(installed, ec)) {
        return installed;
      }
    }
#endif
#endif
    const auto exe = resolveHostExecutable();
    if (!exe.empty()) {
      // Typical layout: prefix/bin/noctalia-pywalfox → prefix/share/noctalia/assets/pywalfox/css
      const auto candidate = exe.parent_path().parent_path() / "share" / "noctalia" / "assets" / "pywalfox" / "css";
      std::error_code ec;
      if (std::filesystem::is_directory(candidate, ec)) {
        return candidate;
      }
    }
    return {};
  }

  bool installManifest(const std::filesystem::path& hostExecutable, std::string* error) {
    if (hostExecutable.empty() || !std::filesystem::is_regular_file(hostExecutable)) {
      if (error != nullptr) {
        *error = "noctalia-pywalfox executable not found";
      }
      return false;
    }

    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(hostExecutable, ec);
    const auto path = ec ? hostExecutable : canonical;

    const auto manifest = userManifestPath();
    if (manifest.empty()) {
      if (error != nullptr) {
        *error = "HOME is not set";
      }
      return false;
    }

    std::filesystem::create_directories(manifest.parent_path(), ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to create native-messaging-hosts directory: " + ec.message();
      }
      return false;
    }

    const nlohmann::json body = {
        {"name", std::string(kManifestName)},
        {"description", "Noctalia Pywalfox native messaging host"},
        {"path", path.string()},
        {"type", "stdio"},
        {"allowed_extensions", nlohmann::json::array({std::string(kExtensionId)})},
    };

    std::ofstream out(manifest, std::ios::trunc);
    if (!out) {
      if (error != nullptr) {
        *error = "failed to write " + manifest.string();
      }
      return false;
    }
    out << body.dump(2) << '\n';
    return true;
  }

  bool uninstallManifest(std::string* error) {
    const auto manifest = userManifestPath();
    if (manifest.empty()) {
      if (error != nullptr) {
        *error = "HOME is not set";
      }
      return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
      return true;
    }
    if (!std::filesystem::remove(manifest, ec)) {
      if (error != nullptr) {
        *error = "failed to remove " + manifest.string() + ": " + ec.message();
      }
      return false;
    }
    return true;
  }

  void ensureManifestForCommunityTemplates(const std::vector<std::string>& communityIds) {
    const bool enabled =
        std::ranges::any_of(communityIds, [](const std::string& id) { return id == kCommunityTemplateId; });
    if (!enabled) {
      return;
    }
    const auto host = resolveHostExecutable();
    std::string err;
    if (!installManifest(host, &err)) {
      std::println(stderr, "noctalia-pywalfox: failed to ensure native messaging host: {}", err);
    }
  }

  int sendSocketCommand(std::string_view command) {
    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      std::println(stderr, "failed to create socket: {}", std::strerror(errno));
      return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const auto path = unixSocketPath().string();
    if (path.size() >= sizeof(addr.sun_path)) {
      ::close(fd);
      std::println(stderr, "socket path too long");
      return 1;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      std::println(stderr, "failed to connect to {}: {}", path, std::strerror(errno));
      ::close(fd);
      return 1;
    }

    const ssize_t sent = ::send(fd, command.data(), command.size(), 0);
    ::close(fd);
    if (sent < 0 || static_cast<std::size_t>(sent) != command.size()) {
      std::println(stderr, "failed to send command");
      return 1;
    }
    return 0;
  }

  int runDaemon() {
    (void)setNonBlocking(STDIN_FILENO);
    (void)setCloexec(STDIN_FILENO);

    const auto socketPath = unixSocketPath();
    const int socketFd = openUnixDatagramServer(socketPath);
    if (socketFd < 0) {
      std::println(stderr, "failed to bind {}: {}", socketPath.string(), std::strerror(errno));
    }

    int inotifyWd = -1;
    const auto colorsPath = colorsJsonPath();
    const int inotifyFd = openColorsInotify(colorsPath, &inotifyWd);
    const std::string colorsFileName = colorsPath.filename().string();

    native_messaging::StdinReader reader;
    bool persistedStateSent = false;

    while (true) {
      std::array<pollfd, 3> fds{};
      nfds_t nfds = 0;
      fds[nfds++] = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
      if (socketFd >= 0) {
        fds[nfds++] = {.fd = socketFd, .events = POLLIN, .revents = 0};
      }
      if (inotifyFd >= 0) {
        fds[nfds++] = {.fd = inotifyFd, .events = POLLIN, .revents = 0};
      }

      const int ready = ::poll(fds.data(), nfds, -1);
      if (ready < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }

      for (nfds_t i = 0; i < nfds; ++i) {
        if (fds[i].revents == 0) {
          continue;
        }
        if (fds[i].fd == STDIN_FILENO) {
          if ((fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (fds[i].revents & POLLIN) == 0) {
            goto shutdown;
          }
          while (true) {
            const auto msg = reader.tryRead();
            if (!msg.has_value()) {
              if (reader.eof()) {
                goto shutdown;
              }
              break;
            }
            handleExtensionMessage(*msg, persistedStateSent);
          }
        } else if (socketFd >= 0 && fds[i].fd == socketFd) {
          char buf[1024];
          while (true) {
            const ssize_t n = ::recv(socketFd, buf, sizeof(buf), 0);
            if (n < 0) {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
              }
              break;
            }
            if (n == 0) {
              break;
            }
            handleSocketCommand(std::string_view(buf, static_cast<std::size_t>(n)));
          }
        } else if (inotifyFd >= 0 && fds[i].fd == inotifyFd) {
          alignas(inotify_event) char buf[4096];
          while (true) {
            const ssize_t n = ::read(inotifyFd, buf, sizeof(buf));
            if (n < 0) {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
              }
              break;
            }
            std::size_t offset = 0;
            bool colorsChanged = false;
            while (offset < static_cast<std::size_t>(n)) {
              const auto* event = reinterpret_cast<const inotify_event*>(buf + offset);
              if (event->len > 0 && colorsFileName == event->name) {
                colorsChanged = true;
              }
              offset += sizeof(inotify_event) + event->len;
            }
            if (colorsChanged) {
              sendColors();
            }
          }
        }
      }
    }

  shutdown:
    if (socketFd >= 0) {
      ::close(socketFd);
      ::unlink(socketPath.c_str());
    }
    if (inotifyFd >= 0) {
      ::close(inotifyFd);
    }
    return 0;
  }

} // namespace pywalfox_host
