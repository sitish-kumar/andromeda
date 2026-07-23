#pragma once

#include "config/config_types.h"
#include "security/secret_store.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace security {

  enum class StorageKeyState : std::uint8_t {
    Opening,
    Ready,
    Unavailable,
    Cancelled,
    DeniedOrLocked,
    MissingKey,
    BackendError,
  };

  enum class StorageKeyPurpose : std::uint8_t {
    ClipboardHistory,
    CalendarEvents,
  };

  class StorageKeyProvider {
  public:
    using ChangeCallback = std::function<void()>;

    explicit StorageKeyProvider(SecretStore& secretStore);
    ~StorageKeyProvider();

    StorageKeyProvider(const StorageKeyProvider&) = delete;
    StorageKeyProvider& operator=(const StorageKeyProvider&) = delete;

    void configure(StorageKeySource source, std::string keyFile, bool encryptedDataExists);
    void retry();
    void noteEncryptedDataExists() noexcept { m_encryptedDataExists = true; }
    void setChangeCallback(ChangeCallback callback);

    [[nodiscard]] StorageKeyState state() const noexcept { return m_state; }
    [[nodiscard]] std::optional<SecureKey> deriveKey(StorageKeyPurpose purpose) const;

  private:
    void loadKeyFile();
    void lookupKey();
    void createKey();
    void activateKey(SecureKey key);
    void setState(StorageKeyState state, bool forceNotify = false);
    [[nodiscard]] static StorageKeyState stateForSecretStoreStatus(SecretStoreStatus status);

    SecretStore& m_secretStore;
    SecretStoreOperation m_operation;
    std::optional<SecureKey> m_masterKey;
    StorageKeySource m_source = StorageKeySource::SecretService;
    std::string m_keyFile;
    bool m_encryptedDataExists = false;
    bool m_configured = false;
    StorageKeyState m_state = StorageKeyState::Opening;
    ChangeCallback m_changeCallback;
  };

} // namespace security
