#include "shell/dock/dock_model.h"

#include "compositors/compositor_platform.h"
#include "system/app_identity.h"
#include "util/string_utils.h"
#include "wayland/wayland_toplevels.h"

namespace shell::dock {

  wl_output* dockFilterOutput(const DockConfig& cfg, wl_output* instanceOutput) {
    if (!cfg.activeMonitorOnly) {
      return nullptr;
    }
    return instanceOutput;
  }

  std::string currentActiveEntryIdLower(const CompositorPlatform& platform) {
    if (const auto active = platform.activeToplevel(); active.has_value()) {
      return StringUtils::toLower(app_identity::resolveRunningDesktopEntry(active->appId, desktopEntries()).id);
    }
    return {};
  }

  namespace {

    bool containsEntryForRunningId(const std::vector<DesktopEntry>& entries, const std::string& runningLower) {
      for (const auto& entry : entries) {
        if (app_identity::desktopEntryMatchesLower(entry, runningLower)) {
          return true;
        }
      }
      return false;
    }

    bool containsRunningEntry(const std::vector<std::string>& runningLower, const std::string& idLower) {
      for (const auto& id : runningLower) {
        if (!id.empty() && id == idLower) {
          return true;
        }
      }
      return false;
    }

  } // namespace

  DockSnapshot buildDockSnapshot(DockModelDependencies deps) {
    DockSnapshot snapshot;
    snapshot.output = deps.output;
    snapshot.filterOutput = dockFilterOutput(deps.config, deps.output);
    snapshot.sourceSerial = deps.sourceSerial;

    const std::string globalActiveIdLower = currentActiveEntryIdLower(deps.platform);
    if (!globalActiveIdLower.empty()) {
      if (const auto active = deps.platform.activeToplevel(); active.has_value() && active->handle != nullptr) {
        deps.lastActiveHandleByAppIdLower[globalActiveIdLower] = active->handle;
      }
    }

    wl_output* const activeOutput = deps.platform.activeToplevelOutput();
    snapshot.activeAppIdLower =
        (deps.config.activeMonitorOnly && activeOutput != deps.output) ? std::string{} : globalActiveIdLower;

    const auto runningIds =
        deps.config.showRunning ? deps.platform.runningAppIds(snapshot.filterOutput) : std::vector<std::string>{};
    const auto resolvedRunning = app_identity::resolveRunningApps(runningIds, desktopEntries());

    std::vector<DesktopEntry> itemEntries = deps.pinnedEntries;
    if (deps.config.showRunning) {
      for (const auto& run : resolvedRunning) {
        if (!containsEntryForRunningId(itemEntries, run.runningLower)) {
          itemEntries.push_back(run.entry);
        }
      }
    }

    std::vector<std::string> runningLower;
    runningLower.reserve(resolvedRunning.size());
    for (const auto& run : resolvedRunning) {
      runningLower.push_back(StringUtils::toLower(run.entry.id));
    }

    snapshot.items.reserve(itemEntries.size());
    for (const auto& entry : itemEntries) {
      DockItemModel item;
      item.entry = entry;
      item.idLower = StringUtils::toLower(entry.id);
      item.startupWmClassLower = StringUtils::toLower(entry.startupWmClass);
      item.running = containsRunningEntry(runningLower, item.idLower);
      item.active = !snapshot.activeAppIdLower.empty() && snapshot.activeAppIdLower == item.idLower;
      if (deps.config.showDots || deps.config.showInstanceCount) {
        item.instanceCount =
            deps.platform.windowsForApp(item.idLower, item.startupWmClassLower, snapshot.filterOutput).size();
      }
      snapshot.items.push_back(std::move(item));
    }

    return snapshot;
  }

  bool sameDockItemSet(const DockSnapshot& a, const DockSnapshot& b) {
    if (a.items.size() != b.items.size()) {
      return false;
    }
    for (std::size_t i = 0; i < a.items.size(); ++i) {
      if (a.items[i].entry.id != b.items[i].entry.id) {
        return false;
      }
      if (a.items[i].idLower != b.items[i].idLower) {
        return false;
      }
      if (a.items[i].startupWmClassLower != b.items[i].startupWmClassLower) {
        return false;
      }
    }
    return true;
  }

} // namespace shell::dock
