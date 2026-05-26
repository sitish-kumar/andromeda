#include "shell/dock/dock.h"

#include "compositors/compositor_platform.h"
#include "config/config_service.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "ipc/ipc_service.h"
#include "render/render_context.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "shell/dock/dock_context_menu.h"
#include "shell/dock/dock_geometry.h"
#include "shell/panel/panel_manager.h"
#include "shell/surface_shadow.h"
#include "shell/tooltip/tooltip_manager.h"
#include "system/app_identity.h"
#include "system/desktop_entry.h"
#include "system/desktop_entry_launch.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"
#include "wayland/layer_surface.h"
#include "wayland/surface.h"
#include "wayland/wayland_toplevels.h"

#include <cerrno>
#include <format>
#include <optional>
#include <wayland-client-core.h>

namespace {

  constexpr Logger kLog("dock");

  // Instance-count badge geometry — scales with icon size.
  constexpr float kBadgeSizeRatio = 0.30f; // fraction of icon size
  constexpr float kBadgeMinSize = 16.0f;   // minimum diameter in px
  constexpr float kBadgeFontRatio = 0.72f; // font size relative to badge diameter
  constexpr float kDotSizeRatio = 0.09f;
  constexpr float kDotMinSize = 4.0f;
  constexpr float kDotGap = 3.0f;

  std::string currentActiveEntryIdLower(const CompositorPlatform& platform) {
    if (const auto active = platform.activeToplevel(); active.has_value()) {
      return StringUtils::toLower(app_identity::resolveRunningDesktopEntry(active->appId, desktopEntries()).id);
    }
    return {};
  }

  wl_output* currentDockFilterOutput(const DockConfig& cfg, wl_output* instanceOutput) {
    if (!cfg.activeMonitorOnly) {
      return nullptr;
    }
    return instanceOutput;
  }

  desktop_entry_launch::LaunchOptions
  dockLaunchOptions(const CompositorPlatform& platform, const ConfigService& config, wl_surface* activationSurface) {
    std::string token;
    if (platform.hasXdgActivation()) {
      token = platform.requestActivationToken(activationSurface);
    }
    return desktop_entry_launch::LaunchOptions{
        .activationToken = std::move(token),
        .runAsSystemdService = config.config().shell.launchAppsAsSystemdServices,
    };
  }

  zwlr_foreign_toplevel_handle_v1* nextActivatableWindowHandle(
      const std::vector<ToplevelInfo>& windows, zwlr_foreign_toplevel_handle_v1* activeHandle,
      zwlr_foreign_toplevel_handle_v1* preferredHandle
  ) {
    for (std::size_t i = 0; i < windows.size(); ++i) {
      if (windows[i].handle != nullptr && windows[i].handle == activeHandle) {
        for (std::size_t offset = 1; offset <= windows.size(); ++offset) {
          auto* nextHandle = windows[(i + offset) % windows.size()].handle;
          if (nextHandle != nullptr) {
            return nextHandle;
          }
        }
        return nullptr;
      }
    }

    for (const auto& window : windows) {
      if (window.handle != nullptr && window.handle == preferredHandle) {
        return window.handle;
      }
    }

    for (const auto& window : windows) {
      if (window.handle != nullptr) {
        return window.handle;
      }
    }
    return nullptr;
  }

  template <typename T> void appendOptionalStackPart(std::string& out, const std::optional<T>& value) {
    out += value.has_value() ? std::format("{}", *value) : "-";
    out.push_back('\x1f');
  }

  std::vector<std::string> barLayerStackSignature(const Config& config) {
    std::vector<std::string> signature;
    signature.reserve(config.bars.size());

    for (const auto& bar : config.bars) {
      std::string item = std::format(
          "{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}", bar.name, bar.position, bar.enabled, bar.autoHide,
          bar.reserveSpace, bar.thickness, bar.marginEnds, bar.marginEdge, bar.shadow
      );

      item.push_back('\x1e');
      for (const auto& override : bar.monitorOverrides) {
        item += override.match;
        item.push_back('\x1f');
        appendOptionalStackPart(item, override.enabled);
        appendOptionalStackPart(item, override.autoHide);
        appendOptionalStackPart(item, override.reserveSpace);
        appendOptionalStackPart(item, override.thickness);
        appendOptionalStackPart(item, override.marginEnds);
        appendOptionalStackPart(item, override.marginEdge);
        appendOptionalStackPart(item, override.shadow);
        item.push_back('\x1e');
      }

      signature.push_back(std::move(item));
    }

    return signature;
  }

  std::string_view dockLauncherIconGlyph(const DockConfig& cfg) {
    return cfg.launcherIcon.empty() ? "grid-dots" : std::string_view{cfg.launcherIcon};
  }

  std::unique_ptr<Flex> makeDockItemRow(const DockConfig& cfg, bool vertical) {
    return ui::flex(
        vertical ? FlexDirection::Vertical : FlexDirection::Horizontal,
        {
            .align = FlexAlign::Center,
            .gap = static_cast<float>(cfg.itemSpacing),
            .padding = static_cast<float>(cfg.padding),
        }
    );
  }

} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

Dock::Dock() = default;
Dock::~Dock() = default;

