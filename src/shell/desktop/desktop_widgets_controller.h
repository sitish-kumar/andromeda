#pragma once

#include "config/config_service.h"
#include "shell/desktop/desktop_widget_factory.h"
#include "ui/dialogs/layer_popup_host.h"

#include <memory>

class BackgroundWidgetsEditor;
class DesktopWidgetsHost;
class HttpClient;
class LockscreenWidgetsController;
class IpcService;
class MprisService;
class PipeWireSpectrum;
class RenderContext;
class SystemMonitorService;
class WaylandConnection;
class WeatherService;
struct KeyboardEvent;
struct PointerEvent;

using DesktopWidgetsSnapshot = DesktopWidgetsConfig;

class DesktopWidgetsController {
public:
  DesktopWidgetsController();
  ~DesktopWidgetsController();

  DesktopWidgetsController(const DesktopWidgetsController&) = delete;
  DesktopWidgetsController& operator=(const DesktopWidgetsController&) = delete;

  void initialize(
      WaylandConnection& wayland, ConfigService* config, PipeWireSpectrum* pipewireSpectrum,
      const WeatherService* weather, RenderContext* renderContext, MprisService* mpris, HttpClient* httpClient,
      SystemMonitorService* sysmon, LockscreenWidgetsController* lockscreenWidgets,
      DesktopWidgetScriptDeps scriptDeps = {}
  );

  void registerIpc(IpcService& ipc);
  void onOutputChange();
  void onSecondTick();
  void requestLayout();
  void requestRedraw();

  void enterEdit();
  void exitEdit();
  void toggleEdit();

  /// Hides on-screen desktop widgets while another overlay editor (e.g. lockscreen layout) is active.
  void suppressDisplay();
  void unsuppressDisplay();
  /// Tears down desktop widget surfaces while the session lock is active.
  void pauseUnderSessionLock();
  void resumeAfterSessionLock();

  [[nodiscard]] bool isEditing() const noexcept;
  [[nodiscard]] std::optional<LayerPopupParentContext> popupParentContextForSurface(wl_surface* surface) const;
  [[nodiscard]] std::optional<LayerPopupParentContext> fallbackPopupParentContext() const;
  bool onPointerEvent(const PointerEvent& event);
  void onKeyboardEvent(const KeyboardEvent& event);

private:
  void loadSnapshotFromConfig();
  void saveSnapshotToConfig();
  void applyVisibility();
  void handleConfigReload();
  void normalizeSnapshot();

  WaylandConnection* m_wayland = nullptr;
  ConfigService* m_config = nullptr;
  LockscreenWidgetsController* m_lockscreenWidgets = nullptr;
  RenderContext* m_renderContext = nullptr;

  DesktopWidgetsSnapshot m_snapshot;
  bool m_initialized = false;
  bool m_displaySuppressed = false;
  bool m_sessionLockPaused = false;
  std::unique_ptr<DesktopWidgetsHost> m_host;
  std::unique_ptr<BackgroundWidgetsEditor> m_editor;
};
