#pragma once

#include "core/timer_manager.h"
#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "shell/panel/attached_panel_context.h"
#include "shell/panel/panel.h"
#include "shell/panel/panel_click_shield.h"
#include "ui/dialogs/layer_popup_host.h"
#include "wayland/hyprland/focus_grab_service.h"
#include "wayland/hyprland/popup_grab_host.h"
#include "wayland/layer_surface.h"
#include "wayland/surface.h"
#include "wayland/wayland_seat.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

class ConfigService;
class CompositorPlatform;
class ContextMenuPopup;
class SelectDropdownPopup;
class Box;
class IpcService;
class Renderer;
class RenderContext;
class WaylandConnection;
struct PointerEvent;
struct wl_output;
struct wl_surface;

struct PanelOpenRequest {
  wl_output* output = nullptr;
  float anchorX = 0.0f;
  float anchorY = 0.0f;
  bool hasExplicitAnchor = false;
  bool hasAnchorPosition = false;
  std::string_view context = {};
  std::string_view sourceBarName = {};
};

class PanelManager : public PopupGrabHost {
public:
  PanelManager();
  ~PanelManager();

  PanelManager(const PanelManager&) = delete;
  PanelManager& operator=(const PanelManager&) = delete;

  static PanelManager& instance();
  static PanelManager* current() noexcept;

  void initialize(CompositorPlatform& platform, ConfigService* config, RenderContext* renderContext);

  // Optional: invoked from shell UI (e.g. control center) to spawn the standalone settings toplevel.
  void setOpenSettingsWindowCallback(std::function<void()> callback);
  void setCloseSettingsWindowCallback(std::function<void()> callback);
  void setToggleSettingsWindowCallback(std::function<void()> callback);
  void openSettingsWindow();
  void closeSettingsWindow();
  void toggleSettingsWindow();
  // Callback to query the bar surface rects on a given output, in output-local
  // coordinates. The click shield's input region excludes these rects so
  // clicks on bar widgets keep flowing to the bar while a panel is open.
  void setClickShieldExcludeRectsProvider(std::function<std::vector<InputRect>(wl_output*)> provider);
  // Callback returning every bar wl_surface. Used to seed the Hyprland focus
  // grab whitelist so bar widgets keep receiving clicks while a panel is open.
  void setFocusGrabBarSurfacesProvider(std::function<std::vector<wl_surface*>()> provider);
  void setPanelClosedCallback(std::function<void()> callback);
  void setPanelOpenedCallback(std::function<void()> callback);
  void setAttachedPanelAvailabilityCallback(std::function<bool(wl_output*, std::string_view)> callback);
  void setAttachedPanelBarSettledCallback(std::function<bool(wl_output*, std::string_view)> callback);
  // Host an attached panel's content inside the bar's own surface. Returns the host bar's
  // wl_surface (dismissal/keyboard target) or nullptr if the bar cannot host. When set and
  // it succeeds, the panel renders in the bar surface instead of its own layer surface.
  using HostAttachedPanelFn = std::function<wl_surface*(
      wl_output*, std::string_view, std::unique_ptr<Node>, float mainLen, float innerLen, float radius, float inset,
      std::function<void(Renderer&, float, float)>, std::function<void()>
  )>;
  void setHostAttachedPanelCallback(HostAttachedPanelFn callback);
  void setCloseHostedPanelCallback(std::function<void(wl_output*, std::string_view)> callback);
  // Immediate (non-animated) hosted-panel teardown, used when a hosted panel is preempted.
  void setDestroyHostedPanelCallback(std::function<void(wl_output*, std::string_view)> callback);
  // Re-reveal a hosted panel that is mid-close (returns true if it re-revealed its content).
  void setReopenHostedPanelCallback(std::function<bool(wl_output*, std::string_view)> callback);
  // Hosted panel content lives in the bar's scene graph; forward the Panel's relayout/redraw/
  // frame-tick requests to the hosting bar surface (PanelManager has no surface of its own).
  void setRequestHostedPanelLayoutCallback(std::function<void(wl_output*, std::string_view)> callback);
  void setRequestHostedPanelRedrawCallback(std::function<void(wl_output*, std::string_view)> callback);
  void setRequestHostedPanelFrameTickCallback(std::function<void(wl_output*, std::string_view)> callback);
  // Resolves the bar's AnimationManager so a hosted Panel animates in the bar's frame loop.
  void setHostedPanelAnimationManagerQuery(std::function<AnimationManager*(wl_output*, std::string_view)> callback);
  // Resolves the bar's layer surface as the popup parent for popups opened by a hosted panel.
  void setHostedPopupParentContextQuery(
      std::function<std::optional<LayerPopupParentContext>(wl_output*, std::string_view)> callback
  );
  // Keyboard routing for hosted panels: set the initial focus area and forward key events into
  // the bar's input dispatcher (where the hosted content's focus areas / text inputs live).
  void setHostedPanelFocusCallback(std::function<void(wl_output*, std::string_view, InputArea*)> callback);
  void setDispatchHostedPanelKeyCallback(
      std::function<void(wl_output*, std::string_view, std::uint32_t, std::uint32_t, std::uint32_t, bool, bool)>
          callback
  );
  // Installs the hosted panel's Select-dropdown popup context on the bar's hosted content subtree.
  void
  setHostedPanelPopupContextCallback(std::function<void(wl_output*, std::string_view, SelectPopupContext*)> callback);
  // Called each frame by the hosting bar surface so the active hosted Panel can tick.
  void onHostedPanelFrameTick(float deltaMs);
  // Called by the bar once a hosted panel's surface has grown to its full size, so the
  // outside-click dismissal (click shield) can be re-armed against the grown bounds.
  void onHostedPanelReady(wl_output* output, std::string_view barName);
  // Called when an auto-hide bar finishes revealing for an attached panel open.
  void onAttachedBarRevealSettled(wl_output* output, std::string_view barName);

