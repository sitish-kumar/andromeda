#include "dbus/idle/screensaver_service.h"

#include "core/log.h"
#include "dbus/session_bus.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

  constexpr Logger kLog("screensaver");

  const sdbus::ServiceName kBusName{"org.freedesktop.ScreenSaver"};
  constexpr auto kInterface = "org.freedesktop.ScreenSaver";
  constexpr auto kFileExistsError = "org.freedesktop.DBus.Error.FileExists";

  const sdbus::ObjectPath kObjectPaths[] = {
      sdbus::ObjectPath{"/org/freedesktop/ScreenSaver"},
      sdbus::ObjectPath{"/ScreenSaver"},
  };

  void requestScreenSaverBusName(sdbus::IConnection& connection) {
    try {
      connection.requestName(kBusName);
    } catch (const sdbus::Error& e) {
      if (e.getName() == kFileExistsError) {
        throw std::runtime_error("org.freedesktop.ScreenSaver is already owned by another service");
      }
      throw;
    }
  }

} // namespace

ScreenSaverService::ScreenSaverService(SessionBus& bus) : m_bus(bus) { registerObjects(); }

ScreenSaverService::~ScreenSaverService() {
  if (m_nameAcquired) {
    try {
      m_bus.connection().releaseName(kBusName);
    } catch (const sdbus::Error& e) {
      kLog.debug("screensaver bus name release failed: {}", e.what());
    }
    m_nameAcquired = false;
  }
  m_objects.clear();
}

void ScreenSaverService::setChangeCallback(ChangeCallback callback) { m_changeCallback = std::move(callback); }

void ScreenSaverService::registerObjects() {
  requestScreenSaverBusName(m_bus.connection());
  m_nameAcquired = true;

  m_dbusProxy = sdbus::createProxy(
      m_bus.connection(), sdbus::ServiceName{"org.freedesktop.DBus"}, sdbus::ObjectPath{"/org/freedesktop/DBus"}
  );
  m_dbusProxy->uponSignal("NameOwnerChanged")
      .onInterface("org.freedesktop.DBus")
      .call([this](const std::string& /*name*/, const std::string& oldOwner, const std::string& newOwner) {
        if (!newOwner.empty()) {
          return;
        }
        const std::size_t removed = unregisterOwnerCookies(oldOwner);
        if (removed > 0) {
          kLog.debug("screensaver: client disconnected, cleared {} inhibit(s)", removed);
        }
      });

  for (const auto& path : kObjectPaths) {
    auto object = sdbus::createObject(m_bus.connection(), path);
    object
        ->addVTable(
            sdbus::registerMethod("Inhibit")
                .withInputParamNames("application_name", "reason_for_inhibit")
                .withOutputParamNames("cookie")
                .implementedAs([this, objectPtr = object.get()](std::string app, std::string reason) {
                  return onInhibit(
                      std::move(app), std::move(reason), objectPtr->getCurrentlyProcessedMessage().getSender()
                  );
                }),
            sdbus::registerMethod("UnInhibit")
                .withInputParamNames("cookie")
                .implementedAs([this, objectPtr = object.get()](std::uint32_t cookie) {
                  onUninhibit(cookie, objectPtr->getCurrentlyProcessedMessage().getSender());
                })
        )
        .forInterface(kInterface);
    m_objects.push_back(std::move(object));
  }

  m_active = true;
  kLog.info("listening on org.freedesktop.ScreenSaver");
}

std::uint32_t ScreenSaverService::onInhibit(std::string app, std::string reason, const char* sender) {
  kLog.debug("screensaver inhibit from {} ({}): {}", app, sender != nullptr ? sender : "?", reason);
  onInhibitDelta(1);

  const auto cookie = m_nextCookieId++;
  m_cookies.push_back(
      InhibitCookie{
          .cookie = cookie,
          .app = std::move(app),
          .reason = std::move(reason),
          .ownerId = sender != nullptr ? std::string(sender) : std::string{},
      }
  );
  kLog.debug("screensaver cookie {} issued (locks={})", cookie, m_inhibitLocks);
  return cookie;
}

void ScreenSaverService::onUninhibit(std::uint32_t cookie, const char* sender) {
  const auto it =
      std::ranges::find_if(m_cookies, [cookie](const InhibitCookie& entry) { return entry.cookie == cookie; });
  if (it == m_cookies.end()) {
    kLog.warn("screensaver uninhibit: unknown cookie {} from {}", cookie, sender != nullptr ? sender : "?");
    return;
  }

  kLog.debug("screensaver uninhibit from {} ({}): {}", it->app, sender != nullptr ? sender : "?", it->reason);
  m_cookies.erase(it);
  onInhibitDelta(-1);
}

void ScreenSaverService::onInhibitDelta(std::int64_t delta) {
  m_inhibitLocks = std::max<std::int64_t>(0, m_inhibitLocks + delta);
  if (m_changeCallback) {
    m_changeCallback(m_inhibitLocks);
  }
}

std::size_t ScreenSaverService::unregisterOwnerCookies(const std::string& ownerId) {
  if (ownerId.empty()) {
    return 0;
  }

  std::size_t removed = 0;
  for (auto it = m_cookies.begin(); it != m_cookies.end();) {
    if (it->ownerId != ownerId) {
      ++it;
      continue;
    }
    ++removed;
    it = m_cookies.erase(it);
    onInhibitDelta(-1);
  }
  return removed;
}
