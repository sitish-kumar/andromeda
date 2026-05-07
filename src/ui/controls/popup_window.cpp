#include "ui/controls/popup_window.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "render/render_context.h"
#include "render/scene/node.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cmath>

namespace {

  constexpr Logger kLog("popup-window");

} // namespace

std::vector<PopupWindow*>& PopupWindow::openPopups() {
  static std::vector<PopupWindow*> popups;
  return popups;
}

PopupWindow::PopupWindow(WaylandConnection& wayland, RenderContext& renderContext)
    : m_wayland(wayland), m_renderContext(renderContext) {}

PopupWindow::~PopupWindow() { close(); }

void PopupWindow::setContentBuilder(ContentBuilder builder) { m_contentBuilder = std::move(builder); }

void PopupWindow::setSceneReadyCallback(SceneReadyCallback callback) { m_sceneReadyCallback = std::move(callback); }

void PopupWindow::setOnDismissed(std::function<void()> callback) { m_onDismissed = std::move(callback); }

void PopupWindow::open(PopupSurfaceConfig config, zwlr_layer_surface_v1* parentLayerSurface, wl_output* output) {
  openCommon(config, parentLayerSurface, nullptr, output);
}

void PopupWindow::openAsChild(PopupSurfaceConfig config, xdg_surface* parentXdgSurface, wl_output* output) {
  openCommon(config, nullptr, parentXdgSurface, output);
}

void PopupWindow::openCommon(PopupSurfaceConfig config, zwlr_layer_surface_v1* parentLayerSurface,
                             xdg_surface* parentXdgSurface, wl_output* output) {
  close();

  m_surface = std::make_unique<PopupSurface>(m_wayland);
  m_surface->setRenderContext(&m_renderContext);

  auto* self = this;
  m_surface->setConfigureCallback(
      [self](std::uint32_t /*w*/, std::uint32_t /*h*/) { self->m_surface->requestLayout(); });

  m_surface->setPrepareFrameCallback([self](bool /*needsUpdate*/, bool needsLayout) {
    if (self->m_surface == nullptr) {
      return;
    }

    const auto width = self->m_surface->width();
    const auto height = self->m_surface->height();
    if (width == 0 || height == 0) {
      return;
    }

    self->m_renderContext.makeCurrent(self->m_surface->renderTarget());

    const bool needsSceneBuild = self->m_sceneRoot == nullptr ||
                                 static_cast<std::uint32_t>(std::round(self->m_sceneRoot->width())) != width ||
                                 static_cast<std::uint32_t>(std::round(self->m_sceneRoot->height())) != height;
    if (needsSceneBuild) {
      UiPhaseScope layoutPhase(UiPhase::Layout);
      self->buildScene(width, height);
      return;
    }

    if (needsLayout && self->m_sceneRoot != nullptr) {
      UiPhaseScope layoutPhase(UiPhase::Layout);
      self->m_sceneRoot->setSize(static_cast<float>(width), static_cast<float>(height));
      self->m_sceneRoot->layout(self->m_renderContext);
      self->m_surface->setSceneRoot(self->m_sceneRoot.get());
    }
  });

  m_surface->setDismissedCallback([self]() { DeferredCall::callLater([self]() { self->close(); }); });

  const bool initialized = parentXdgSurface != nullptr ? m_surface->initializeAsChild(parentXdgSurface, output, config)
                                                       : m_surface->initialize(parentLayerSurface, output, config);
  if (!initialized) {
    kLog.warn("failed to create popup window");
    m_surface.reset();
    return;
  }

  m_wlSurface = m_surface->wlSurface();
  openPopups().push_back(this);
}

void PopupWindow::buildScene(std::uint32_t width, std::uint32_t height) {
  const float fw = static_cast<float>(width);
  const float fh = static_cast<float>(height);

  if (m_contentBuilder) {
    m_sceneRoot = m_contentBuilder(fw, fh);
  }

  if (m_sceneRoot == nullptr) {
    m_sceneRoot = std::make_unique<Node>();
  }
  m_sceneRoot->setSize(fw, fh);
  m_sceneRoot->layout(m_renderContext);

  m_inputDispatcher.setSceneRoot(m_sceneRoot.get());
  m_inputDispatcher.setCursorShapeCallback(
      [this](std::uint32_t serial, std::uint32_t shape) { m_wayland.setCursorShape(serial, shape); });
  m_surface->setSceneRoot(m_sceneRoot.get());

  if (m_sceneReadyCallback) {
    m_sceneReadyCallback(m_inputDispatcher);
  }
}

void PopupWindow::close() {
  const bool wasOpen = m_surface != nullptr;
  auto& popups = openPopups();
  popups.erase(std::remove(popups.begin(), popups.end(), this), popups.end());
  m_sceneRoot.reset();
  m_surface.reset();
  m_inputDispatcher.setSceneRoot(nullptr);
  m_wlSurface = nullptr;
  m_pointerInside = false;
  if (wasOpen && m_onDismissed) {
    m_onDismissed();
  }
}

