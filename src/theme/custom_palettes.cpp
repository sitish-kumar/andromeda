#include "theme/custom_palettes.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace noctalia::theme {

  std::filesystem::path customPaletteDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
      return std::filesystem::path(xdg) / "noctalia" / "palettes";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      return std::filesystem::path(home) / ".config" / "noctalia" / "palettes";
    }
    return {};
  }

  std::filesystem::path customPalettePath(std::string_view name) {
    return customPaletteDir() / (std::string(name) + ".json");
  }

  std::vector<AvailablePalette> availableCustomPalettes() {
    const auto dir = customPaletteDir();
    if (dir.empty()) {
      return {};
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) {
      return {};
    }
    std::vector<AvailablePalette> out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec || !entry.is_regular_file(ec) || ec) {
        continue;
      }
      const auto& path = entry.path();
      if (path.extension() != ".json") {
        continue;
      }
      out.push_back(AvailablePalette{path.stem().string()});
    }
    std::sort(out.begin(), out.end(),
              [](const AvailablePalette& a, const AvailablePalette& b) { return a.name < b.name; });
    return out;
  }

} // namespace noctalia::theme
