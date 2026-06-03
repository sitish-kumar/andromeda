#include "config/schema/config_schema.h"

#include "config/schema/engine.h"

#include <algorithm>
#include <format>

namespace noctalia::config::schema {

  const Schema<AudioConfig>& audioSchema() {
    static const Schema<AudioConfig> s = {
        field(&AudioConfig::enableOverdrive, "enable_overdrive"),
        field(&AudioConfig::enableSounds, "enable_sounds"),
        field(&AudioConfig::soundVolume, "sound_volume", Range<float>{0.0f, 1.0f}),
        field(&AudioConfig::volumeChangeSound, "volume_change_sound"),
        field(&AudioConfig::notificationSound, "notification_sound"),
    };
    return s;
  }

  const Schema<WeatherConfig>& weatherSchema() {
    static const Schema<WeatherConfig> s = {
        field(&WeatherConfig::enabled, "enabled"),
        field(&WeatherConfig::effects, "effects"),
        field(&WeatherConfig::refreshMinutes, "refresh_minutes"),
        field(&WeatherConfig::unit, "unit"),
    };
    return s;
  }

  const Schema<OsdConfig>& osdSchema() {
    static const Schema<OsdConfig> s = {
        field(&OsdConfig::position, "position"),
        field(&OsdConfig::orientation, "orientation"),
        field(&OsdConfig::scale, "scale", Range<float>{0.5f, 2.5f}),
        field(&OsdConfig::backgroundOpacity, "background_opacity", Range<float>{0.0f, 1.0f}),
        field(&OsdConfig::offsetX, "offset_x", Range<std::int64_t>{0, std::nullopt}),
        field(&OsdConfig::offsetY, "offset_y", Range<std::int64_t>{0, std::nullopt}),
        field(&OsdConfig::monitors, "monitors"),
        field(&OsdConfig::lockKeys, "lock_keys"),
        field(&OsdConfig::keyboardLayout, "keyboard_layout"),
    };
    return s;
  }

  const Schema<BackdropConfig>& backdropSchema() {
    static const Schema<BackdropConfig> s = {
        field(&BackdropConfig::enabled, "enabled"),
        field(&BackdropConfig::blurIntensity, "blur_intensity", Range<float>{0.0f, 1.0f}),
        field(&BackdropConfig::tintIntensity, "tint_intensity", Range<float>{0.0f, 1.0f}),
    };
    return s;
  }

  const Schema<LockscreenConfig>& lockscreenSchema() {
    static const Schema<LockscreenConfig> s = {
        field(&LockscreenConfig::blurredDesktop, "blurred_desktop"),
        field(&LockscreenConfig::blurIntensity, "blur_intensity", Range<float>{0.0f, 1.0f}),
        field(&LockscreenConfig::tintIntensity, "tint_intensity", Range<float>{0.0f, 1.0f}),
        field(&LockscreenConfig::wallpaperBlurIntensity, "wallpaper_blur_intensity", Range<float>{0.0f, 1.0f}),
        field(&LockscreenConfig::wallpaperTintIntensity, "wallpaper_tint_intensity", Range<float>{0.0f, 1.0f}),
    };
    return s;
  }

  namespace {
    // Poll-second floats are stored verbatim here; the [1,120]/disabled clamping
    // happens at consumption, not at parse time — so no Range is attached.
    const Schema<SystemConfig::MonitorConfig>& systemMonitorSchema() {
      static const Schema<SystemConfig::MonitorConfig> s = {
          field(&SystemConfig::MonitorConfig::enabled, "enabled"),
          field(&SystemConfig::MonitorConfig::cpuPollSeconds, "cpu_poll_seconds"),
          field(&SystemConfig::MonitorConfig::gpuPollSeconds, "gpu_poll_seconds"),
          field(&SystemConfig::MonitorConfig::memoryPollSeconds, "memory_poll_seconds"),
          field(&SystemConfig::MonitorConfig::networkPollSeconds, "network_poll_seconds"),
          field(&SystemConfig::MonitorConfig::diskPollSeconds, "disk_poll_seconds"),
      };
      return s;
    }
  } // namespace

  const Schema<SystemConfig>& systemSchema() {
    static const Schema<SystemConfig> s = {
        subTable(&SystemConfig::monitor, "monitor", systemMonitorSchema()),
    };
    return s;
  }

