#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class SystemBus;

namespace sdbus {
  class IProxy;
} // namespace sdbus

enum class BatteryState : std::uint8_t {
  Unknown = 0,
  Charging = 1,
  Discharging = 2,
  Empty = 3,
  FullyCharged = 4,
  PendingCharge = 5,
  PendingDischarge = 6,
};

[[nodiscard]] std::string batteryStateLabel(BatteryState state);

struct UPowerState {
  double percentage = 0.0;
  BatteryState state = BatteryState::Unknown;
  std::int64_t timeToEmpty = 0; // seconds
  std::int64_t timeToFull = 0;  // seconds
  bool isPresent = false;
  bool onBattery = false;

  bool operator==(const UPowerState&) const = default;
};

struct UPowerDeviceInfo {
  std::string path;
  std::string nativePath;
  std::string vendor;
  std::string model;
  std::string serial;
  std::uint32_t type = 0;
  bool powerSupply = false;
  bool isPresent = false;
  UPowerState state;

  bool operator==(const UPowerDeviceInfo&) const = default;
};

class UPowerService {
public:
  using ChangeCallback = std::function<void()>;

  explicit UPowerService(SystemBus& bus);

  void setChangeCallback(ChangeCallback callback);
  void refresh();

  [[nodiscard]] const UPowerState& state() const noexcept { return m_state; }
  [[nodiscard]] UPowerState stateForDevice(std::string_view selector) const;
  [[nodiscard]] std::vector<UPowerDeviceInfo> batteryDevices() const;

private:
  struct TrackedDevice {
    UPowerDeviceInfo info;
    std::unique_ptr<sdbus::IProxy> proxy;
  };

  [[nodiscard]] UPowerState readDefaultState() const;
  [[nodiscard]] UPowerState readDeviceState(sdbus::IProxy& proxy) const;
  [[nodiscard]] UPowerDeviceInfo readDeviceInfo(std::string path, sdbus::IProxy& proxy) const;
  [[nodiscard]] const UPowerDeviceInfo* defaultSystemBattery() const noexcept;
  [[nodiscard]] const UPowerDeviceInfo* findDevice(std::string_view selector) const;
  void refreshDisplayDeviceProxy();
  void emitChangedIfNeeded(bool devicesChanged);
  void rescanDevices();
  void refreshDeviceStates();

  SystemBus& m_bus;
  std::unique_ptr<sdbus::IProxy> m_upowerProxy;
  std::unique_ptr<sdbus::IProxy> m_displayDeviceProxy;
  std::string m_displayDevicePath;
  std::vector<TrackedDevice> m_devices;
  UPowerState m_state;
  ChangeCallback m_changeCallback;
};
