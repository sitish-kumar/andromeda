#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pywalfox_host {

  inline constexpr std::string_view kCommunityTemplateId = "pywalfox";
  inline constexpr std::string_view kExtensionId = "pywalfox@frewacom.org";
  inline constexpr std::string_view kDaemonVersion = "noctalia-2.9.0-compat";
  inline constexpr std::string_view kManifestName = "pywalfox";

  [[nodiscard]] std::filesystem::path colorsJsonPath();
  [[nodiscard]] std::filesystem::path unixSocketPath();
  [[nodiscard]] std::filesystem::path userManifestPath();
  [[nodiscard]] std::filesystem::path resolveHostExecutable();
  [[nodiscard]] std::filesystem::path cssAssetDir();

  // Write ~/.mozilla/native-messaging-hosts/pywalfox.json pointing at hostExecutable.
  bool installManifest(const std::filesystem::path& hostExecutable, std::string* error = nullptr);
  bool uninstallManifest(std::string* error = nullptr);

  // If communityIds contains "pywalfox", ensure the user-local native-messaging manifest.
  void ensureManifestForCommunityTemplates(const std::vector<std::string>& communityIds);

  int runDaemon();
  int sendSocketCommand(std::string_view command);

} // namespace pywalfox_host
