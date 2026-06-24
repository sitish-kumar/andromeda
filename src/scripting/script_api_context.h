#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace scripting {

  class ScriptApiContext {
  public:
    [[nodiscard]] bool isDarkMode() const noexcept { return m_darkMode.load(std::memory_order_relaxed); }
    void setDarkMode(bool dark) noexcept { m_darkMode.store(dark, std::memory_order_relaxed); }

    void setWallpaperDirectory(std::string directory) {
      std::scoped_lock lock(m_mutex);
      m_wallpaperDirectory = std::move(directory);
    }

    [[nodiscard]] std::string wallpaperDirectory() const {
      std::scoped_lock lock(m_mutex);
      return m_wallpaperDirectory;
    }

  private:
    std::atomic<bool> m_darkMode{true};
    mutable std::mutex m_mutex;
    std::string m_wallpaperDirectory;
  };

} // namespace scripting
