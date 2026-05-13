#include "core/process.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace {

  void writePipeOrIgnore(int fd, const void* data, size_t len) {
    auto p = reinterpret_cast<const char*>(data);
    size_t remaining = len;
    while (remaining > 0) {
      const ssize_t n = ::write(fd, p, remaining);
      if (n > 0) {
        p += static_cast<size_t>(n);
        remaining -= static_cast<size_t>(n);
      } else if (n == 0) {
        return;
      } else if (errno != EINTR) {
        return;
      }
    }
  }

  void attachStdioToDevNull() {
    const int devnull = ::open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      ::dup2(devnull, STDIN_FILENO);
      ::dup2(devnull, STDOUT_FILENO);
      ::dup2(devnull, STDERR_FILENO);
      if (devnull > STDERR_FILENO) {
        ::close(devnull);
      }
    }
  }

  // Double-fork + setsid so the exec'd process is not a direct child of the caller (matches
  // launcher app activation). Parent reaps the short-lived intermediate child.
  bool doubleForkExecDetached(const std::vector<std::string>& args, pid_t* reportPid,
                              const std::string& activationToken) {
    int reportPipe[2] = {-1, -1};
    const bool needPid = reportPid != nullptr;
    if (needPid && ::pipe(reportPipe) != 0) {
      return false;
    }

    const pid_t intermediate = ::fork();
    if (intermediate < 0) {
      if (needPid) {
        ::close(reportPipe[0]);
        ::close(reportPipe[1]);
      }
      return false;
    }

    if (intermediate > 0) {
      // Parent: read grandchild pid first (intermediate may exit before the grandchild writes).
      if (needPid) {
        ::close(reportPipe[1]);
        pid_t reported = -1;
        const auto n = ::read(reportPipe[0], &reported, sizeof(reported));
        ::close(reportPipe[0]);
        int status = 0;
        while (::waitpid(intermediate, &status, 0) < 0 && errno == EINTR) {
        }
        const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0 && n == sizeof(reported) && reported > 0;
        if (ok) {
          *reportPid = reported;
        }
        return ok;
      }

      int status = 0;
      while (::waitpid(intermediate, &status, 0) < 0 && errno == EINTR) {
      }
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    // Intermediate child: new session, then fork again so the grandchild reparents away.
    if (needPid) {
      ::close(reportPipe[0]);
    }

    if (::setsid() < 0) {
      if (needPid) {
        const pid_t err = -1;
        writePipeOrIgnore(reportPipe[1], &err, sizeof(err));
        ::close(reportPipe[1]);
      }
      ::_exit(1);
    }

    const pid_t worker = ::fork();
    if (worker < 0) {
      if (needPid) {
        const pid_t err = -1;
        writePipeOrIgnore(reportPipe[1], &err, sizeof(err));
        ::close(reportPipe[1]);
      }
      ::_exit(1);
    }
    if (worker > 0) {
      if (needPid) {
        ::close(reportPipe[1]);
      }
      ::_exit(0);
    }

    // Grandchild
    if (needPid) {
      const pid_t self = ::getpid();
      writePipeOrIgnore(reportPipe[1], &self, sizeof(self));
      ::close(reportPipe[1]);
    }

    if (!activationToken.empty()) {
      ::setenv("XDG_ACTIVATION_TOKEN", activationToken.c_str(), 1);
      ::setenv("DESKTOP_STARTUP_ID", activationToken.c_str(), 1);
    }

    attachStdioToDevNull();

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    ::execvp(argv[0], argv.data());
    ::_exit(127);
  }

} // namespace

namespace process {

  bool commandExists(const char* name) {
    if (name == nullptr || name[0] == '\0') {
      return false;
    }

    if (std::strchr(name, '/') != nullptr) {
      return ::access(name, X_OK) == 0;
    }

    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr || pathEnv[0] == '\0') {
      pathEnv = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    }

    std::string_view path(pathEnv);
    std::size_t start = 0;
    while (start <= path.size()) {
      const std::size_t end = path.find(':', start);
      const std::string_view dir = end == std::string_view::npos ? path.substr(start) : path.substr(start, end - start);
      const std::filesystem::path candidate =
          dir.empty() ? std::filesystem::path(name) : (std::filesystem::path(dir) / name);
      if (::access(candidate.c_str(), X_OK) == 0) {
        return true;
      }
      if (end == std::string_view::npos) {
        break;
      }
      start = end + 1;
    }

