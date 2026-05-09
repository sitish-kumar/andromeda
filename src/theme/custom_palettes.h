#pragma once

#include "theme/community_palettes.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace noctalia::theme {

  [[nodiscard]] std::filesystem::path customPaletteDir();
  [[nodiscard]] std::filesystem::path customPalettePath(std::string_view name);
  [[nodiscard]] std::vector<AvailablePalette> availableCustomPalettes();

} // namespace noctalia::theme
