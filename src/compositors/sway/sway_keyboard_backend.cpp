#include "compositors/sway/sway_keyboard_backend.h"

#include "core/process.h"
#include "util/string_utils.h"

#include <chrono>
#include <cstdlib>
#include <json.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

  constexpr auto kCurrentLayoutCacheTtl = std::chrono::seconds(1);

  struct CurrentLayoutCache {
    std::optional<std::string> value;
    std::chrono::steady_clock::time_point fetchedAt{};
    bool valid = false;
  };

  CurrentLayoutCache& currentLayoutCache() {
    static CurrentLayoutCache cache;
    return cache;
  }

  void invalidateCurrentLayoutCache() { currentLayoutCache() = CurrentLayoutCache{}; }

  [[nodiscard]] std::optional<std::string> runAndCapture(const std::vector<std::string>& args) {
    if (args.empty() || args.front().empty()) {
      return std::nullopt;
    }

    int pipefd[2];
    if (::pipe(pipefd) != 0) {
      return std::nullopt;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
      ::close(pipefd[0]);
      ::close(pipefd[1]);
      return std::nullopt;
    }

    if (pid == 0) {
      ::close(pipefd[0]);
      ::dup2(pipefd[1], STDOUT_FILENO);
      ::close(pipefd[1]);

      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      ::execvp(argv[0], argv.data());
      ::_exit(127);
    }

    ::close(pipefd[1]);
    std::string output;
    char buffer[4096];
    ssize_t count = 0;
    while ((count = ::read(pipefd[0], buffer, sizeof(buffer))) > 0) {
      output.append(buffer, static_cast<std::size_t>(count));
    }
    ::close(pipefd[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      return std::nullopt;
    }

    return output;
  }

} // namespace

SwayKeyboardBackend::SwayKeyboardBackend(std::string_view compositorHint) {
  const bool hinted = StringUtils::containsInsensitive(compositorHint, "sway");
  const char* swaySock = std::getenv("SWAYSOCK");
  m_enabled = hinted || (swaySock != nullptr && swaySock[0] != '\0');
  m_msgCommand = process::commandExists("swaymsg") ? "swaymsg" : "i3-msg";
}

bool SwayKeyboardBackend::isAvailable() const noexcept { return m_enabled && !m_msgCommand.empty(); }

bool SwayKeyboardBackend::cycleLayout() const {
  if (!isAvailable()) {
    return false;
  }
  const bool ok = process::runSync({m_msgCommand, "input", "type:keyboard", "xkb_switch_layout", "next"});
  if (ok) {
    invalidateCurrentLayoutCache();
  }
  return ok;
}

std::optional<KeyboardLayoutState> SwayKeyboardBackend::layoutState() const {
  const auto current = currentLayoutName();
  if (!current.has_value()) {
    return std::nullopt;
  }
  return KeyboardLayoutState{{*current}, 0};
}

std::optional<std::string> SwayKeyboardBackend::currentLayoutName() const {
  if (!isAvailable()) {
    return std::nullopt;
  }

  const auto now = std::chrono::steady_clock::now();
  auto& cache = currentLayoutCache();
  if (cache.valid && now - cache.fetchedAt < kCurrentLayoutCacheTtl) {
    return cache.value;
  }

  auto finish = [&](std::optional<std::string> value) {
    cache.value = std::move(value);
    cache.fetchedAt = now;
    cache.valid = true;
    return cache.value;
  };

  const auto payload = runAndCapture({m_msgCommand, "-t", "get_inputs", "--raw"});
  if (!payload.has_value() || payload->empty()) {
    return finish(std::nullopt);
  }

  try {
    const auto json = nlohmann::json::parse(*payload);
    if (!json.is_array()) {
      return finish(std::nullopt);
    }
    for (const auto& input : json) {
      if (!input.is_object()) {
        continue;
      }
      if (input.value("type", "") != "keyboard") {
        continue;
      }
      const std::string layout = input.value("xkb_active_layout_name", "");
      if (!layout.empty()) {
        return finish(layout);
      }
    }
  } catch (const nlohmann::json::exception&) {
    return finish(std::nullopt);
  }

  return finish(std::nullopt);
}
