#include "system/v4l2_monitor.h"

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/inotify.h>
#include <unistd.h>

namespace fs = std::filesystem;

V4l2Monitor::V4l2Monitor() {
  m_inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (m_inotifyFd < 0) {
    return;
  }

  m_devWatchWd = inotify_add_watch(m_inotifyFd, "/dev", IN_CREATE | IN_DELETE);

  // Watch existing /dev/video* devices
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator("/dev", ec)) {
    if (ec)
      break;
    std::string name = entry.path().filename().string();
    if (name.starts_with("video")) {
      addWatch(entry.path().string());
    }
  }

  // Initial scan
  rescan();
}

V4l2Monitor::~V4l2Monitor() {
  if (m_inotifyFd >= 0) {
    close(m_inotifyFd);
  }
}

void V4l2Monitor::doAddPollFds(std::vector<pollfd>& fds) {
  if (m_inotifyFd >= 0) {
    fds.push_back({.fd = m_inotifyFd, .events = POLLIN, .revents = 0});
  }
}

void V4l2Monitor::addWatch(const std::string& path) {
  if (m_inotifyFd < 0)
    return;
  int wd = inotify_add_watch(m_inotifyFd, path.c_str(), IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE);
  if (wd >= 0) {
    m_watches[wd] = path;
  }
}

void V4l2Monitor::removeWatch(int wd) {
  if (m_inotifyFd < 0 || wd < 0)
    return;
  inotify_rm_watch(m_inotifyFd, wd);
  m_watches.erase(wd);
}

void V4l2Monitor::dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) {
  if (m_inotifyFd < 0 || startIdx >= fds.size())
    return;

  if (!(fds[startIdx].revents & POLLIN)) {
    return;
  }

  bool needsRescan = false;
  char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

  while (true) {
    ssize_t len = read(m_inotifyFd, buf, sizeof(buf));
    if (len <= 0)
      break;

    const char* ptr = buf;
    while (ptr < buf + len) {
      const auto* event = reinterpret_cast<const struct inotify_event*>(ptr);

      if (event->wd == m_devWatchWd) {
        if (event->len > 0) {
          std::string name(event->name);
          if (name.starts_with("video")) {
            if (event->mask & IN_CREATE) {
              addWatch("/dev/" + name);
            }
          }
        }
      } else {
        // A video device was opened or closed
        if (event->mask & (IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)) {
          needsRescan = true;
        }
        if (event->mask & IN_IGNORED) {
          m_watches.erase(event->wd);
          needsRescan = true;
        }
      }

      ptr += sizeof(struct inotify_event) + event->len;
    }
  }

  if (needsRescan) {
    rescan();
  }
}

void V4l2Monitor::rescan() {
  PrivacyState nextState;
  std::error_code ec;

  for (const auto& procEntry : fs::directory_iterator("/proc", ec)) {
    if (ec)
      break;
    if (!procEntry.is_directory())
      continue;

    std::string pidStr = procEntry.path().filename().string();
    if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit))
      continue;

    bool hasVideoFd = false;
    for (const auto& fdEntry : fs::directory_iterator(procEntry.path() / "fd", ec)) {
      if (ec) {
        ec.clear();
        break; // Process might have terminated or permission denied
      }
      std::string target = fs::read_symlink(fdEntry.path(), ec).string();
      if (!ec && target.starts_with("/dev/video")) {
        hasVideoFd = true;
        break;
      }
      ec.clear();
    }

    if (hasVideoFd) {
      std::string comm;
      std::ifstream commFile(procEntry.path() / "comm");
      if (std::getline(commFile, comm)) {
        nextState.captures.push_back(
            {.kind = PrivacyCaptureKind::Camera, .id = static_cast<std::uint32_t>(std::stoi(pidStr)), .appName = comm}
        );
      }
    }
  }

  if (m_state != nextState) {
    m_state = std::move(nextState);
    if (m_changeCallback) {
      m_changeCallback();
    }
  }
}