    return false;
  }

  bool runAsync(const std::vector<std::string>& args, const std::string& activationToken) {
    if (args.empty() || args.front().empty()) {
      return false;
    }
    return doubleForkExecDetached(args, nullptr, activationToken);
  }

  bool runAsync(std::initializer_list<const char*> args) {
    std::vector<std::string> command;
    command.reserve(args.size());
    for (const char* arg : args) {
      if (arg == nullptr) {
        return false;
      }
      command.emplace_back(arg);
    }
    return runAsync(command, {});
  }

  bool runAsync(const std::string& command) {
    if (command.empty()) {
      return false;
    }
    return runAsync(std::vector<std::string>{"/bin/sh", "-lc", command}, {});
  }

  std::optional<int> launchDetachedTracked(const std::vector<std::string>& args) {
    if (args.empty() || args.front().empty()) {
      return std::nullopt;
    }
    pid_t reported = -1;
    if (!doubleForkExecDetached(args, &reported, {})) {
      return std::nullopt;
    }
    return static_cast<int>(reported);
  }

  std::optional<int> launchDetachedTracked(std::initializer_list<const char*> args) {
    std::vector<std::string> command;
    command.reserve(args.size());
    for (const char* arg : args) {
      if (arg == nullptr) {
        return std::nullopt;
      }
      command.emplace_back(arg);
    }
    return launchDetachedTracked(command);
  }

  void terminateTracked(int pid) {
    if (pid <= 0) {
      return;
    }
    const pid_t p = static_cast<pid_t>(pid);
    ::kill(p, SIGTERM);
    int status = 0;
    if (::waitpid(p, &status, WNOHANG) != p) {
      ::kill(p, SIGKILL);
      ::waitpid(p, &status, 0);
    }
  }

  RunResult runSync(const std::vector<std::string>& args) {
    if (args.empty() || args.front().empty())
      return {-1, {}, {}};

    int outPipe[2]{};
    int errPipe[2]{};
    if (::pipe(outPipe) != 0 || ::pipe(errPipe) != 0)
      return {-1, {}, {}};

    const pid_t pid = ::fork();
    if (pid < 0) {
      ::close(outPipe[0]);
      ::close(outPipe[1]);
      ::close(errPipe[0]);
      ::close(errPipe[1]);
      return {-1, {}, {}};
    }

    if (pid == 0) {
      ::close(outPipe[0]);
      ::close(errPipe[0]);
      ::dup2(outPipe[1], STDOUT_FILENO);
      ::dup2(errPipe[1], STDERR_FILENO);
      ::close(outPipe[1]);
      ::close(errPipe[1]);

      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (const auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
      argv.push_back(nullptr);

      ::execvp(argv[0], argv.data());
      ::_exit(127);
    }

    ::close(outPipe[1]);
    ::close(errPipe[1]);

    auto drain = [](int fd) {
      std::string buf;
      char tmp[4096];
      for (;;) {
        const auto n = ::read(fd, tmp, sizeof(tmp));
        if (n <= 0)
          break;
        buf.append(tmp, static_cast<size_t>(n));
      }
      ::close(fd);
      while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r'))
        buf.pop_back();
      return buf;
    };

    std::string out = drain(outPipe[0]);
    std::string err = drain(errPipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return {exitCode, std::move(out), std::move(err)};
  }

  RunResult runSync(std::initializer_list<const char*> args) {
    std::vector<std::string> command;
    command.reserve(args.size());
    for (const char* arg : args) {
      if (arg == nullptr) {
        return {-1, {}, {}};
      }
      command.emplace_back(arg);
    }
    return runSync(command);
  }

  RunResult runSync(const std::string& command) {
    if (command.empty())
      return {-1, {}, {}};
    return runSync(std::vector<std::string>{"/bin/sh", "-lc", command});
  }

  bool launchFirstAvailable(std::initializer_list<std::initializer_list<const char*>> commandVariants) {
    for (const auto& variant : commandVariants) {
      if (variant.size() == 0) {
        continue;
      }
      const char* executable = *variant.begin();
      if (!commandExists(executable)) {
        continue;
      }
      if (runAsync(variant)) {
        return true;
      }
    }
    return false;
  }

} // namespace process
