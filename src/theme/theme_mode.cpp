#include "theme/theme_mode.h"

#include <atomic>

namespace noctalia::theme {

  namespace {
    std::atomic<bool> g_isDarkMode{true};
  } // namespace

  bool isDarkMode() noexcept { return g_isDarkMode.load(std::memory_order_relaxed); }

  void setDarkMode(bool dark) noexcept { g_isDarkMode.store(dark, std::memory_order_relaxed); }

} // namespace noctalia::theme
