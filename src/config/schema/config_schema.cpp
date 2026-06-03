#include "config/schema/config_schema.h"

#include "config/schema/engine.h"
#include "core/key_chord.h"
#include "util/file_utils.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <unordered_map>

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
        field(&NotificationConfig::blacklist, "blacklist"),
        field(&NotificationConfig::blacklistAllowCritical, "blacklist_allow_critical"),
        field(&NotificationConfig::allowedUrgencies, "allowed_urgencies"),
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

  namespace {
    // TOML key is "name" but the field is displayName.
    const Schema<CalendarConfig::Account>& calendarAccountSchema() {
      static const Schema<CalendarConfig::Account> s = {
          field(&CalendarConfig::Account::id, "id"),
          field(&CalendarConfig::Account::type, "type"),
          field(&CalendarConfig::Account::displayName, "name"),
          field(&CalendarConfig::Account::color, "color"),
          field(&CalendarConfig::Account::url, "url"),
          field(&CalendarConfig::Account::username, "username"),
      };
      return s;
    }
  } // namespace

  namespace {
    // optional<ColorSpec> stored as a config string. alwaysEmit writes
    // string-or-empty unconditionally (wallpaper.fill_color); otherwise only when
    // set (monitor overrides). A present non-string value is a hard error.
    template <typename Struct>
    Field<Struct> colorSpecField(std::optional<ColorSpec> Struct::* member, std::string_view key, bool alwaysEmit) {
      return custom<Struct>(
          key,
          [member, key](const toml::table& tbl, Struct& out, std::string_view parentPath, Diagnostics&) {
            if (!tbl.contains(key)) {
              return;
            }
            auto v = tbl[key].value<std::string>();
            if (!v) {
              throw std::runtime_error(joinPath(parentPath, key) + ": expected string ColorSpec");
            }
            if (StringUtils::trim(*v).empty()) {
              out.*member = std::nullopt;
            } else {
              out.*member = colorSpecFromConfigString(*v, joinPath(parentPath, key));
            }
          },
          [member, key, alwaysEmit](toml::table& tbl, const Struct& in) {
            if ((in.*member).has_value()) {
              tbl.insert_or_assign(key, colorSpecToConfigString(*(in.*member)));
            } else if (alwaysEmit) {
              tbl.insert_or_assign(key, std::string{});
            }
          }
      );
    }

    // String holding a filesystem path: ~ and $VARS expand on read, emitted raw.
    template <typename Struct> Field<Struct> pathStringField(std::string Struct::* member, std::string_view key) {
      return custom<Struct>(
          key,
          [member, key](const toml::table& tbl, Struct& out, std::string_view, Diagnostics&) {
            if (auto v = tbl[key].value<std::string>()) {
              out.*member = v->empty() ? *v : FileUtils::expandUserPath(*v).string();
            }
          },
          [member, key](toml::table& tbl, const Struct& in) { tbl.insert_or_assign(key, in.*member); }
      );
    }

    template <typename Struct>
    Field<Struct> optionalPathStringField(std::optional<std::string> Struct::* member, std::string_view key) {
      return custom<Struct>(
          key,
          [member, key](const toml::table& tbl, Struct& out, std::string_view, Diagnostics&) {
            if (auto v = tbl[key].value<std::string>()) {
              out.*member = v->empty() ? *v : FileUtils::expandUserPath(*v).string();
            }
          },
          [member, key](toml::table& tbl, const Struct& in) {
            if ((in.*member).has_value()) {
              tbl.insert_or_assign(key, *(in.*member));
            }
          }
      );
    }

    template <typename Struct>
    Field<Struct> optionalBoolField(std::optional<bool> Struct::* member, std::string_view key) {
      return custom<Struct>(
          key,
          [member, key](const toml::table& tbl, Struct& out, std::string_view, Diagnostics&) {
            if (auto v = tbl[key].value<bool>()) {
              out.*member = *v;
            }
          },
          [member, key](toml::table& tbl, const Struct& in) {
            if ((in.*member).has_value()) {
              tbl.insert_or_assign(key, *(in.*member));
            }
          }
      );
    }

    // vector<Enum> <-> array of enum keys; pushes fallback if the parsed list is empty.
    template <typename Struct, typename Enum, std::size_t N>
    Field<Struct> enumArrayField(
        std::vector<Enum> Struct::* member, std::string_view key, const EnumOption<Enum> (&options)[N],
        std::optional<Enum> fallbackIfEmpty
    ) {
      const EnumOption<Enum>* opts = options;
      return custom<Struct>(
          key,
          [member, key, opts, fallbackIfEmpty](const toml::table& tbl, Struct& out, std::string_view, Diagnostics&) {
            const auto* arr = tbl[key].as_array();
            if (arr == nullptr) {
              return;
            }
            (out.*member).clear();
            for (const auto& item : *arr) {
              if (auto s = item.value<std::string>()) {
                if (auto e = enumLookup(opts, N, *s)) {
                  (out.*member).push_back(*e);
                }
              }
            }
            if ((out.*member).empty() && fallbackIfEmpty) {
              (out.*member).push_back(*fallbackIfEmpty);
            }
          },
          [member, key, opts](toml::table& tbl, const Struct& in) {
            toml::array arr;
            for (auto e : in.*member) {
              const std::string_view k = enumKeyOf(opts, N, e);
              if (!k.empty()) {
                arr.push_back(std::string(k));
              }
            }
            tbl.insert_or_assign(key, std::move(arr));
          }
      );
    }

    const Schema<WallpaperAutomationConfig>& wallpaperAutomationSchema() {
      static const Schema<WallpaperAutomationConfig> s = {
          field(&WallpaperAutomationConfig::enabled, "enabled"),
          field(&WallpaperAutomationConfig::intervalMinutes, "interval_minutes", Range<std::int64_t>{0, 1440}),
          // order accepts case-insensitive random|alphabetical.
          custom<WallpaperAutomationConfig>(
              "order",
              [](const toml::table& tbl, WallpaperAutomationConfig& out, std::string_view parentPath,
                 Diagnostics& diag) {
                if (auto v = tbl["order"].value<std::string>()) {
                  const std::string lowered = StringUtils::toLower(StringUtils::trim(*v));
                  if (auto parsed = enumFromKey(kWallpaperAutomationOrders, lowered)) {
                    out.order = *parsed;
                  } else {
                    diag.warn(joinPath(parentPath, "order"), "expected random|alphabetical, got \"" + *v + "\"");
                  }
                }
              },
              [](toml::table& tbl, const WallpaperAutomationConfig& in) {
                tbl.insert_or_assign("order", std::string(enumToKey(kWallpaperAutomationOrders, in.order)));
              }
          ),
          field(&WallpaperAutomationConfig::recursive, "recursive"),
      };
      return s;
    }

    const Schema<WallpaperMonitorOverride>& wallpaperMonitorSchema() {
      static const Schema<WallpaperMonitorOverride> s = {
          field(&WallpaperMonitorOverride::match, "match"),
          optionalBoolField(&WallpaperMonitorOverride::enabled, "enabled"),
          colorSpecField(&WallpaperMonitorOverride::fillColor, "fill_color", /*alwaysEmit=*/false),
          optionalPathStringField(&WallpaperMonitorOverride::directory, "directory"),
          optionalPathStringField(&WallpaperMonitorOverride::directoryLight, "directory_light"),
          optionalPathStringField(&WallpaperMonitorOverride::directoryDark, "directory_dark"),
      };
      return s;
    }

    // One keybind action: reads a single chord string or an array of them
    // (warning on an unparseable chord, rethrowing on a hard parse exception);
    // writes the configured chords, or the built-in defaults when none are set.
    Field<KeybindsConfig>
    keybindActionField(std::vector<KeyChord> KeybindsConfig::* member, std::string_view key, KeybindAction action) {
      return custom<KeybindsConfig>(
          key,
          [member, key](const toml::table& tbl, KeybindsConfig& out, std::string_view parentPath, Diagnostics& diag) {
            auto& vec = out.*member;
            vec.clear();
            const auto* node = tbl.get(key);
            if (node == nullptr) {
              return;
            }
            auto parseOne = [&](const std::string& spec) {
              try {
                if (auto chord = parseKeyChordSpec(spec)) {
                  vec.push_back(*chord);
                } else {
                  diag.warn(joinPath(parentPath, key), "invalid keybind chord \"" + spec + "\"");
                }
              } catch (const std::exception& e) {
                throw std::runtime_error(std::format("keybinds.{}: {}", key, e.what()));
              }
            };
            if (auto v = node->value<std::string>()) {
              parseOne(*v);
              return;
            }
            if (const auto* arr = node->as_array()) {
              for (const auto& item : *arr) {
                if (auto v = item.value<std::string>()) {
                  parseOne(*v);
                }
              }
            }
          },
          [member, key, action](toml::table& tbl, const KeybindsConfig& in) {
            const auto& values = in.*member;
            toml::array arr;
            auto emit = [&](const std::vector<KeyChord>& chords) {
              for (const auto& chord : chords) {
                std::string serialized = keyChordToString(chord);
                if (!serialized.empty()) {
                  arr.push_back(std::move(serialized));
                }
              }
            };
            if (values.empty()) {
              emit(defaultKeybindSet(action));
            } else {
              emit(values);
            }
            tbl.insert_or_assign(key, std::move(arr));
          }
      );
    }
  } // namespace

  const Schema<KeybindsConfig>& keybindsSchema() {
    static const Schema<KeybindsConfig> s = {
        keybindActionField(&KeybindsConfig::validate, "validate", KeybindAction::Validate),
        keybindActionField(&KeybindsConfig::cancel, "cancel", KeybindAction::Cancel),
        keybindActionField(&KeybindsConfig::left, "left", KeybindAction::Left),
        keybindActionField(&KeybindsConfig::right, "right", KeybindAction::Right),
        keybindActionField(&KeybindsConfig::up, "up", KeybindAction::Up),
        keybindActionField(&KeybindsConfig::down, "down", KeybindAction::Down),
    };
    return s;
  }

  namespace {
    const Schema<IdleBehaviorConfig>& idleBehaviorSchema() {
      static const Schema<IdleBehaviorConfig> s = {
          field(&IdleBehaviorConfig::enabled, "enabled"),
          field(&IdleBehaviorConfig::timeoutSeconds, "timeout"),
          // action is trimmed on read.
          custom<IdleBehaviorConfig>(
              "action",
              [](const toml::table& tbl, IdleBehaviorConfig& out, std::string_view, Diagnostics&) {
                if (auto v = tbl["action"].value<std::string>()) {
                  out.action = StringUtils::trim(*v);
                }
              },
              [](toml::table& tbl, const IdleBehaviorConfig& in) { tbl.insert_or_assign("action", in.action); }
          ),
          field(&IdleBehaviorConfig::command, "command"),
          field(&IdleBehaviorConfig::resumeCommand, "resume_command"),
          // Emitted only for a bare `suspend` that opts out of pre-suspend locking.
          custom<IdleBehaviorConfig>(
              "lock_before_suspend",
              [](const toml::table& tbl, IdleBehaviorConfig& out, std::string_view, Diagnostics&) {
                if (auto v = tbl["lock_before_suspend"].value<bool>()) {
                  out.lockBeforeSuspend = *v;
                }
              },
              [](toml::table& tbl, const IdleBehaviorConfig& in) {
                if (in.action == "suspend" && !in.lockBeforeSuspend) {
                  tbl.insert_or_assign("lock_before_suspend", false);
                }
              }
          ),
          finalize<IdleBehaviorConfig>([](IdleBehaviorConfig& b, std::string_view, Diagnostics&) {
            normalizeIdleBehaviorAction(b);
          }),
      };
      return s;
    }
  } // namespace

  const Schema<IdleConfig>& idleSchema() {
    static const Schema<IdleConfig> s = {
        field(&IdleConfig::preActionFadeSeconds, "pre_action_fade_seconds", Range<float>{0.0f, 120.0f}),
        // behavior_order is emitted here (vector order); the actual reorder runs
        // last, after the behavior map has been read.
        custom<IdleConfig>(
            "behavior_order", [](const toml::table&, IdleConfig&, std::string_view, Diagnostics&) {},
            [](toml::table& tbl, const IdleConfig& in) {
              toml::array order;
              for (const auto& b : in.behaviors) {
                if (!b.name.empty()) {
                  order.push_back(b.name);
                }
              }
              tbl.insert_or_assign("behavior_order", std::move(order));
            }
        ),
        namedMap<IdleConfig, IdleBehaviorConfig>(
            &IdleConfig::behaviors, "behavior", idleBehaviorSchema(),
            [](IdleBehaviorConfig& b, std::string_view name) { b.name = std::string(name); },
            [](const IdleBehaviorConfig& b) { return b.name; }
        ),
        // Keyless finalizer: reorder behaviors to match behavior_order, leaving
        // any unlisted behaviors in their original relative order at the end.
        custom<IdleConfig>(
            "",
            [](const toml::table& tbl, IdleConfig& out, std::string_view, Diagnostics&) {
              const auto* orderArr = tbl["behavior_order"].as_array();
              if (orderArr == nullptr || out.behaviors.empty()) {
                return;
              }
              std::vector<std::string> orderedNames;
              for (const auto& item : *orderArr) {
                if (auto name = item.value<std::string>(); name && !name->empty()) {
                  orderedNames.push_back(*name);
                }
              }
              if (orderedNames.empty()) {
                return;
              }
              std::unordered_map<std::string, IdleBehaviorConfig> byName;
              for (auto& b : out.behaviors) {
                byName.insert_or_assign(b.name, std::move(b));
              }
              std::vector<IdleBehaviorConfig> ordered;
              ordered.reserve(byName.size());
              for (const auto& name : orderedNames) {
                auto it = byName.find(name);
                if (it == byName.end()) {
                  continue;
                }
                ordered.push_back(std::move(it->second));
                byName.erase(it);
              }
              for (auto& [name, b] : byName) {
                (void)name;
                ordered.push_back(std::move(b));
              }
              out.behaviors = std::move(ordered);
            },
            [](toml::table&, const IdleConfig&) {}
        ),
    };
    return s;
  }

  const Schema<WallpaperConfig>& wallpaperSchema() {
    static const Schema<WallpaperConfig> s = {
        field(&WallpaperConfig::enabled, "enabled"),
        enumField(&WallpaperConfig::fillMode, "fill_mode", kWallpaperFillModes),
        colorSpecField(&WallpaperConfig::fillColor, "fill_color", /*alwaysEmit=*/true),
        enumArrayField(
            &WallpaperConfig::transitions, "transition", kWallpaperTransitions,
            std::optional<WallpaperTransition>{WallpaperTransition::Fade}
        ),
        field(&WallpaperConfig::transitionDurationMs, "transition_duration", Range<float>{100.0f, 30000.0f}),
        field(&WallpaperConfig::edgeSmoothness, "edge_smoothness", Range<float>{0.0f, 1.0f}),
        field(&WallpaperConfig::transitionOnStartup, "transition_on_startup"),
        pathStringField(&WallpaperConfig::directory, "directory"),
        pathStringField(&WallpaperConfig::directoryLight, "directory_light"),
        pathStringField(&WallpaperConfig::directoryDark, "directory_dark"),
        field(&WallpaperConfig::perMonitorDirectories, "per_monitor_directories"),
        subTable(&WallpaperConfig::automation, "automation", wallpaperAutomationSchema()),
        namedMap<WallpaperConfig, WallpaperMonitorOverride>(
            &WallpaperConfig::monitorOverrides, "monitor", wallpaperMonitorSchema(),
            [](WallpaperMonitorOverride& o, std::string_view name) { o.match = std::string(name); },
            [](const WallpaperMonitorOverride& o) { return o.match; }
        ),
    };
    return s;
  }

  const Schema<HooksConfig>& hooksSchema() {
    // One field per HookKind, keyed by its canonical name. A value may be a single
    // command string or an array; empty entries are dropped (matching the legacy
    // setHookCommandsFromNode). Every kind is always emitted, even when empty.
    static const Schema<HooksConfig> s = [] {
      Schema<HooksConfig> fields;
      for (std::size_t i = 0; i < static_cast<std::size_t>(HookKind::Count); ++i) {
        const std::string_view key = hookKindKey(static_cast<HookKind>(i));
        fields.push_back(
            custom<HooksConfig>(
                key,
                [i, key](const toml::table& tbl, HooksConfig& out, std::string_view, Diagnostics&) {
                  auto& vec = out.commands[i];
                  vec.clear();
                  const auto* node = tbl.get(key);
                  if (node == nullptr) {
                    return;
                  }
                  if (const auto* str = node->as_string()) {
                    if (!str->get().empty()) {
                      vec.push_back(str->get());
                    }
                    return;
                  }
                  if (const auto* arr = node->as_array()) {
                    for (const auto& item : *arr) {
                      if (auto v = item.value<std::string>(); v && !v->empty()) {
                        vec.push_back(*v);
                      }
                    }
                  }
                },
                [i, key](toml::table& tbl, const HooksConfig& in) {
                  toml::array arr;
                  for (const auto& cmd : in.commands[i]) {
                    arr.push_back(cmd);
                  }
                  tbl.insert_or_assign(key, std::move(arr));
                }
            )
        );
      }
      return fields;
    }();
    return s;
  }

  const Schema<CalendarConfig>& calendarSchema() {
    static const Schema<CalendarConfig> s = {
        field(&CalendarConfig::enabled, "enabled"),
        field(&CalendarConfig::refreshMinutes, "refresh_minutes"),
        arrayOf<CalendarConfig, CalendarConfig::Account>(
            &CalendarConfig::accounts, "accounts", calendarAccountSchema(),
            [](const CalendarConfig::Account& a) { return !a.id.empty() && !a.type.empty(); }
        ),
    };
    return s;
  }

} // namespace noctalia::config::schema
