#pragma once

#include "compositors/workspace_backend.h"

#include <cstdint>
#include <functional>
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace compositors::hyprland {
  class HyprlandRuntime;
} // namespace compositors::hyprland

class HyprlandWorkspaceBackend final : public WorkspaceBackend,
                                       public WorkspaceOutputNameResolver,
                                       public WorkspaceSocketConnector {
public:
  using OutputNameResolver = WorkspaceOutputNameResolver::Resolver;

  HyprlandWorkspaceBackend(OutputNameResolver outputNameResolver, compositors::hyprland::HyprlandRuntime& runtime);

  bool connectSocket() override;
  void setOutputNameResolver(OutputNameResolver outputNameResolver) override;

  [[nodiscard]] const char* backendName() const override { return "hyprland-ipc"; }
  [[nodiscard]] bool isAvailable() const noexcept override { return m_eventSocketFd >= 0; }
  void setChangeCallback(ChangeCallback callback) override;
  void activate(const std::string& id) override;
  void activateForOutput(wl_output* output, const std::string& id) override;
  void activateForOutput(wl_output* output, const Workspace& workspace) override;
  [[nodiscard]] std::vector<Workspace> all() const override;
  [[nodiscard]] std::vector<Workspace> forOutput(wl_output* output) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appIdsByWorkspace(wl_output* output) const override;
  [[nodiscard]] std::vector<WorkspaceWindow> workspaceWindows(wl_output* output) const override;
  void cleanup() override;

  [[nodiscard]] int pollFd() const noexcept override { return m_eventSocketFd; }
  void dispatchPoll(short revents) override;

private:
  struct WorkspaceState {
    int id = -1;
    std::string name;
    std::string monitor;
    bool active = false;
    bool urgent = false;
    bool occupied = false;
    std::size_t ordinal = 0;
  };

  struct ToplevelState {
    std::string workspace;
    std::string appId;
    std::string title;
    bool urgent = false;
    std::int32_t x = 0;
    std::int32_t y = 0;
  };

  void updateConfigProvider();
  [[nodiscard]] std::optional<nlohmann::json> requestJson(const std::string& request) const;
  void refreshSnapshot();
  void refreshWorkspaces();
  void refreshMonitors();
  void refreshClients();
  void recomputeWorkspaceFlags();
  void notifyChanged() const;

  void readSocket();
  void parseMessages();
  void handleEvent(std::string_view line);
  void handleFocusedMonitor(std::string_view monitorName, std::string_view workspaceName);
  void handleWorkspaceActivated(std::string_view workspaceName);
  void clearUrgentForWorkspace(std::string_view workspaceName);
  void moveToplevel(std::uint64_t address, std::string_view workspaceName);

  [[nodiscard]] WorkspaceState* findWorkspaceById(int id);
  [[nodiscard]] WorkspaceState* findWorkspaceByName(std::string_view name);
  [[nodiscard]] static std::optional<std::uint64_t> parseHexAddress(std::string_view value);
  [[nodiscard]] static std::optional<int> parseInt(std::string_view value);
  [[nodiscard]] static std::vector<std::string_view> parseEventArgs(std::string_view data, std::size_t count);
  [[nodiscard]] static std::string quoteCommandArg(const std::string& value);
  [[nodiscard]] static Workspace toWorkspace(const WorkspaceState& state);

  OutputNameResolver m_outputNameResolver;
  compositors::hyprland::HyprlandRuntime& m_runtime;
  int m_eventSocketFd = -1;
  bool m_configLua;
  std::vector<char> m_readBuffer;
  std::vector<WorkspaceState> m_workspaces;
  std::unordered_map<std::uint64_t, ToplevelState> m_toplevels;
  std::unordered_map<std::string, std::string> m_activeWorkspaceByMonitor;
  std::size_t m_nextOrdinal = 0;
  ChangeCallback m_changeCallback;
};