bool Dock::initialize(CompositorPlatform& platform, ConfigService* config, RenderContext* renderContext) {
  m_platform = &platform;
  m_config = config;
  m_renderContext = renderContext;

  const auto& cfg = m_config->config().dock;
  m_config->addReloadCallback([this]() {
    const auto& newCfg = m_config->config().dock;
    const auto& newShadow = m_config->config().shell.shadow;
    const auto newBarLayerStack = barLayerStackSignature(m_config->config());
    if (newCfg == m_lastDockConfig && newShadow == m_lastShadow && newBarLayerStack == m_lastBarLayerStack) {
      return;
    }
    if (newBarLayerStack != m_lastBarLayerStack && newCfg == m_lastDockConfig && newShadow == m_lastShadow) {
      kLog.info("bar layer stack changed; recreating dock surfaces");
    }
    reload();
  });

  m_lastDockConfig = cfg;
  m_lastShadow = m_config->config().shell.shadow;
  m_lastPinnedConfig = cfg.pinned;
  m_lastBarLayerStack = barLayerStackSignature(m_config->config());

  if (!cfg.enabled) {
    kLog.info("dock disabled in config");
    return true;
  }

  refreshPinnedAppsIfNeeded();
  syncInstances();
  return true;
}

void Dock::reload() {
  kLog.info("reloading config");
  const auto& cfg = m_config->config().dock;
  m_lastDockConfig = cfg;
  m_lastShadow = m_config->config().shell.shadow;
  m_lastBarLayerStack = barLayerStackSignature(m_config->config());

  if (!cfg.enabled) {
    closeAllInstances();
    return;
  }

  refreshPinnedAppsIfNeeded();

  closeAllInstances();

  if (wl_display_roundtrip(m_platform->display()) < 0) {
    const int roundtripErrno = errno;
    kLog.error(
        "Wayland roundtrip failed while reloading dock surfaces: {}",
        m_platform->wayland().describeDisplayError(roundtripErrno)
    );
  }
  syncInstances();
}

void Dock::show() {
  if (m_config == nullptr || !m_config->config().dock.enabled) {
    return;
  }

  pruneCachedToplevelHandles();
  refreshPinnedAppsIfNeeded();
  if (m_instances.empty()) {
    syncInstances();
    return;
  }

  refresh();
}

void Dock::closeAllInstances() {
  m_itemMenu.reset();
  m_lastActiveHandleByAppIdLower.clear();
  m_surfaceMap.clear();
  m_hoveredInstance = nullptr;
  m_popupOwnerInstance = nullptr;
  m_instances.clear();
}

void Dock::pruneCachedToplevelHandles() {
  if (m_platform == nullptr) {
    m_lastActiveHandleByAppIdLower.clear();
    return;
  }

  std::erase_if(m_lastActiveHandleByAppIdLower, [this](const auto& cached) {
    return cached.second == nullptr || !m_platform->containsWlrToplevelHandle(cached.second);
  });
}

void Dock::detachInstanceState(shell::dock::DockInstance& inst) {
  if (inst.surface != nullptr) {
    if (wl_surface* const wls = inst.surface->wlSurface()) {
      m_surfaceMap.erase(wls);
    }
  }
  if (m_hoveredInstance == &inst) {
    m_hoveredInstance = nullptr;
  }
  if (m_popupOwnerInstance == &inst) {
    m_itemMenu.reset();
    m_popupOwnerInstance = nullptr;
  }
}

void Dock::onOutputChange() {
  if (!m_config->config().dock.enabled) {
    return;
  }
  syncInstances();
}

void Dock::refresh() {
  if (m_config == nullptr || m_platform == nullptr || !m_config->config().dock.enabled) {
    return;
  }

  pruneCachedToplevelHandles();
  refreshPinnedAppsIfNeeded();

  syncInstances();

  if (m_instances.empty()) {
    return;
  }

  for (auto& inst : m_instances) {
    if (inst->surface == nullptr) {
      continue;
    }
    inst->surface->requestUpdateOnly();
  }
}

void Dock::toggleVisibility() {
  if (m_instances.empty()) {
    show();
  } else {
    closeAllInstances();
  }
}

void Dock::requestRedraw() {
  for (auto& inst : m_instances) {
    if (inst->surface != nullptr) {
      inst->surface->requestRedraw();
    }
  }
}

void Dock::requestLayout() {
  for (auto& inst : m_instances) {
    if (inst->surface != nullptr) {
      inst->surface->requestLayout();
    }
  }
}

// ── Input ─────────────────────────────────────────────────────────────────────

