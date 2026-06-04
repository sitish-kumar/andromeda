#pragma once

#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace compositors::niri {

  class NiriEventHandler;

  class NiriRuntime {
  public:
    NiriRuntime() = default;

    [[nodiscard]] bool available() const;
    [[nodiscard]] const std::string& socketPath() const;
    [[nodiscard]] std::optional<nlohmann::json> requestJson(std::string_view request) const;
    [[nodiscard]] bool requestOk(std::string_view request, bool acceptNoResponse = false) const;
    [[nodiscard]] bool requestAction(const nlohmann::json& action, bool acceptNoResponse = false) const;
    void refresh();

    // Shared event-stream dispatch. The socket owner (NiriWorkspaceBackend) calls
    // dispatchEvent() for each parsed message; every registered handler sees it.
    void registerEventHandler(NiriEventHandler* handler);
    void unregisterEventHandler(NiriEventHandler* handler);
    void dispatchEvent(std::string_view key, const nlohmann::json& value) const;

  private:
    struct IpcReply;

    [[nodiscard]] IpcReply request(std::string_view request) const;
    void ensureResolved() const;
    void resolveSocketPath() const;

    mutable bool m_resolved = false;
    mutable std::string m_socketPath;
    std::vector<NiriEventHandler*> m_eventHandlers;
  };

} // namespace compositors::niri
