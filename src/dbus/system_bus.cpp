#include "dbus/system_bus.h"

#include <string>

namespace {
  constexpr auto kDbusInterface = "org.freedesktop.DBus";
  const sdbus::ServiceName kDbusName{kDbusInterface};
  const sdbus::ObjectPath kDbusPath{"/org/freedesktop/DBus"};
} // namespace

SystemBus::SystemBus() : m_connection(sdbus::createSystemBusConnection()) {}

sdbus::IConnection::PollData SystemBus::getPollData() const { return m_connection->getEventLoopPollData(); }

bool SystemBus::nameHasOwner(std::string_view name) const {
  auto proxy = sdbus::createProxy(*m_connection, kDbusName, kDbusPath);
  bool hasOwner = false;
  proxy->callMethod("NameHasOwner")
      .onInterface(kDbusInterface)
      .withArguments(std::string{name})
      .storeResultsTo(hasOwner);
  return hasOwner;
}

void SystemBus::processPendingEvents() {
  while (m_connection->processPendingEvent()) {
  }
}
