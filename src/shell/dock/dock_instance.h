#pragma once

#include "config/config_types.h"
#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "system/desktop_entry.h"
#include "ui/signal.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Box;
class CompositorPlatform;
class ConfigService;
class Flex;
class Glyph;
class Image;
class InputArea;
class Label;
class LayerSurface;
class Node;
class RenderContext;
struct wl_output;

namespace shell::dock {

  struct DockItemView {
    DesktopEntry entry;
    std::string idLower;
    std::string startupWmClassLower;
    InputArea* area = nullptr;
    Box* background = nullptr;
    std::array<Box*, 3> dotIndicators{};
    Box* badge = nullptr;
    Label* badgeLabel = nullptr;
    Image* iconImage = nullptr;
    Glyph* iconGlyph = nullptr;
    bool hovered = false;
    bool running = false;
    bool active = false;
    float visualScale = -1.0f;
    float visualOpacity = -1.0f;
    AnimationManager::Id scaleAnimId = 0;
    AnimationManager::Id opacityAnimId = 0;
    std::size_t instanceCount = 0;
  };

  struct DockInstance {
    std::uint32_t outputName = 0;
    wl_output* output = nullptr;
    std::int32_t scale = 1;
    std::unique_ptr<LayerSurface> surface;
    // sceneRoot must be destroyed before `animations` — ~Node() calls cancelForOwner().
    AnimationManager animations;
    std::unique_ptr<Node> sceneRoot;
    Node* slideRoot = nullptr;
    float slideHiddenDx = 0.0f;
    float slideHiddenDy = 0.0f;
    Box* shadow = nullptr;
    Box* panel = nullptr;
    Flex* row = nullptr;
    InputDispatcher inputDispatcher;
    std::vector<DockItemView> items;
    std::uint64_t modelSerial = 0;
    std::string activeAppIdLower;
    wl_output* lastFilterOutput = nullptr;
    bool pointerInside = false;
    // Auto-hide: tracks visibility [0,1] driven by hover.
    float hideOpacity = 1.0f;
    AnimationManager::Id hideAnimId = 0;
    Signal<>::ScopedConnection paletteConn;
  };

  struct DockInstanceDependencies {
    CompositorPlatform& platform;
    ConfigService& config;
    RenderContext& renderContext;
  };

  struct DockInstanceCallbacks {
    std::function<bool(DockInstance&)> syncModel;
    std::function<void(DockInstance&)> rebuildItems;
    std::function<void(DockInstance&)> updateVisuals;
  };

  void prepareFrame(
      DockInstance& instance, DockInstanceDependencies deps, const DockInstanceCallbacks& callbacks, bool needsUpdate,
      bool needsLayout
  );
  void buildScene(DockInstance& instance, DockInstanceDependencies deps, const DockInstanceCallbacks& callbacks);
  void resizeSurface(
      DockInstance& instance, const DockConfig& cfg, const ShellConfig::ShadowConfig& shadowConfig
  );
  void applyPanelPalette(DockInstance& instance, const DockConfig& cfg);
  void syncDockSlideLayerTransform(DockInstance& instance, const DockConfig& cfg);
  void applyDockCompositorBlur(DockInstance& instance, const DockConfig& cfg);
  void startHideFadeOut(DockInstance& instance, ConfigService& config);

} // namespace shell::dock
