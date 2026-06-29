#pragma once

#include <string>
#include <string_view>

class AccountsService;
class ConfigService;
struct Config;

namespace shell {

  enum class AvatarApplyError {
    None,
    InvalidPath,
    LoadFailed,
    WriteFailed,
    AccountsFailed,
    ConfigFailed,
  };

  struct AvatarApplyResult {
    AvatarApplyError error = AvatarApplyError::None;

    [[nodiscard]] bool success() const noexcept { return error == AvatarApplyError::None; }
  };

  [[nodiscard]] std::string resolvedAvatarPath(const AccountsService* accounts, const Config& config);

  // AccountsService IconFile is the canonical system avatar; shell.avatar_path is
  // the fallback when AccountsService is unavailable or has no icon set.
  [[nodiscard]] std::string avatarDisplayPath(const AccountsService* accounts, const Config& config);

  // Persists shell.avatar_path and updates AccountsService when it is connected.
  // Large images are center-cropped and downscaled before AccountsService accepts them.
  [[nodiscard]] AvatarApplyResult
  applyAvatarPath(AccountsService* accounts, ConfigService* config, std::string_view path);

  [[nodiscard]] const char* avatarApplyErrorTranslationKey(AvatarApplyError error) noexcept;

} // namespace shell