bool Dock::onPointerEvent(const PointerEvent& event) {
  // Route to any open popup first.
  // If a pointer press is not consumed by the popup, close it and let the same
  // event continue to dock item hit-testing.
  if (m_itemMenu != nullptr) {
    const bool consumed = shell::dock::routePopupEvent(*m_itemMenu, event);
    if (consumed) {
      return true;
    }
    if (event.type == PointerEvent::Type::Button && event.state == 1) {
      closeItemMenu();
    }
  }

  switch (event.type) {
  case PointerEvent::Type::Enter: {
    auto it = m_surfaceMap.find(event.surface);
    if (it == m_surfaceMap.end()) {
      break;
    }
    m_hoveredInstance = it->second;
    m_hoveredInstance->pointerInside = true;
    m_hoveredInstance->inputDispatcher.pointerEnter(
        static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial
    );
    // Auto-hide: show the dock when the pointer enters.
    if (m_config->config().dock.autoHide && m_hoveredInstance->sceneRoot != nullptr) {
      if (m_hoveredInstance->hideAnimId != 0) {
        m_hoveredInstance->animations.cancel(m_hoveredInstance->hideAnimId);
        m_hoveredInstance->hideAnimId = 0;
      }
      const float current = m_hoveredInstance->hideOpacity;
      m_hoveredInstance->hideAnimId = m_hoveredInstance->animations.animate(
          current, 1.0f, Style::animNormal, Easing::EaseOutCubic,
          [inst = m_hoveredInstance, this](float v) {
            inst->hideOpacity = v;
            const auto& cfg = m_config->config().dock;
            shell::dock::syncDockSlideLayerTransform(*inst, cfg);
            shell::dock::applyDockCompositorBlur(*inst, cfg);
          },
          [inst = m_hoveredInstance]() { inst->hideAnimId = 0; }
      );
      // Restore full input region (full surface so shadow-margin edges don't
      // cause an immediate Leave when triggered from the edge of the strip).
      if (m_hoveredInstance->surface != nullptr) {
        const int sw = static_cast<int>(m_hoveredInstance->surface->width());
        const int sh = static_cast<int>(m_hoveredInstance->surface->height());
        m_hoveredInstance->surface->setInputRegion({InputRect{0, 0, sw, sh}});
      }
      m_hoveredInstance->surface->requestRedraw();
    }
    break;
  }
  case PointerEvent::Type::Leave: {
    if (m_hoveredInstance != nullptr) {
      m_hoveredInstance->pointerInside = false;
      m_hoveredInstance->inputDispatcher.pointerLeave();

      // Clear item hover state.
      for (auto& item : m_hoveredInstance->items) {
        if (item.hovered) {
          item.hovered = false;
          if (item.background != nullptr) {
            item.background->setFill(clearColorSpec());
          }
          if (m_hoveredInstance->sceneRoot) {
            m_hoveredInstance->sceneRoot->markPaintDirty();
          }
        }
      }

      if (m_config != nullptr && m_config->config().dock.autoHide && m_popupOwnerInstance == nullptr) {
        shell::dock::startHideFadeOut(*m_hoveredInstance, *m_config);
      }
      m_hoveredInstance = nullptr;
    }
    break;
  }
  case PointerEvent::Type::Motion: {
    if (m_hoveredInstance == nullptr)
      break;
    m_hoveredInstance->inputDispatcher.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), 0);
    break;
  }
  case PointerEvent::Type::Button: {
    auto it = m_surfaceMap.find(event.surface);
    if (it != m_surfaceMap.end()) {
      shell::dock::DockInstance* targetInstance = it->second;
      if (m_hoveredInstance != targetInstance) {
        if (m_hoveredInstance != nullptr) {
          m_hoveredInstance->pointerInside = false;
          m_hoveredInstance->inputDispatcher.pointerLeave();
        }
        m_hoveredInstance = targetInstance;
        m_hoveredInstance->pointerInside = true;
        m_hoveredInstance->inputDispatcher.pointerEnter(
            static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial
        );
      } else {
        m_hoveredInstance->inputDispatcher.pointerMotion(
            static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial
        );
      }
    }

    if (m_hoveredInstance == nullptr)
      break;
    const bool pressed = (event.state == 1);
    m_hoveredInstance->inputDispatcher.pointerButton(
        static_cast<float>(event.sx), static_cast<float>(event.sy), event.button, pressed
    );
    break;
  }
  case PointerEvent::Type::Axis:
    break;
  }

  if (m_hoveredInstance != nullptr
      && m_hoveredInstance->sceneRoot != nullptr
      && (m_hoveredInstance->sceneRoot->paintDirty() || m_hoveredInstance->sceneRoot->layoutDirty())) {
    if (m_hoveredInstance->sceneRoot->layoutDirty()) {
      m_hoveredInstance->surface->requestLayout();
    } else {
      m_hoveredInstance->surface->requestRedraw();
    }
  }

  return m_hoveredInstance != nullptr;
}

// ── Private: instance management ─────────────────────────────────────────────

bool Dock::refreshPinnedAppsIfNeeded() {
  if (desktopEntriesVersion() == m_entriesVersion && m_config->config().dock.pinned == m_lastPinnedConfig) {
    return false;
  }

  m_lastPinnedConfig = m_config->config().dock.pinned;
  m_entriesVersion = desktopEntriesVersion();
  m_pinnedEntries.clear();

  const auto& entries = desktopEntries();

  for (const auto& pinnedId : m_config->config().dock.pinned) {
    const auto pinnedLower = StringUtils::toLower(pinnedId);
    bool found = false;

    for (const auto& entry : entries) {
      if (entry.hidden || entry.noDisplay) {
        continue;
      }
      // Match by entry ID (stem of the desktop file path, e.g. "firefox"),
      // by StartupWMClass (lower), or by Name (lower).
      const auto stemLower = StringUtils::toLower([&] {
        // Extract stem: "org.mozilla.firefox.desktop" → "firefox" (last component, no ext)
        const auto slash = entry.id.rfind('/');
        const auto base = (slash == std::string::npos) ? entry.id : entry.id.substr(slash + 1);
        const auto dot = base.rfind('.');
        return (dot == std::string::npos) ? base : base.substr(0, dot);
      }());

      if (stemLower == pinnedLower || app_identity::desktopEntryMatchesLower(entry, pinnedLower)) {
        m_pinnedEntries.push_back(entry);
        found = true;
        break;
      }
    }

    if (!found) {
      kLog.debug("pinned app not found: {}", pinnedId);
      // Add placeholder so the pinned slot is visible even when app is not installed.
      DesktopEntry placeholder;
      placeholder.id = pinnedId;
      placeholder.name = pinnedId;
      placeholder.nameLower = pinnedLower;
      m_pinnedEntries.push_back(std::move(placeholder));
    }
  }

  ++m_modelSerial;
  kLog.debug("pinned app list: {} entries", m_pinnedEntries.size());
  return true;
}