  void registerPanel(const std::string& id, std::unique_ptr<Panel> content);

  void openPanel(const std::string& panelId, PanelOpenRequest request = {});
  void closePanel(bool animateClose = true);
  void togglePanel(const std::string& panelId, PanelOpenRequest request);
  // IPC-friendly overload: asks CompositorPlatform for preferred interactive output.
  void togglePanel(const std::string& panelId);
  void clearClipboardHistory();

  bool onPointerEvent(const PointerEvent& event);
  void onKeyboardEvent(const KeyboardEvent& event);

  [[nodiscard]] bool isOpen() const noexcept;
  [[nodiscard]] bool isOpenPanel(std::string_view panelId) const noexcept;
  [[nodiscard]] bool isPanelTransitionActive() const noexcept;
  [[nodiscard]] bool isAttachedOpen() const noexcept;
  // Output the active panel is on; null when none is open.
  [[nodiscard]] wl_output* attachedPanelOutput() const noexcept;
  // Bar that opened the active panel; empty when none was recorded.
  [[nodiscard]] std::string_view attachedSourceBarName() const noexcept;
  [[nodiscard]] const std::string& activePanelId() const noexcept;
  // True when a panel is open and it reports the given context as active (e.g. control-center tab).
  [[nodiscard]] bool isActivePanelContext(std::string_view context) const noexcept;
  [[nodiscard]] std::optional<LayerPopupParentContext> popupParentContextForSurface(wl_surface* surface) const noexcept;
  [[nodiscard]] std::optional<LayerPopupParentContext> fallbackPopupParentContext() const noexcept;

  [[nodiscard]] RenderContext* renderContext() const noexcept { return m_renderContext; }
  [[nodiscard]] WaylandConnection* wayland() const noexcept;

  void setActivePopup(ContextMenuPopup* popup);
  void clearActivePopup();

  void refresh();
  // Reacts to a ConfigService reload while a panel is open: re-pulls the host bar's
  // per-panel-relevant config (attached background opacity), styling, and compositor
  // blur region. No-op when no panel is open.
  void onConfigReloaded();
  void onIconThemeChanged();
  void focusArea(InputArea* area);
  void requestUpdateOnly();
  void requestLayout();
  // Requests a redraw on the active panel surface without re-running panel
  // update/layout. Used for reactive palette restyling.
  void requestRedraw();
  void requestFrameTick();
  void close();
  void beginAttachedPopup(wl_surface* surface);
  void endAttachedPopup(wl_surface* surface);

  // PopupGrabHost. PopupSurface enrolls itself with us while our focus_grab
  // is active so the compositor doesn't fire `cleared` when the user
  // interacts with a popup opened from inside the panel.
  void registerPopupSurface(wl_surface* surface) override;
  void unregisterPopupSurface(wl_surface* surface) override;

  void registerIpc(IpcService& ipc);

private:
  static PanelManager* s_instance;

  void buildScene(std::uint32_t width, std::uint32_t height);
  void prepareFrame(bool needsUpdate, bool needsLayout);
  // preserveHostedSurface: skip the bar-side hosted teardown (which shrinks the surface to base)
  // because a new hosted panel on the same bar will reclaim and resize the grown surface in place,
  // avoiding a one-frame shrink-then-grow flicker.
  void destroyPanel(bool preserveHostedSurface = false);
  // Surface-agnostic invalidation: target the panel's own surface, or — when the panel is
  // hosted in the bar — forward to the hosting bar surface. (Hosted update folds into layout.)
  void requestPanelLayout();
  void requestPanelRedraw();
  void requestPanelUpdate();
  void requestPanelFrameTick();
  // Called BEFORE the panel surface commits so shields sit below the panel
  // within the layer-shell layer. No-op when the focus-grab path is in use.
  void activateClickShield(LayerShellLayer layer);
  // Called AFTER the panel surface is mapped so the panel wl_surface is
  // available for the whitelist. No-op when focus-grab is unavailable.
  void activateFocusGrab();
  void deactivateOutsideClickHandlers();
  void applyAttachedReveal(float progress);
  void applyDetachedReveal(float progress);
  void startAttachedOpenAnimation();
  // Restyle the attached-panel decoration nodes (bg fill, drop shadow)
  // using the cached attached background opacity and bar position. Geometry/positions are not touched.
  // Safe to call any time after buildScene has run.
  void applyAttachedDecorationStyle();
  // Submit a wl_region matching the visible panel body to the compositor for blur.
  // Clips by m_attachedRevealProgress so the blur grows in lock-step with the
  // open/close animation.
  void applyPanelCompositorBlur();

