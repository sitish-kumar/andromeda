#pragma once

namespace noctalia::theme {

  // Resolved dark/light theme mode, published by ThemeService whenever it
  // resolves the active theme. Lives apart from ThemeService so consumers
  // without an instance handle (e.g. scripted widgets running on worker
  // threads) can query it safely. Reads/writes are atomic.
  [[nodiscard]] bool isDarkMode() noexcept;
  void setDarkMode(bool dark) noexcept;

} // namespace noctalia::theme
