#pragma once

#include <string_view>

namespace fprintd {

  [[nodiscard]] bool isAvailable();

  /// Stops any in-progress fprintd verification (safe from any thread).
  void stopVerification();

  /// Blocks until verification completes, is cancelled, or times out. Run off the main thread.
  [[nodiscard]] bool verifyUser(std::string_view username);

} // namespace fprintd