void Dock::syncInstances() {
  const auto& outputs = m_platform->outputs();
  const auto& cfg = m_config->config().dock;
  const auto& selectedMonitors = cfg.monitors;
  const bool hasStaticContent = !cfg.pinned.empty() || shell::dock::dockLauncherButtonCount(cfg) > 0;
  // When activeMonitorOnly is off, the running-apps check is identical for every output, so hoist it.
  const bool anyRunningGlobal = (!hasStaticContent && cfg.showRunning && !cfg.activeMonitorOnly)
      ? !m_platform->runningAppIds(nullptr).empty()
      : false;
  const auto outputAllowed = [&](const WaylandOutput& output) {
    if (!selectedMonitors.empty()
        && std::none_of(selectedMonitors.begin(), selectedMonitors.end(), [&output](const std::string& m) {
             return outputMatchesSelector(m, output);
           })) {
      return false;
    }
    if (hasStaticContent) {
      return true;
    }
    if (!cfg.showRunning) {
      return false;
    }
    if (cfg.activeMonitorOnly) {
      return !m_platform->runningAppIds(output.output).empty();
    }
    return anyRunningGlobal;
  };

  // Remove instances for dead outputs or outputs no longer selected.
  std::erase_if(m_instances, [this, &outputs, &outputAllowed](const auto& inst) {
    const auto it =
        std::find_if(outputs.begin(), outputs.end(), [&inst](const auto& o) { return o.name == inst->outputName; });
    const bool drop = (it == outputs.end()) || !outputAllowed(*it);
    if (drop) {
      detachInstanceState(*inst);
    }
    return drop;
  });

  for (const auto& output : outputs) {
    if (!output.done)
      continue;
    if (!outputAllowed(output))
      continue;
    const bool exists = std::any_of(m_instances.begin(), m_instances.end(), [&output](const auto& inst) {
      return inst->outputName == output.name;
    });
    if (!exists) {
      createInstance(output);
    }
  }
}

void Dock::createInstance(const WaylandOutput& output) {
  const auto& cfg = m_config->config().dock;
  kLog.info(
      "creating dock on {} ({}) icon_size={} position={}", output.connectorName, output.description, cfg.iconSize,
      cfg.position
  );

  auto instance = std::make_unique<shell::dock::DockInstance>();
  instance->outputName = output.name;
  instance->output = output.output;
  instance->scale = output.scale;
  instance->activeAppIdLower = currentActiveEntryIdLower(*m_platform);

  const auto& shadowConfig = m_config->config().shell.shadow;
  LayerSurfaceConfig lsCfg = shell::dock::makeLayerSurfaceConfig(
      cfg, shadowConfig, cfg.pinned.size() + shell::dock::dockLauncherButtonCount(cfg)
  );

  instance->surface = std::make_unique<LayerSurface>(m_platform->wayland(), std::move(lsCfg));
  instance->surface->setRenderContext(m_renderContext);

  auto* inst = instance.get();
  instance->surface->setConfigureCallback([inst](std::uint32_t /*w*/, std::uint32_t /*h*/) {
    inst->surface->requestLayout();
  });
  instance->surface->setPrepareFrameCallback([this, inst](bool needsUpdate, bool needsLayout) {
    if (m_platform == nullptr || m_config == nullptr || m_renderContext == nullptr) {
      return;
    }
    shell::dock::prepareFrame(
        *inst, {.platform = *m_platform, .config = *m_config, .renderContext = *m_renderContext},
        shell::dock::DockInstanceCallbacks{
            .syncModel = [this](shell::dock::DockInstance& callbackInstance) {
              return syncInstanceModel(callbackInstance);
            },
            .rebuildItems = [this](shell::dock::DockInstance& callbackInstance) { rebuildItems(callbackInstance); },
            .updateVisuals = [this](shell::dock::DockInstance& callbackInstance) { updateVisuals(callbackInstance); },
        },
        needsUpdate, needsLayout
    );
  });
  instance->surface->setAnimationManager(&instance->animations);

  if (!instance->surface->initialize(output.output)) {
    kLog.warn("failed to init dock surface for output {}", output.name);
    return;
  }

  m_surfaceMap[instance->surface->wlSurface()] = instance.get();
  m_instances.push_back(std::move(instance));
}

// ── Private: scene building ───────────────────────────────────────────────────

bool Dock::syncInstanceModel(shell::dock::DockInstance& instance) {
  if (m_platform == nullptr || m_config == nullptr) {
    return false;
  }

  const auto& cfg = m_config->config().dock;
  const std::string globalActiveIdLower = currentActiveEntryIdLower(*m_platform);
  if (!globalActiveIdLower.empty()) {
    if (const auto active = m_platform->activeToplevel(); active.has_value() && active->handle != nullptr) {
      m_lastActiveHandleByAppIdLower[globalActiveIdLower] = active->handle;
    }
  }
  wl_output* const activeOutput = m_platform->activeToplevelOutput();
  wl_output* filterOutput = currentDockFilterOutput(cfg, instance.output);
  const bool filterOutputChanged = (filterOutput != instance.lastFilterOutput);
  instance.lastFilterOutput = filterOutput;

  // When filtering by active monitor, inactive monitors' docks should not
  // highlight the globally-active app — it isn't on them.
  const std::string activeIdLower =
      (cfg.activeMonitorOnly && activeOutput != instance.output) ? std::string{} : globalActiveIdLower;
  instance.activeAppIdLower = activeIdLower;

  const auto runningIds = cfg.showRunning ? m_platform->runningAppIds(filterOutput) : std::vector<std::string>{};
  const auto& allEntries = desktopEntries();
  const auto resolvedRunning = app_identity::resolveRunningApps(runningIds, allEntries);
  std::vector<std::string> runningLower;
  runningLower.reserve(resolvedRunning.size());
  for (const auto& run : resolvedRunning) {
    runningLower.push_back(StringUtils::toLower(run.entry.id));
  }

  bool needRebuild = (instance.modelSerial != m_modelSerial) || filterOutputChanged;
  if (!needRebuild && cfg.showRunning) {
    const std::size_t expectedTotal = [&] {
      std::vector<DesktopEntry> entries = m_pinnedEntries;
      for (const auto& run : resolvedRunning) {
        bool present = false;
        for (const auto& entry : entries) {
          if (app_identity::desktopEntryMatchesLower(entry, run.runningLower)) {
            present = true;
            break;
          }
        }
        if (!present) {
          entries.push_back(run.entry);
        }
      }
      return entries.size();
    }();
    if (expectedTotal != instance.items.size()) {
      needRebuild = true;
    }
  }

  for (auto& item : instance.items) {
    item.running = matchesRunningApp(item, runningLower);
    item.active = matchesActiveApp(item, activeIdLower);
  }

  return needRebuild;
}

