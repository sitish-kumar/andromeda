#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <vector>

class SessionBus;

/// Session-bus org.freedesktop.ScreenSaver provider for apps (e.g. Chrome) that inhibit via D-Bus.
class ScreenSaverService {
public:
  using ChangeCallback = std::function<void(std::int64_t inhibitLocks)>;

  explicit ScreenSaverService(SessionBus& bus);
  ~ScreenSaverService();

  ScreenSaverService(const ScreenSaverService&) = delete;
  ScreenSaverService& operator=(const ScreenSaverService&) = delete;

  [[nodiscard]] bool active() const noexcept { return m_active; }
  [[nodiscard]] std::int64_t inhibitLocks() const noexcept { return m_inhibitLocks; }
  void setChangeCallback(ChangeCallback callback);

private:
  struct InhibitCookie {
    std::uint32_t cookie = 0;
    std::string app;
    std::string reason;
    std::string ownerId;
  };

  void registerObjects();
  std::uint32_t onInhibit(std::string app, std::string reason, const char* sender);
  void onUninhibit(std::uint32_t cookie, const char* sender);
  void onInhibitDelta(std::int64_t delta);
  [[nodiscard]] std::size_t unregisterOwnerCookies(const std::string& ownerId);

  SessionBus& m_bus;
  ChangeCallback m_changeCallback;
  std::int64_t m_inhibitLocks = 0;
  std::uint32_t m_nextCookieId = 1337;
  bool m_active = false;
  bool m_nameAcquired = false;
  std::vector<InhibitCookie> m_cookies;
  std::vector<std::unique_ptr<sdbus::IObject>> m_objects;
  std::unique_ptr<sdbus::IProxy> m_dbusProxy;
};
