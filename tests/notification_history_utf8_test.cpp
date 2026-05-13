#include "notification/notification_history_store.h"
#include "notification/notification_manager.h"
#include "util/string_utils.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>

namespace {

  Notification makeNotification(std::string body) {
    const auto now = Clock::now();
    const auto wallNow = WallClock::now();
    return Notification{
        .id = 314,
        .origin = NotificationOrigin::External,
        .appName = "test",
        .summary = "summary",
        .body = std::move(body),
        .timeout = 6000,
        .urgency = Urgency::Normal,
        .actions = {},
        .icon = std::nullopt,
        .imageData = std::nullopt,
        .category = std::nullopt,
        .desktopEntry = std::nullopt,
        .receivedTime = now,
        .expiryTime = std::nullopt,
        .receivedWallClock = wallNow,
        .expiryWallClock = std::nullopt,
    };
  }

  std::filesystem::path testPath() {
    return std::filesystem::temp_directory_path() /
           ("noctalia-notification-history-utf8-" + std::to_string(static_cast<long long>(getpid())) + ".json");
  }

} // namespace

int main() {
  std::string splitBoundary(1023, 'a');
  splitBoundary.push_back(static_cast<char>(0xD1));
  splitBoundary.push_back(static_cast<char>(0x80));

  const std::string truncated = StringUtils::truncateUtf8(splitBoundary, 1024);
  if (truncated.size() != 1023) {
    std::cerr << "truncateUtf8 kept a partial code point at the byte limit\n";
    return 1;
  }

  std::string exactBoundary(1022, 'a');
  exactBoundary.push_back(static_cast<char>(0xD1));
  exactBoundary.push_back(static_cast<char>(0x80));

  const std::string exact = StringUtils::truncateUtf8(exactBoundary, 1024);
  if (exact.size() != 1024) {
    std::cerr << "truncateUtf8 removed a complete code point at the byte limit\n";
    return 1;
  }

  const auto path = testPath();
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".tmp");

  std::string body = "prefix ";
  body.push_back(static_cast<char>(0xD1));

  std::deque<NotificationHistoryEntry> entries;
  entries.push_back(NotificationHistoryEntry{
      .notification = makeNotification(std::move(body)),
      .active = true,
      .closeReason = std::nullopt,
      .eventSerial = 1,
  });

  try {
    if (!saveNotificationHistoryToFile(path, entries, 315, 2)) {
      std::cerr << "saveNotificationHistoryToFile returned false\n";
      return 1;
    }
  } catch (const std::exception& e) {
    std::cerr << "saveNotificationHistoryToFile threw: " << e.what() << '\n';
    return 1;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in.good()) {
    std::cerr << "history file was not written\n";
    return 1;
  }

  std::filesystem::remove(path);
  return 0;
}