// ── Private: item population ──────────────────────────────────────────────────

void Dock::rebuildItems(shell::dock::DockInstance& instance) {
  uiAssertNotRendering("Dock::rebuildItems");
  if (instance.row == nullptr || m_renderContext == nullptr) {
    return;
  }

  const auto& cfg = m_config->config().dock;
  const bool vert = shell::dock::isVerticalPosition(cfg.position);
  const float iSize = static_cast<float>(cfg.iconSize);

  for (auto& item : instance.items) {
    if (item.scaleAnimId != 0) {
      instance.animations.cancel(item.scaleAnimId);
      item.scaleAnimId = 0;
    }
    if (item.opacityAnimId != 0) {
      instance.animations.cancel(item.opacityAnimId);
      item.opacityAnimId = 0;
    }
  }

  // Clear previous items by recreating the row.
  if (instance.row != nullptr && instance.panel != nullptr) {
    instance.panel->removeChild(instance.row);
    instance.row = nullptr;
  }
  instance.items.clear();

  // Create a fresh row.
  auto freshRow = makeDockItemRow(cfg, vert);
  instance.row = static_cast<Flex*>(
      instance.panel != nullptr ? instance.panel->addChild(std::move(freshRow))
                                : instance.sceneRoot->addChild(std::move(freshRow))
  );

  // Determine items: pinned + (optionally) running-only apps not in pinned.
  std::vector<DesktopEntry> itemEntries = m_pinnedEntries;
  wl_output* filterOutput = currentDockFilterOutput(cfg, instance.output);
  instance.lastFilterOutput = filterOutput;

  if (cfg.showRunning) {
    const auto runningIds = m_platform->runningAppIds(filterOutput);
    const auto& allEntries = desktopEntries();
    const auto resolvedRunning = app_identity::resolveRunningApps(runningIds, allEntries);

    for (const auto& run : resolvedRunning) {
      // Skip if already in itemEntries (covers pinned entries).
      bool alreadyPresent = false;
      for (const auto& itm : itemEntries) {
        if (app_identity::desktopEntryMatchesLower(itm, run.runningLower)) {
          alreadyPresent = true;
          break;
        }
      }
      if (alreadyPresent)
        continue;

      itemEntries.push_back(run.entry);
    }
  }

  const auto activeIdLower = instance.activeAppIdLower;
  const auto runningIds = m_platform->runningAppIds(filterOutput);
  const auto resolvedRunning = app_identity::resolveRunningApps(runningIds, desktopEntries());
  std::vector<std::string> runningLower;
  runningLower.reserve(resolvedRunning.size());
  for (const auto& run : resolvedRunning) {
    runningLower.push_back(StringUtils::toLower(run.entry.id));
  }

  if (cfg.launcherPosition == "start") {
    instance.row->addChild(createLauncherButton(instance));
  }

  // Reserve up-front so emplace_back never reallocates while lambdas hold raw pointers.
  instance.items.reserve(itemEntries.size());

  for (const auto& entry : itemEntries) {
    auto& item = instance.items.emplace_back();
    item.entry = entry;
    item.idLower = StringUtils::toLower(entry.id);
    item.startupWmClassLower = StringUtils::toLower(entry.startupWmClass);
    item.active = matchesActiveApp(item, activeIdLower);
    item.running = matchesRunningApp(item, runningLower);

    // Cell is icon + kCellPad on each side; hover bg fills the full cell.
    constexpr float kCellPad = 6.0f; // px extra on each side
    const float cellMain = iSize + 2.0f * kCellPad;
    const float cellCross = iSize + 2.0f * kCellPad;
    auto areaNode = std::make_unique<InputArea>();
    if (!vert) {
      areaNode->setSize(cellMain, cellCross);
    } else {
      areaNode->setSize(cellCross, cellMain);
    }

    // Hover background — fills cell, radius matches dock panel.
    areaNode->addChild(
        ui::box({
            .out = &item.background,
            .fill = clearColorSpec(),
            .radius = static_cast<float>(cfg.radius),
            .width = cellMain,
            .height = cellMain, // square — excludes indicator strip
            .configure = [](Box& box) { box.setPosition(0.0f, 0.0f); },
        })
    );

    // Icon centred inside the padded cell.
    const std::string& iconPath = [&]() -> const std::string& {
      if (!entry.icon.empty()) {
        const std::string& primary = m_iconResolver.resolve(entry.icon, cfg.iconSize);
        if (!primary.empty()) {
          return primary;
        }
      }
      return m_iconResolver.resolve("application-x-executable", cfg.iconSize);
    }();
    auto iconImg = ui::image({
        .width = iSize,
        .height = iSize,
        .configure = [this, &iconPath, &cfg, kCellPad](Image& image) {
          if (!iconPath.empty() && m_renderContext != nullptr) {
            image.setSourceFile(*m_renderContext, iconPath, cfg.iconSize, true);
          }
          image.setPosition(kCellPad, kCellPad);
        },
    });

    if (iconImg->hasImage()) {
      item.iconImage = static_cast<Image*>(areaNode->addChild(std::move(iconImg)));
    } else {
      // Fallback: Tabler app-window glyph (matches launcher when theme icons are unavailable).
      item.iconGlyph = static_cast<Glyph*>(areaNode->addChild(
          ui::glyph({
              .glyph = "app-window",
              .glyphSize = iSize,
              .color = colorSpecFromRole(ColorRole::OnSurface),
              .width = iSize,
              .height = iSize,
              .configure = [kCellPad](Glyph& glyph) { glyph.setPosition(kCellPad, kCellPad); },
          })
      ));
    }

    if (cfg.showDots) {
      const float dot = std::max(kDotMinSize, std::round(iSize * kDotSizeRatio));
      const bool verticalDots = shell::dock::isVerticalPosition(cfg.position);

      for (std::size_t dotIndex = 0; dotIndex < item.dotIndicators.size(); ++dotIndex) {
        item.dotIndicators[dotIndex] = static_cast<Box*>(areaNode->addChild(
            ui::box({
                .fill = colorSpecFromRole(ColorRole::Secondary),
                .radius = dot * 0.5f,
                .width = dot,
                .height = dot,
                .visible = false,
                .configure = [verticalDots, position = cfg.position, cellMain, dot](Box& box) {
                  if (verticalDots) {
                    const float x = position == "left" ? std::round(cellMain - dot - 1.0f) : 1.0f;
                    box.setPosition(x, std::round((cellMain - dot) * 0.5f));
                  } else {
                    const float y = position == "bottom" ? 1.0f : std::round(cellMain - dot - 1.0f);
                    box.setPosition(std::round((cellMain - dot) * 0.5f), y);
                  }
                },
            })
        ));
      }
    }

    // Instance-count badge — top-right corner of the icon, initially hidden.
    if (cfg.showInstanceCount) {
      const float bd = std::max(kBadgeMinSize, iSize * kBadgeSizeRatio);
      const float badgeX = kCellPad + iSize - bd * 0.55f;
      const float badgeY = kCellPad - bd * 0.45f;

      areaNode->addChild(
          ui::box({
              .out = &item.badge,
              .radius = bd * 0.5f,
              .width = bd,
              .height = bd,
              .visible = false,
              .configure = [badgeX, badgeY](Box& box) { box.setPosition(badgeX, badgeY); },
          })
      );

      item.badge->addChild(
          ui::label({
              .out = &item.badgeLabel,
              .fontSize = bd * kBadgeFontRatio,
              .maxLines = 1,
              .fontWeight = FontWeight::Bold,
              .visible = false,
          })
      );
    }

    // Pointer callbacks.
    auto* itemPtr = &item;
    auto* instPtr = &instance;

    areaNode->setOnEnter([itemPtr, instPtr](const InputArea::PointerData&) {
      if (!itemPtr->hovered) {
        itemPtr->hovered = true;
        if (itemPtr->background) {
          itemPtr->background->setFill(colorSpecFromRole(ColorRole::Hover));
        }
        if (instPtr->sceneRoot)
          instPtr->sceneRoot->markPaintDirty();
      }
    });
    areaNode->setOnLeave([itemPtr, instPtr]() {
      if (itemPtr->hovered) {
        itemPtr->hovered = false;
        if (itemPtr->background) {
          itemPtr->background->setFill(clearColorSpec());
        }
        if (instPtr->sceneRoot)
          instPtr->sceneRoot->markPaintDirty();
      }
    });
    areaNode->setAcceptedButtons(InputArea::buttonMask({BTN_LEFT, BTN_RIGHT}));
    areaNode->setOnClick([itemPtr, instPtr, this](const InputArea::PointerData& d) {
      if (d.button == BTN_LEFT) {
        handleItemClick(*instPtr, *itemPtr);
      } else if (d.button == BTN_RIGHT) {
        openItemMenu(*instPtr, *itemPtr);
      }
    });

    item.area = static_cast<InputArea*>(instance.row->addChild(std::move(areaNode)));
  }

  if (cfg.launcherPosition == "end") {
    instance.row->addChild(createLauncherButton(instance));
  }

  instance.modelSerial = m_modelSerial;

  // Force surface resize when item count changes.
  shell::dock::resizeSurface(instance, cfg, m_config->config().shell.shadow);
}