  const Schema<NightLightConfig>& nightlightSchema() {
    static const Schema<NightLightConfig> s = {
        field(&NightLightConfig::enabled, "enabled"),
        field(&NightLightConfig::force, "force"),
        field(&NightLightConfig::dayTemperature, "temperature_day", Range<std::int64_t>{1000, 25000}),
        field(&NightLightConfig::nightTemperature, "temperature_night", Range<std::int64_t>{1000, 25000}),
        // Day must lead night by at least the gap; pull night down, bumping day up
        // only if night would fall below the floor.
        finalize<NightLightConfig>([](NightLightConfig& nl, std::string_view path, Diagnostics& diag) {
          if (nl.dayTemperature - nl.nightTemperature >= NightLightConfig::kTemperatureGap) {
            return;
          }
          const std::int32_t origDay = nl.dayTemperature;
          const std::int32_t origNight = nl.nightTemperature;
          nl.nightTemperature = origDay - NightLightConfig::kTemperatureGap;
          if (nl.nightTemperature < NightLightConfig::kTemperatureMin) {
            nl.nightTemperature = NightLightConfig::kTemperatureMin;
            nl.dayTemperature = NightLightConfig::kTemperatureMin + NightLightConfig::kTemperatureGap;
          }
          diag.warn(
              std::string(path),
              std::format(
                  "temperatures must satisfy day > night (day={}K night={}K); adjusted to day={}K night={}K", origDay,
                  origNight, nl.dayTemperature, nl.nightTemperature
              )
          );
        }),
    };
    return s;
  }

  const Schema<LocationConfig>& locationSchema() {
    static const Schema<LocationConfig> s = {
        field(&LocationConfig::autoLocate, "auto_locate"), field(&LocationConfig::address, "address"),
        field(&LocationConfig::sunset, "sunset"),          field(&LocationConfig::sunrise, "sunrise"),
        field(&LocationConfig::latitude, "latitude"),      field(&LocationConfig::longitude, "longitude"),
    };
    return s;
  }

  const Schema<NotificationConfig>& notificationSchema() {
    static const Schema<NotificationConfig> s = {
        field(&NotificationConfig::enableDaemon, "enable_daemon"),
        field(&NotificationConfig::showAppName, "show_app_name"),
        field(&NotificationConfig::position, "position"),
        field(&NotificationConfig::layer, "layer"),
        field(&NotificationConfig::scale, "scale", Range<float>{0.5f, 2.5f}),
        field(&NotificationConfig::backgroundOpacity, "background_opacity", Range<float>{0.0f, 1.0f}),
        field(&NotificationConfig::offsetX, "offset_x"),
        field(&NotificationConfig::offsetY, "offset_y"),
        field(&NotificationConfig::monitors, "monitors"),
        field(&NotificationConfig::collapseOnDismiss, "collapse_on_dismiss"),
    };
    return s;
  }

  const Schema<DockConfig>& dockSchema() {
    static const Schema<DockConfig> s = {
        field(&DockConfig::enabled, "enabled"),
        field(&DockConfig::position, "position"),
        field(&DockConfig::activeMonitorOnly, "active_monitor_only"),
        field(&DockConfig::iconSize, "icon_size", Range<std::int64_t>{16, 256}),
        field(&DockConfig::padding, "padding", Range<std::int64_t>{0, 100}),
        field(&DockConfig::itemSpacing, "item_spacing", Range<std::int64_t>{0, 100}),
        field(&DockConfig::backgroundOpacity, "background_opacity", Range<float>{0.0f, 1.0f}),
        // `radius` seeds all four corners; per-corner keys below override it.
        custom<DockConfig>(
            "radius",
            [](const toml::table& tbl, DockConfig& d, std::string_view, Diagnostics&) {
              if (auto v = tbl["radius"].value<std::int64_t>()) {
                const auto r = static_cast<std::int32_t>(std::clamp<std::int64_t>(*v, 0, 500));
                d.radius = r;
                d.radiusTopLeft = r;
                d.radiusTopRight = r;
                d.radiusBottomLeft = r;
                d.radiusBottomRight = r;
              }
            },
            [](toml::table& tbl, const DockConfig& d) {
              tbl.insert_or_assign("radius", static_cast<std::int64_t>(d.radius));
            }
        ),
        field(&DockConfig::radiusTopLeft, "radius_top_left", Range<std::int64_t>{0, 500}),
        field(&DockConfig::radiusTopRight, "radius_top_right", Range<std::int64_t>{0, 500}),
        field(&DockConfig::radiusBottomLeft, "radius_bottom_left", Range<std::int64_t>{0, 500}),
        field(&DockConfig::radiusBottomRight, "radius_bottom_right", Range<std::int64_t>{0, 500}),
        field(&DockConfig::marginEnds, "margin_ends", Range<std::int64_t>{0, 500}),
        field(&DockConfig::marginEdge, "margin_edge", Range<std::int64_t>{0, 100}),
        field(&DockConfig::shadow, "shadow"),
        field(&DockConfig::showRunning, "show_running"),
        field(&DockConfig::autoHide, "auto_hide"),
        field(&DockConfig::reserveSpace, "reserve_space"),
        field(&DockConfig::activeScale, "active_scale", Range<float>{0.1f, 1.75f}),
        field(&DockConfig::inactiveScale, "inactive_scale", Range<float>{0.1f, 1.0f}),
        field(&DockConfig::activeOpacity, "active_opacity", Range<float>{0.0f, 1.0f}),
        field(&DockConfig::inactiveOpacity, "inactive_opacity", Range<float>{0.0f, 1.0f}),
        field(&DockConfig::showDots, "show_dots"),
        field(&DockConfig::showInstanceCount, "show_instance_count"),
        // launcher_position accepts none|start|end; anything else warns and is ignored.
        custom<DockConfig>(
            "launcher_position",
            [](const toml::table& tbl, DockConfig& d, std::string_view parentPath, Diagnostics& diag) {
              if (auto v = tbl["launcher_position"].value<std::string>()) {
                if (*v == "none" || *v == "start" || *v == "end") {
                  d.launcherPosition = *v;
                } else {
                  diag.warn(
                      joinPath(parentPath, "launcher_position"),
                      "invalid value '" + *v + "'; expected none, start, or end"
                  );
                }
              }
            },
            [](toml::table& tbl, const DockConfig& d) { tbl.insert_or_assign("launcher_position", d.launcherPosition); }
        ),
        field(&DockConfig::launcherIcon, "launcher_icon"),
        field(&DockConfig::pinned, "pinned"),
        field(&DockConfig::monitors, "monitors"),
    };
    return s;
  }

