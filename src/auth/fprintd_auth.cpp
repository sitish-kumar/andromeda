#include "auth/fprintd_auth.h"

#include "core/log.h"
#include "core/process.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sdbus-c++/sdbus-c++.h>
#include <string>

namespace {

  constexpr Logger kLog("fprintd");

  constexpr auto kFprintBus = "net.reactivated.Fprint";
  constexpr auto kFprintManagerPath = "/net/reactivated/Fprint/Manager";
  constexpr auto kFprintManagerInterface = "net.reactivated.Fprint.Manager";
  constexpr auto kFprintDeviceInterface = "net.reactivated.Fprint.Device";

  std::atomic<bool> g_cancelRequested{false};

  sdbus::ObjectPath defaultDevicePath(sdbus::IConnection& connection) {
    auto manager =
        sdbus::createProxy(connection, sdbus::ServiceName{kFprintBus}, sdbus::ObjectPath{kFprintManagerPath});
    sdbus::ObjectPath path;
    manager->callMethod("GetDefaultDevice").onInterface(kFprintManagerInterface).storeResultsTo(path);
    return path;
  }

  void releaseDevice(sdbus::IProxy& device) {
    try {
      device.callMethod("Release").onInterface(kFprintDeviceInterface);
    } catch (const sdbus::Error& error) {
      kLog.debug("Release failed: {}", error.getMessage());
    }
  }

  void stopDeviceVerification(sdbus::IProxy& device) {
    try {
      device.callMethod("VerifyStop").onInterface(kFprintDeviceInterface);
      return;
    } catch (const sdbus::Error&) {
    }
    try {
      device.callMethod("Cancel").onInterface(kFprintDeviceInterface);
    } catch (const sdbus::Error& error) {
      kLog.debug("Cancel failed: {}", error.getMessage());
    }
  }

} // namespace

namespace fprintd {

  bool isAvailable() {
    static const bool available = process::commandExists("fprintd-verify");
    return available;
  }

  void stopVerification() {
    g_cancelRequested.store(true, std::memory_order_release);
    try {
      auto connection = sdbus::createSystemBusConnection();
      const auto path = defaultDevicePath(*connection);
      auto device = sdbus::createProxy(*connection, sdbus::ServiceName{kFprintBus}, path);
      stopDeviceVerification(*device);
    } catch (const sdbus::Error& error) {
      kLog.debug("stopVerification: {}", error.getMessage());
    }
  }

  bool verifyUser(std::string_view username) {
    g_cancelRequested.store(false, std::memory_order_release);

    try {
      auto connection = sdbus::createSystemBusConnection();
      const auto path = defaultDevicePath(*connection);
      auto device = sdbus::createProxy(*connection, sdbus::ServiceName{kFprintBus}, path);

      std::mutex mutex;
      std::condition_variable cv;
      bool completed = false;
      bool success = false;

      device->uponSignal("VerifyStatus")
          .onInterface(kFprintDeviceInterface)
          .call([&](const std::string& status, const bool done) {
            if (!done) {
              return;
            }
            std::lock_guard lock(mutex);
            success = status == "verify-match";
            completed = true;
            cv.notify_one();
          });

      const std::string user(username);
      device->callMethod("Claim").onInterface(kFprintDeviceInterface).withArguments(user);

      bool verifyStarted = false;
      try {
        device->callMethod("VerifyStart").onInterface(kFprintDeviceInterface).withArguments(std::string{"any"});
        verifyStarted = true;
      } catch (const sdbus::Error&) {
        device->callMethod("Verify").onInterface(kFprintDeviceInterface).withArguments(user);
        verifyStarted = true;
      }

      if (!verifyStarted) {
        releaseDevice(*device);
        return false;
      }

      const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes{2};
      while (!completed && std::chrono::steady_clock::now() < deadline) {
        connection->processPendingEvent();

        if (g_cancelRequested.load(std::memory_order_acquire)) {
          stopDeviceVerification(*device);
        }

        std::unique_lock lock(mutex);
        cv.wait_for(lock, std::chrono::milliseconds{50}, [&] { return completed; });
      }

      releaseDevice(*device);
      return success;
    } catch (const sdbus::Error& error) {
      kLog.debug("verifyUser failed: {}", error.getMessage());
      return false;
    }
  }

} // namespace fprintd