// ── Private: visual update ────────────────────────────────────────────────────

void Dock::updateVisuals(shell::dock::DockInstance& instance) {
  const auto& cfg = m_config->config().dock;

  for (auto& item : instance.items) {
    const float iconScale = item.active ? cfg.activeScale : cfg.inactiveScale;
    const float iconOpacity = item.active ? cfg.activeOpacity : cfg.inactiveOpacity;
    Node* iconNode =
        item.iconImage != nullptr ? static_cast<Node*>(item.iconImage) : static_cast<Node*>(item.iconGlyph);

    if (iconNode != nullptr) {
      if (item.visualScale < 0.0f) {
        item.visualScale = iconScale;
        iconNode->setScale(iconScale);
      } else if (std::abs(item.visualScale - iconScale) > 0.001f) {
        if (item.scaleAnimId != 0) {
          instance.animations.cancel(item.scaleAnimId);
        }
        item.scaleAnimId = instance.animations.animate(
            item.visualScale, iconScale, Style::animNormal, Easing::EaseOutCubic,
            [node = iconNode, itemPtr = &item](float value) {
              itemPtr->visualScale = value;
              node->setScale(value);
            },
            [itemPtr = &item] { itemPtr->scaleAnimId = 0; }
        );
      }

      if (item.visualOpacity < 0.0f) {
        item.visualOpacity = iconOpacity;
        iconNode->setOpacity(iconOpacity);
      } else if (std::abs(item.visualOpacity - iconOpacity) > 0.001f) {
        if (item.opacityAnimId != 0) {
          instance.animations.cancel(item.opacityAnimId);
        }
        item.opacityAnimId = instance.animations.animate(
            item.visualOpacity, iconOpacity, Style::animNormal, Easing::EaseOutCubic,
            [node = iconNode, itemPtr = &item](float value) {
              itemPtr->visualOpacity = value;
              node->setOpacity(value);
            },
            [itemPtr = &item] { itemPtr->opacityAnimId = 0; }
        );
      }
    }

    const bool needsWindowCount = cfg.showDots || item.badge != nullptr;
    std::size_t count = 0;
    if (needsWindowCount) {
      const auto windows = m_platform->windowsForApp(
          item.idLower, item.startupWmClassLower, currentDockFilterOutput(cfg, instance.output)
      );
      count = windows.size();
      item.instanceCount = count;
    }

    if (cfg.showDots) {
      const std::size_t dotCount = std::min<std::size_t>(count, 3);
      const float iSize = static_cast<float>(cfg.iconSize);
      constexpr float kCellPad = 6.0f;
      const float cellMain = iSize + 2.0f * kCellPad;
      const float dot = std::max(kDotMinSize, std::round(iSize * kDotSizeRatio));
      const float groupLength =
          dotCount == 0 ? dot : dot * static_cast<float>(dotCount) + kDotGap * static_cast<float>(dotCount - 1);
      const float groupStart = std::round((cellMain - groupLength) * 0.5f);
      const bool verticalDots = shell::dock::isVerticalPosition(cfg.position);

      for (std::size_t dotIndex = 0; dotIndex < item.dotIndicators.size(); ++dotIndex) {
        if (item.dotIndicators[dotIndex] == nullptr) {
          continue;
        }
        Box* dotNode = item.dotIndicators[dotIndex];
        const bool visible = dotIndex < dotCount;
        dotNode->setVisible(visible);
        dotNode->setFill(colorSpecFromRole(ColorRole::Secondary));
        if (visible) {
          const float main = groupStart + static_cast<float>(dotIndex) * (dot + kDotGap);
          if (verticalDots) {
            const float x = cfg.position == "left" ? std::round(cellMain - dot - 1.0f) : 1.0f;
            dotNode->setPosition(x, main);
          } else {
            const float y = cfg.position == "bottom" ? 1.0f : std::round(cellMain - dot - 1.0f);
            dotNode->setPosition(main, y);
          }
        }
      }
    }

    // Instance-count badge.
    if (item.badge != nullptr && item.badgeLabel != nullptr) {
      const bool show = count >= 2;
      item.badge->setVisible(show);
      item.badgeLabel->setVisible(show);
      if (show) {
        const std::string label = (count > 9) ? "9+" : std::to_string(count);
        item.badgeLabel->setText(label);
        item.badgeLabel->setColor(colorSpecFromRole(ColorRole::OnPrimary));
        item.badge->setFill(colorSpecFromRole(ColorRole::Primary));
        if (m_renderContext != nullptr) {
          const float bd = std::max(kBadgeMinSize, static_cast<float>(cfg.iconSize) * kBadgeSizeRatio);
          item.badgeLabel->measure(*m_renderContext);
          item.badgeLabel->setPosition(
              std::round((bd - item.badgeLabel->width()) * 0.5f), std::round((bd - item.badgeLabel->height()) * 0.5f)
          );
        }
      }
    }
  }
}

