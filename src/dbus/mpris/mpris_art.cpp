#include "dbus/mpris/mpris_art.h"

#include "dbus/mpris/mpris_service.h"
#include "net/uri.h"

#include <format>
#include <string_view>

namespace {

  constexpr std::string_view kGoogleArtSizeSuffix = "=w544-h544-l90-rj";

  std::string extractQueryParam(std::string_view url, std::string_view key) {
    const auto queryPos = url.find('?');
    if (queryPos == std::string_view::npos)
      return {};
    std::string_view query = url.substr(queryPos + 1);
    while (!query.empty()) {
      const auto ampPos = query.find('&');
      const std::string_view pair = query.substr(0, ampPos);
      const auto eqPos = pair.find('=');
      if (pair.substr(0, eqPos) == key)
        return eqPos == std::string_view::npos ? std::string{} : std::string(pair.substr(eqPos + 1));
      if (ampPos == std::string_view::npos)
        break;
      query.remove_prefix(ampPos + 1);
    }
    return {};
  }

  std::string deriveYouTubeThumbnailUrl(std::string_view sourceUrl) {
    if (sourceUrl.empty())
      return {};
    std::string videoId;
    if (sourceUrl.find("youtube.com/watch") != std::string_view::npos
        || sourceUrl.find("music.youtube.com/watch") != std::string_view::npos) {
      videoId = extractQueryParam(sourceUrl, "v");
    } else if (sourceUrl.find("youtu.be/") != std::string_view::npos) {
      const auto marker = sourceUrl.find("youtu.be/");
      const auto start = marker + std::string_view("youtu.be/").size();
      const auto end = sourceUrl.find_first_of("?#&/", start);
      videoId =
          std::string(sourceUrl.substr(start, end == std::string_view::npos ? sourceUrl.size() - start : end - start));
    } else if (sourceUrl.find("youtube.com/shorts/") != std::string_view::npos) {
      const auto marker = sourceUrl.find("youtube.com/shorts/");
      const auto start = marker + std::string_view("youtube.com/shorts/").size();
      const auto end = sourceUrl.find_first_of("?#&/", start);
      videoId =
          std::string(sourceUrl.substr(start, end == std::string_view::npos ? sourceUrl.size() - start : end - start));
    }
    if (videoId.empty())
      return {};
    return std::format("https://i.ytimg.com/vi/{}/hqdefault.jpg", videoId);
  }

  [[nodiscard]] std::string upgradeGoogleArtUrl(std::string_view url) {
    if (url.find("googleusercontent.com") == std::string_view::npos
        && url.find("ggpht.com") == std::string_view::npos) {
      return std::string(url);
    }

    const auto paramStart = url.rfind('=');
    if (paramStart == std::string_view::npos) {
      return std::string(url);
    }

    std::string upgraded(url.substr(0, paramStart));
    upgraded.append(kGoogleArtSizeSuffix);
    return upgraded;
  }

  [[nodiscard]] bool isGoogleMusicArtUrl(std::string_view url) {
    return url.find("googleusercontent.com") != std::string_view::npos
        || url.find("ggpht.com") != std::string_view::npos;
  }

  [[nodiscard]] bool isYouTubeMusicSourceUrl(std::string_view sourceUrl) {
    return sourceUrl.find("music.youtube.com") != std::string_view::npos;
  }

} // namespace

namespace mpris {

  bool isRemoteArtUrl(std::string_view url) { return uri::isRemoteUrl(url); }

  bool shouldCenterSquareCropArt(const MprisPlayerInfo& player, std::string_view effectiveArtUrl) {
    if (isGoogleMusicArtUrl(effectiveArtUrl) || isGoogleMusicArtUrl(player.artUrl)) {
      return true;
    }
    return isYouTubeMusicSourceUrl(player.sourceUrl);
  }

  std::string effectiveArtUrl(const MprisPlayerInfo& player) {
    if (!player.artUrl.empty()) {
      if (isRemoteArtUrl(player.artUrl)) {
        return upgradeGoogleArtUrl(player.artUrl);
      }
      return player.artUrl;
    }
    return deriveYouTubeThumbnailUrl(player.sourceUrl);
  }

  std::string normalizeArtPath(std::string_view artUrl) { return uri::normalizeFileUrl(artUrl); }

  std::filesystem::path artCachePath(std::string_view artUrl) {
    const std::filesystem::path cacheDir = std::filesystem::path("/tmp") / "noctalia-media-art";
    const std::size_t hash = std::hash<std::string_view>{}(artUrl);
    return cacheDir / (std::to_string(hash) + ".img");
  }

  std::string joinArtists(const std::vector<std::string>& artists) {
    if (artists.empty())
      return {};
    std::string joined = artists.front();
    for (std::size_t i = 1; i < artists.size(); ++i) {
      joined += ", ";
      joined += artists[i];
    }
    return joined;
  }

} // namespace mpris
