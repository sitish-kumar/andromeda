#pragma once

#include "config/config_types.h"
#include "config/schema/field.h"

// Per-section field schemas: the single source of truth for reading, writing,
// and validating each config section. Field order matches the legacy
// configToToml emission order so serialization stays byte-identical.
namespace noctalia::config::schema {

  const Schema<AudioConfig>& audioSchema();
  const Schema<WeatherConfig>& weatherSchema();
  const Schema<OsdConfig>& osdSchema();
  const Schema<BackdropConfig>& backdropSchema();
  const Schema<LockscreenConfig>& lockscreenSchema();
  const Schema<SystemConfig>& systemSchema();
  const Schema<NightLightConfig>& nightlightSchema();
  const Schema<LocationConfig>& locationSchema();
  const Schema<NotificationConfig>& notificationSchema();
  const Schema<DockConfig>& dockSchema();
  const Schema<BrightnessConfig>& brightnessSchema();
  const Schema<BatteryConfig>& batterySchema();
  const Schema<ControlCenterConfig>& controlCenterSchema();
  const Schema<CalendarConfig>& calendarSchema();
  const Schema<KeybindsConfig>& keybindsSchema();
  const Schema<HooksConfig>& hooksSchema();
  const Schema<IdleConfig>& idleSchema();
  const Schema<WallpaperConfig>& wallpaperSchema();
  const Schema<ThemeConfig>& themeSchema();
  const Schema<ShellConfig>& shellSchema();

} // namespace noctalia::config::schema
