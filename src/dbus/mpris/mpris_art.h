#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct MprisPlayerInfo;

namespace mpris {

  [[nodiscard]] bool isRemoteArtUrl(std::string_view url);
  [[nodiscard]] std::string effectiveArtUrl(const MprisPlayerInfo& player);
  // Strip baked-in letterboxing from YouTube Music album art (Google CDN / music.youtube.com).
  [[nodiscard]] bool shouldCenterSquareCropArt(const MprisPlayerInfo& player, std::string_view effectiveArtUrl);
  [[nodiscard]] std::string normalizeArtPath(std::string_view artUrl);
  [[nodiscard]] std::filesystem::path artCachePath(std::string_view artUrl);
  [[nodiscard]] std::string joinArtists(const std::vector<std::string>& artists);

} // namespace mpris
