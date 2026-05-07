#pragma once

#include "render/scene/input_dispatcher.h"
#include "wayland/popup_surface.h"

#include <functional>
#include <memory>

class Node;
class PopupSurface;
class RenderContext;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;
struct wl_surface;
struct xdg_surface;
struct zwlr_layer_surface_v1;

class PopupWindow {
public:
  enum class AnchorMode : std::uint8_t {
    BelowAnchor,
    CenterOnAnchor,
  };

  using ContentBuilder = std::function<std::unique_ptr<Node>(float width, float height)>;
  using SceneReadyCallback = std::function<void(InputDispatcher&)>;

  PopupWindow(WaylandConnection& wayland, RenderContext& renderContext);
  ~PopupWindow();

  void setContentBuilder(ContentBuilder builder);
  void setSceneReadyCallback(SceneReadyCallback callback);
  void setOnDismissed(std::function<void()> callback);

  void open(PopupSurfaceConfig config, zwlr_layer_surface_v1* parentLayerSurface, wl_output* output);
  void openAsChild(PopupSurfaceConfig config, xdg_surface* parentXdgSurface, wl_output* output);
  void close();

  [[nodiscard]] bool isOpen() const noexcept;
  [[nodiscard]] wl_surface* wlSurface() const noexcept;

  [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
  void onKeyboardEvent(const KeyboardEvent& event);

  void requestLayout();
  void requestRedraw();

  [[nodiscard]] static PopupSurfaceConfig makeConfig(std::int32_t anchorX, std::int32_t anchorY,
                                                     std::int32_t anchorWidth, std::int32_t anchorHeight,
                                                     std::uint32_t width, std::uint32_t height, std::uint32_t serial,
                                                     AnchorMode mode, std::int32_t offsetX = 0,
                                                     std::int32_t offsetY = 0, bool grab = true);

private:
  void openCommon(PopupSurfaceConfig config, zwlr_layer_surface_v1* parentLayerSurface, xdg_surface* parentXdgSurface,
                  wl_output* output);
  void buildScene(std::uint32_t width, std::uint32_t height);

  WaylandConnection& m_wayland;
  RenderContext& m_renderContext;
  std::unique_ptr<PopupSurface> m_surface;
  std::unique_ptr<Node> m_sceneRoot;
  InputDispatcher m_inputDispatcher;
  wl_surface* m_wlSurface = nullptr;
  bool m_pointerInside = false;

  ContentBuilder m_contentBuilder;
  SceneReadyCallback m_sceneReadyCallback;
  std::function<void()> m_onDismissed;
};