// ── Private: helpers ──────────────────────────────────────────────────────────

bool Dock::matchesActiveApp(const shell::dock::DockItemView& item, std::string_view activeIdLower) const {
  return !activeIdLower.empty() && activeIdLower == item.idLower;
}

bool Dock::matchesRunningApp(
    const shell::dock::DockItemView& item, const std::vector<std::string>& runningLower
) const {
  for (const auto& rid : runningLower) {
    if (!rid.empty() && rid == item.idLower) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<InputArea> Dock::createLauncherButton(shell::dock::DockInstance& instance) {
  const auto& cfg = m_config->config().dock;
  const bool vert = shell::dock::isVerticalPosition(cfg.position);
  const float iSize = static_cast<float>(cfg.iconSize);
  constexpr float kCellPad = 6.0f;
  const float cellMain = iSize + 2.0f * kCellPad;
  const float cellCross = iSize + 2.0f * kCellPad;
  const float glyphSize = iSize * 0.8f;
  const float glyphOffsetY = kCellPad + (iSize - glyphSize) * 0.5f;

  auto areaNode = std::make_unique<InputArea>();
  if (!vert) {
    areaNode->setSize(cellMain, cellCross);
  } else {
    areaNode->setSize(cellCross, cellMain);
  }

  Box* bgPtr = nullptr;
  areaNode->addChild(
      ui::box({
          .out = &bgPtr,
          .fill = clearColorSpec(),
          .radius = static_cast<float>(cfg.radius),
          .width = cellMain,
          .height = cellMain,
          .configure = [](Box& box) { box.setPosition(0.0f, 0.0f); },
      })
  );

  areaNode->addChild(
      ui::glyph({
          .glyphSize = glyphSize,
          .color = colorSpecFromRole(ColorRole::OnSurface),
          .width = iSize,
          .height = iSize,
          .configure = [&cfg, kCellPad, glyphOffsetY](Glyph& glyph) {
            if (!glyph.setGlyph(dockLauncherIconGlyph(cfg))) {
              glyph.setGlyph("grid-dots");
            }
            glyph.setPosition(kCellPad, glyphOffsetY);
          },
      })
  );

  auto* instPtr = &instance;
  areaNode->setOnEnter([bgPtr, instPtr](const InputArea::PointerData&) {
    bgPtr->setFill(colorSpecFromRole(ColorRole::Hover));
    if (instPtr->sceneRoot != nullptr) {
      instPtr->sceneRoot->markPaintDirty();
    }
  });
  areaNode->setOnLeave([bgPtr, instPtr]() {
    bgPtr->setFill(clearColorSpec());
    if (instPtr->sceneRoot != nullptr) {
      instPtr->sceneRoot->markPaintDirty();
    }
  });
  areaNode->setAcceptedButtons(InputArea::buttonMask({BTN_LEFT}));
  areaNode->setOnClick([instPtr](const InputArea::PointerData& d) {
    if (d.button == BTN_LEFT) {
      PanelManager::instance().togglePanel("launcher", PanelOpenRequest{.output = instPtr->output});
    }
  });

  return areaNode;
}

// ── Private: click handling ───────────────────────────────────────────────────

void Dock::handleItemClick(shell::dock::DockInstance& instance, shell::dock::DockItemView& item) {
  pruneCachedToplevelHandles();

  auto windows = m_platform->windowsForApp(
      item.idLower, item.startupWmClassLower, currentDockFilterOutput(m_config->config().dock, instance.output)
  );

  if (windows.empty()) {
    wl_surface* const activationSurface = instance.surface != nullptr ? instance.surface->wlSurface() : nullptr;
    (void)desktop_entry_launch::launchEntry(item.entry, dockLaunchOptions(*m_platform, *m_config, activationSurface));
    return;
  }

  if (windows.size() == 1) {
    m_platform->activateToplevel(windows[0].handle);
    return;
  }

  zwlr_foreign_toplevel_handle_v1* activeHandle = nullptr;
  if (const auto active = m_platform->activeToplevel(); active.has_value()) {
    activeHandle = active->handle;
  }

  auto* preferredHandle = [&]() -> zwlr_foreign_toplevel_handle_v1* {
    const auto it = m_lastActiveHandleByAppIdLower.find(item.idLower);
    return it != m_lastActiveHandleByAppIdLower.end() ? it->second : nullptr;
  }();
  if (auto* nextHandle = nextActivatableWindowHandle(windows, activeHandle, preferredHandle); nextHandle != nullptr) {
    m_platform->activateToplevel(nextHandle);
  }
}

// ── Private: item context menu (right-click) ──────────────────────────────────

void Dock::closeItemMenu() {
  shell::dock::DockInstance* owner = m_popupOwnerInstance;
  m_popupOwnerInstance = nullptr;
  m_itemMenu.reset();
  // Fade the owner out — the pointer left the dock to interact with the menu,
  // whether or not the compositor sent a Leave event at that time.
  if (m_config != nullptr && m_config->config().dock.autoHide && owner != nullptr && owner->hideOpacity > 0.0f) {
    owner->pointerInside = false;
    if (m_hoveredInstance == owner) {
      m_hoveredInstance = nullptr;
    }
    shell::dock::startHideFadeOut(*owner, *m_config);
  }
}

void Dock::openItemMenu(shell::dock::DockInstance& instance, shell::dock::DockItemView& item) {
  if (m_config == nullptr) {
    m_popupOwnerInstance = nullptr;
    m_itemMenu.reset();
    return;
  }

  closeItemMenu();

  if (m_platform == nullptr || m_renderContext == nullptr || !m_platform->hasXdgShell()) {
    return;
  }

  m_popupOwnerInstance = &instance;

  auto windows = m_platform->windowsForApp(
      item.idLower, item.startupWmClassLower, currentDockFilterOutput(m_config->config().dock, instance.output)
  );
  const std::string entryId = item.entry.id;
  const std::string entryWorkingDir = item.entry.workingDir;
  const bool entryTerminal = item.entry.terminal;

  shell::dock::DockMenuCallbacks callbacks{
      .activateWindow = [this](zwlr_foreign_toplevel_handle_v1* handle) { m_platform->activateToplevel(handle); },
      .closeWindow = [this](zwlr_foreign_toplevel_handle_v1* handle) { m_platform->closeToplevel(handle); },
      .launchAction =
          [this, entryId, entryWorkingDir, entryTerminal](const DesktopAction& action) {
            (void)desktop_entry_launch::launchAction(
                action, entryId, entryWorkingDir, entryTerminal, dockLaunchOptions(*m_platform, *m_config, nullptr)
            );
          },
      .closeMenu = [this]() { closeItemMenu(); },
  };

  auto* layerSurface =
      instance.surface != nullptr ? m_platform->layerSurfaceFor(instance.surface->wlSurface()) : nullptr;
  m_itemMenu = shell::dock::createItemMenu(
      *m_platform, *m_config, *m_renderContext, layerSurface, instance.output, m_config->config().dock, item.entry,
      windows, callbacks
  );
  if (m_itemMenu == nullptr) {
    m_popupOwnerInstance = nullptr;
  }
}

void Dock::registerIpc(IpcService& ipc) {
  ipc.registerHandler(
      "dock-show",
      [this](const std::string&) -> std::string {
        if (m_config)
          m_config->setDockEnabled(true);
        return "ok\n";
      },
      "dock-show", "Show the dock (persists override)"
  );

  ipc.registerHandler(
      "dock-hide",
      [this](const std::string&) -> std::string {
        if (m_config)
          m_config->setDockEnabled(false);
        return "ok\n";
      },
      "dock-hide", "Hide the dock (persists override)"
  );

  ipc.registerHandler(
      "dock-toggle",
      [this](const std::string&) -> std::string {
        if (m_config)
          m_config->setDockEnabled(!m_config->config().dock.enabled);
        return "ok\n";
      },
      "dock-toggle", "Toggle dock visibility (persists override)"
  );

  ipc.registerHandler(
      "dock-reload",
      [this](const std::string&) -> std::string {
        reload();
        return "ok\n";
      },
      "dock-reload", "Reload dock configuration"
  );
}