  CompositorPlatform* m_platform = nullptr;
  ConfigService* m_config = nullptr;
  RenderContext* m_renderContext = nullptr;
  std::function<void()> m_openSettingsWindow;
  std::function<void()> m_closeSettingsWindow;
  std::function<void()> m_toggleSettingsWindow;
  std::function<std::vector<InputRect>(wl_output*)> m_clickShieldExcludeRectsProvider;
  std::function<std::vector<wl_surface*>()> m_focusGrabBarSurfacesProvider;
  std::function<void()> m_panelClosedCallback;
  std::function<void()> m_panelOpenedCallback;
  std::function<bool(wl_output*, std::string_view)> m_attachedPanelAvailabilityCallback;
  std::function<bool(wl_output*, std::string_view)> m_attachedPanelBarSettledCallback;
  HostAttachedPanelFn m_hostAttachedPanelCallback;
  std::function<void(wl_output*, std::string_view)> m_closeHostedPanelCallback;
  std::function<void(wl_output*, std::string_view)> m_destroyHostedPanelCallback;
  std::function<bool(wl_output*, std::string_view)> m_reopenHostedPanelCallback;
  std::function<void(wl_output*, std::string_view)> m_requestHostedPanelLayoutCallback;
  std::function<void(wl_output*, std::string_view)> m_requestHostedPanelRedrawCallback;
  std::function<void(wl_output*, std::string_view)> m_requestHostedPanelFrameTickCallback;
  std::function<AnimationManager*(wl_output*, std::string_view)> m_hostedPanelAnimationManagerQuery;
  std::function<std::optional<LayerPopupParentContext>(wl_output*, std::string_view)> m_hostedPopupParentContextQuery;
  std::function<void(wl_output*, std::string_view, InputArea*)> m_setHostedPanelFocusCallback;
  std::function<void(wl_output*, std::string_view, std::uint32_t, std::uint32_t, std::uint32_t, bool, bool)>
      m_dispatchHostedPanelKeyCallback;
  std::function<void(wl_output*, std::string_view, SelectPopupContext*)> m_setHostedPanelPopupContextCallback;
  bool m_hosted = false;
  LayerShellLayer m_hostedPanelLayer = LayerShellLayer::Top;
  PanelClickShield m_clickShield;
  std::unique_ptr<FocusGrab> m_focusGrab;

  std::unique_ptr<Surface> m_surface;
  LayerSurface* m_layerSurface = nullptr;
  // m_sceneRoot must be destroyed before m_animations — ~Node() calls cancelForOwner().
  // Also m_panels (which own their own Nodes parented under m_sceneRoot) must be destroyed
  // before m_animations for the same reason.
  AnimationManager m_animations;
  std::unique_ptr<Node> m_sceneRoot;
  Node* m_bgNode = nullptr;
  Node* m_contentNode = nullptr;
  Node* m_attachedRevealClipNode = nullptr;
  Node* m_attachedRevealContentNode = nullptr;
  Box* m_panelShadowNode = nullptr;
  InputDispatcher m_inputDispatcher;

  std::unordered_map<std::string, std::unique_ptr<Panel>> m_panels;
  Panel* m_activePanel = nullptr;
  std::string m_activePanelId;
  std::string m_pendingOpenContext;

  wl_output* m_output = nullptr;
  wl_surface* m_wlSurface = nullptr;
  float m_contentWidth = 0.0f;
  float m_contentHeight = 0.0f;
  std::int32_t m_panelInsetX = 0;
  std::int32_t m_panelInsetY = 0;
  std::uint32_t m_panelVisualWidth = 0;
  std::uint32_t m_panelVisualHeight = 0;
  float m_attachedBackgroundOpacity = 1.0f;
  float m_attachedRevealProgress = 1.0f;
  float m_detachedRevealProgress = 1.0f;
  AttachedRevealDirection m_attachedRevealDirection = AttachedRevealDirection::Down;
  Timer m_keyboardRelaxTimer;
  std::string m_attachedBarPosition; // "top" / "bottom" / "left" / "right" while attached, empty otherwise
  std::string m_sourceBarName;       // name of the bar that opened the current panel
  bool m_pointerInside = false;
  bool m_inTransition = false;
  bool m_closing = false;
  bool m_attachedToBar = false;
  bool m_attachedOpenAnimationPending = false;
  std::size_t m_attachedPopupCount = 0;
  ContextMenuPopup* m_activePopup = nullptr;
  std::unique_ptr<SelectDropdownPopup> m_selectPopup;
  std::uint64_t m_destroyGeneration = 0; // invalidates stale deferred destroyPanel calls
};
