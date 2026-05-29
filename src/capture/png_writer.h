#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace capture {

  [[nodiscard]] bool writePng(
      const std::filesystem::path& path, const std::uint8_t* rgba, int width, int height,
      std::string* errorOut = nullptr
  );

  [[nodiscard]] std::vector<std::uint8_t>
  encodePng(const std::uint8_t* rgba, int width, int height, std::string* errorOut = nullptr);

} // namespace capture
