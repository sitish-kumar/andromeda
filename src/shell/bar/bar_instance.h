#pragma once

#include "config/config_service.h"
#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "shell/bar/widget.h"
#include "shell/panel/attached_panel_context.h"
#include "ui/signal.h"
#include "wayland/layer_surface.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class Box;
class Flex;
class Node;
class Renderer;

struct BarCapsuleRun {
  Node* shell = nullptr;
  Box* bg = nullptr;
  Flex* container = nullptr;
  Node* content = nullptr;
  WidgetBarCapsuleSpec spec{};
  float contentScale = 1.0f;
  std::vector<Widget*> widgets;
};

struct BarInstance {
  std::uint32_t outputName = 0;
  wl_output* output = nullptr;
  std::int32_t scale = 1;
  std::size_t barIndex = 0;
  BarConfig barConfig;
  std::unique_ptr<LayerSurface> surface;
  // sceneRoot must be destroyed before `animations` — ~Node() calls cancelForOwner().
  AnimationManager animations;
  std::unique_ptr<Node> sceneRoot;
  Node* slideRoot = nullptr;
  float slideHiddenDx = 0.0f;
  float slideHiddenDy = 0.0f;
  InputDispatcher inputDispatcher;
  float hideOpacity = 1.0f;
  // bar-hide/toggle IPC on non-autohide bars: release compositor exclusive zone until bar-show (v4 isVisible=false).
  bool ipcLayoutReleased = false;
  // bar-auto-hide-set off keeps autoHide true until the reveal completes; block hover helpers from replacing it.
  bool autoHideDisablePending = false;
  bool pointerInside = false;
  float lastPointerSx = 0.0f;
  float lastPointerSy = 0.0f;
  std::size_t attachedPopupCount = 0;

  // Bar background, shadow, and layout sections (start/center/end along main axis)
  Box* bg = nullptr;
  Box* shadow = nullptr;
  Node* shadowLeftClip = nullptr;
  Node* shadowRightClip = nullptr;
  Box* shadowLeft = nullptr;
  Box* shadowRight = nullptr;
  Node* contentClip = nullptr;
  Node* startSlot = nullptr;
  Node* centerSlot = nullptr;
  Node* endSlot = nullptr;
  Flex* startSection = nullptr;
  Flex* centerSection = nullptr;
  Flex* endSection = nullptr;

  std::vector<std::unique_ptr<Widget>> startWidgets;
  std::vector<std::unique_ptr<Widget>> centerWidgets;
  std::vector<std::unique_ptr<Widget>> endWidgets;
  std::vector<BarCapsuleRun> startCapsuleRuns;
  std::vector<BarCapsuleRun> centerCapsuleRuns;
  std::vector<BarCapsuleRun> endCapsuleRuns;

  Signal<>::ScopedConnection paletteConn;
  std::optional<AttachedPanelGeometry> attachedPanelGeometry;
  bool attachedPanelResizeTestOpen = false;
  std::uint32_t attachedPanelResizeTestExtent = 0;
  float attachedPanelResizeTestProgress = 0.0f;

  // Bar-hosted attached panel: real panel content rendered inside this bar's surface.
  // clip/content nodes live under slideRoot (sibling of contentClip); content holds the
  // panel's released root and slides during the reveal.
  Node* hostedPanelClip = nullptr;
  Node* hostedPanelContent = nullptr;
  bool hostedPanelOpen = false;
  float hostedPanelProgress = 0.0f;
  float hostedPanelMainLen = 0.0f;  // content extent along the bar main axis (logical px)
  float hostedPanelInnerLen = 0.0f; // content extent along the inner axis (logical px)
  float hostedPanelRadius = 0.0f;
  float hostedPanelInset = 0.0f; // padding between the tab edge and the panel content
  bool hostedPanelReadyFired = false;
  std::function<void(Renderer&, float, float)> hostedPanelLayout;
  std::function<void()> hostedPanelClosed;
};