bool PopupWindow::isOpen() const noexcept { return m_surface != nullptr; }

wl_surface* PopupWindow::wlSurface() const noexcept { return m_wlSurface; }

bool PopupWindow::onPointerEvent(const PointerEvent& event) {
  if (!isOpen()) {
    return false;
  }

  const bool onPopup = (event.surface != nullptr && event.surface == m_wlSurface);

  switch (event.type) {
  case PointerEvent::Type::Enter:
    if (onPopup) {
      m_pointerInside = true;
      m_inputDispatcher.pointerEnter(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
    }
    break;
  case PointerEvent::Type::Leave:
    if (onPopup) {
      m_pointerInside = false;
      m_inputDispatcher.pointerLeave();
    }
    break;
  case PointerEvent::Type::Motion:
    if (onPopup || m_pointerInside) {
      if (onPopup) {
        m_pointerInside = true;
      }
      m_inputDispatcher.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), 0);
      return true;
    }
    break;
  case PointerEvent::Type::Button:
    if (onPopup || m_pointerInside) {
      if (onPopup) {
        m_pointerInside = true;
      }
      const bool pressed = (event.state == 1);
      (void)m_inputDispatcher.pointerButton(static_cast<float>(event.sx), static_cast<float>(event.sy), event.button,
                                            pressed);
      return true;
    }
    break;
  case PointerEvent::Type::Axis:
    if (onPopup || m_pointerInside) {
      return m_inputDispatcher.pointerAxis(static_cast<float>(event.sx), static_cast<float>(event.sy), event.axis,
                                           event.axisSource, event.axisValue, event.axisDiscrete, event.axisValue120,
                                           event.axisLines);
    }
    break;
  }

  if (m_surface != nullptr && m_sceneRoot != nullptr && m_surface->isRunning()) {
    if (m_sceneRoot->layoutDirty()) {
      m_surface->requestLayout();
    } else if (m_sceneRoot->paintDirty()) {
      m_surface->requestRedraw();
    }
  }

  return onPopup;
}

void PopupWindow::onKeyboardEvent(const KeyboardEvent& event) {
  if (!isOpen()) {
    return;
  }
  m_inputDispatcher.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
  if (m_surface != nullptr && m_sceneRoot != nullptr && m_surface->isRunning()) {
    if (m_sceneRoot->layoutDirty()) {
      m_surface->requestLayout();
    } else if (m_sceneRoot->paintDirty()) {
      m_surface->requestRedraw();
    }
  }
}

void PopupWindow::requestLayout() {
  if (m_surface != nullptr) {
    m_surface->requestLayout();
  }
}

void PopupWindow::requestRedraw() {
  if (m_surface != nullptr) {
    m_surface->requestRedraw();
  }
}

bool PopupWindow::dispatchKeyboardEvent(wl_surface* keyboardSurface, const KeyboardEvent& event) {
  if (keyboardSurface == nullptr) {
    return false;
  }

  auto& popups = openPopups();
  for (auto it = popups.rbegin(); it != popups.rend(); ++it) {
    auto* popup = *it;
    if (popup == nullptr || !popup->isOpen()) {
      continue;
    }
    if (popup->wlSurface() == keyboardSurface) {
      popup->onKeyboardEvent(event);
      return true;
    }
  }

  return false;
}

PopupSurfaceConfig PopupWindow::makeConfig(std::int32_t anchorX, std::int32_t anchorY, std::int32_t anchorWidth,
                                           std::int32_t anchorHeight, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t serial, AnchorMode mode, std::int32_t offsetX,
                                           std::int32_t offsetY, bool grab) {
  PopupSurfaceConfig cfg{
      .anchorX = anchorX,
      .anchorY = anchorY,
      .anchorWidth = std::max(1, anchorWidth),
      .anchorHeight = std::max(1, anchorHeight),
      .width = std::max<std::uint32_t>(1, width),
      .height = std::max<std::uint32_t>(1, height),
      .anchor = XDG_POSITIONER_ANCHOR_BOTTOM,
      .gravity = XDG_POSITIONER_GRAVITY_BOTTOM,
      .constraintAdjustment = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y,
      .offsetX = offsetX,
      .offsetY = offsetY,
      .serial = serial,
      .grab = grab,
  };

  if (mode == AnchorMode::CenterOnAnchor) {
    cfg.anchor = XDG_POSITIONER_ANCHOR_BOTTOM;
    cfg.gravity = XDG_POSITIONER_GRAVITY_BOTTOM;
    cfg.anchorX += cfg.anchorWidth / 2;
    cfg.anchorY += cfg.anchorHeight / 2;
    cfg.anchorWidth = 1;
    cfg.anchorHeight = 1;
  }

  return cfg;
}
