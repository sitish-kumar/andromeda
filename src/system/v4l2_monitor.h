#pragma once

#include "app/poll_source.h"
#include "system/privacy_types.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class V4l2Monitor final : public PollSource {
public:
  V4l2Monitor();
  ~V4l2Monitor() override;

  using ChangeCallback = std::function<void()>;
  void setChangeCallback(ChangeCallback callback) { m_changeCallback = std::move(callback); }

  [[nodiscard]] const PrivacyState& privacyState() const noexcept { return m_state; }

  void dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) override;

protected:
  void doAddPollFds(std::vector<pollfd>& fds) override;

private:
  void rescan();
  void addWatch(const std::string& path);
  void removeWatch(int wd);

  int m_inotifyFd = -1;
  int m_devWatchWd = -1;
  std::unordered_map<int, std::string> m_watches;
  PrivacyState m_state;
  ChangeCallback m_changeCallback;
};