  namespace {
    const Schema<BrightnessMonitorOverride>& brightnessMonitorSchema() {
      static const Schema<BrightnessMonitorOverride> s = {
          field(&BrightnessMonitorOverride::match, "match"),
          optionalEnumField(&BrightnessMonitorOverride::backend, "backend", kBrightnessBackendPreferences),
      };
      return s;
    }

    const Schema<BatteryDeviceWarningThreshold>& batteryDeviceSchema() {
      static const Schema<BatteryDeviceWarningThreshold> s = {
          field(&BatteryDeviceWarningThreshold::warningThreshold, "warning_threshold", Range<std::int64_t>{0, 100}),
      };
      return s;
    }
  } // namespace

  const Schema<BrightnessConfig>& brightnessSchema() {
    static const Schema<BrightnessConfig> s = {
        field(&BrightnessConfig::enableDdcutil, "enable_ddcutil"),
        field(&BrightnessConfig::ddcutilIgnoreMmids, "ignore_mmids"),
        // Map key seeds `match`; an explicit `match` key inside overrides it.
        namedMap<BrightnessConfig, BrightnessMonitorOverride>(
            &BrightnessConfig::monitorOverrides, "monitor", brightnessMonitorSchema(),
            [](BrightnessMonitorOverride& o, std::string_view name) { o.match = std::string(name); },
            [](const BrightnessMonitorOverride& o) { return o.match; }
        ),
    };
    return s;
  }

  const Schema<BatteryConfig>& batterySchema() {
    static const Schema<BatteryConfig> s = {
        field(&BatteryConfig::warningThreshold, "warning_threshold", Range<std::int64_t>{0, 100}),
        // selector comes only from the map key; empty selectors are dropped.
        namedMap<BatteryConfig, BatteryDeviceWarningThreshold>(
            &BatteryConfig::deviceThresholds, "device", batteryDeviceSchema(),
            [](BatteryDeviceWarningThreshold& d, std::string_view name) { d.selector = std::string(name); },
            [](const BatteryDeviceWarningThreshold& d) { return d.selector; }, /*readSkipEmptyName=*/true
        ),
    };
    return s;
  }

  namespace {
    const Schema<ShortcutConfig>& shortcutSchema() {
      static const Schema<ShortcutConfig> s = {field(&ShortcutConfig::type, "type")};
      return s;
    }
  } // namespace

  const Schema<ControlCenterConfig>& controlCenterSchema() {
    static const Schema<ControlCenterConfig> s = {
        enumField(&ControlCenterConfig::sidebarMode, "sidebar", kControlCenterSidebarModes),
        enumField(&ControlCenterConfig::sidebarSectionMode, "sidebar_section", kControlCenterSidebarModes),
        arrayOf<ControlCenterConfig, ShortcutConfig>(
            &ControlCenterConfig::shortcuts, "shortcuts", shortcutSchema(),
            [](const ShortcutConfig& sc) { return !sc.type.empty(); }
        ),
    };
    return s;
  }

} // namespace noctalia::config::schema
