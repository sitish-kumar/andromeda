#include "compositors/compositor_runtime.h"

#include "compositors/hyprland/hyprland_runtime.h"
#include "compositors/niri/niri_runtime.h"
#include "compositors/sway/sway_runtime.h"

namespace compositors {

  CompositorRuntimeRegistry::CompositorRuntimeRegistry()
      : m_hyprland(std::make_unique<hyprland::HyprlandRuntime>()), m_niri(std::make_unique<niri::NiriRuntime>()),
        m_sway(std::make_unique<sway::SwayRuntime>()) {}

  CompositorRuntimeRegistry::~CompositorRuntimeRegistry() = default;

  hyprland::HyprlandRuntime& CompositorRuntimeRegistry::hyprland() noexcept { return *m_hyprland; }

  const hyprland::HyprlandRuntime& CompositorRuntimeRegistry::hyprland() const noexcept { return *m_hyprland; }

  niri::NiriRuntime& CompositorRuntimeRegistry::niri() noexcept { return *m_niri; }

  const niri::NiriRuntime& CompositorRuntimeRegistry::niri() const noexcept { return *m_niri; }

  sway::SwayRuntime& CompositorRuntimeRegistry::sway() noexcept { return *m_sway; }

  const sway::SwayRuntime& CompositorRuntimeRegistry::sway() const noexcept { return *m_sway; }

} // namespace compositors
